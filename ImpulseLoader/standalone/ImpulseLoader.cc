/*
 * ImpulseLoader.cc
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */


#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <cmath>

#include <locale.h>

#include "engine.h"
#include "ParallelThread.h"
#define STANDALONE
#include "ImpulseLoader.c"
#include "TextEntry.h"
#include "xmessage-dialog.h"

class ImpulseLoader : public TextEntry
{
public:
    Widget_t*               TopWin;

    ImpulseLoader() : engine() {
        workToDo.store(false, std::memory_order_release);
        processCounter = 0;
        settingsHaveChanged = false;
        disableAutoConnect = false;
        s_time = 0.0;
        ui = (X11_UI*)malloc(sizeof(X11_UI));
        ui->private_ptr = NULL;
        ui->need_resize = 1;
        ui->loop_counter = 4;
        ui->uiKnowSampleRate = false;
        ui->setVerbose = false;
        ui->uiSampleRate = 0;
        ui->f_index = 0;
        title = "ImpulseLoader";
        currentPreset = "Default";
        for(int i = 0;i<CONTROLS;i++)
            ui->widget[i] = NULL;
        getConfigFilePath();
    }

    ~ImpulseLoader() {
        PresetListNames.clear();
        fetch.stop();
        free(ui->private_ptr);
        free(ui);
        //cleanup();
    }

    void startGui() {
        main_init(&ui->main);
        set_custom_theme(ui);
        int w = 1;
        int h = 1;
        plugin_set_window_size(&w,&h,"standalone");
        TopWin  = create_window(&ui->main, os_get_root_window(&ui->main, IS_WINDOW), 0, 0, w, h+20);
        Widget_t* Menu = add_menubar(TopWin, "",0, 0, w, 20);
        Menu->func.expose_callback = draw_menubar;
        ui->win = create_widget(&ui->main, TopWin, 0, 20, w, h);
        widget_set_title(TopWin, title.c_str());
        widget_set_icon_from_png(TopWin,LDVAR(ImpulseLoader_png));
        ui->win->parent_struct = ui;
        ui->win->private_struct = (void*)this;
        ui->win->scale.gravity = NORTHWEST;
        plugin_create_controller_widgets(ui,"standalone");

        Widget_t* EngineMenu = menubar_add_menu(Menu, "Engine");
        Widget_t* QuitMenu = menu_add_entry(EngineMenu, "Quit");
        QuitMenu->parent_struct = (void*)this;
        QuitMenu->func.button_release_callback = quit_callback;
        Widget_t* PresetMenu = menubar_add_menu(Menu, "Presets");
        PresetLoadMenu = menu_add_submenu(PresetMenu, "Load Preset");
        PresetLoadMenu->parent_struct = (void*)this;
        PresetLoadMenu->func.value_changed_callback = load_preset_callback;
        Widget_t* SaveMenu = menu_add_entry(PresetMenu, "Save as ...");
        SaveMenu->parent_struct = (void*)this;
        SaveMenu->func.button_release_callback = save_preset_callback;
        Widget_t* DeleteMenu = menu_add_entry(PresetMenu, "Delete Current");
        DeleteMenu->parent_struct = (void*)this;
        DeleteMenu->func.button_release_callback = delete_preset_callback;
        Widget_t* OptionMenu = menubar_add_menu(Menu, "Options");
        ShowValues = menu_add_check_entry(OptionMenu, "Show Controller values");
        ShowValues->parent_struct = (void*)this;
        ShowValues->func.value_changed_callback = show_values_callback;
        AutoConnect = menu_add_check_entry(OptionMenu, "Disable Auto Connect");
        AutoConnect->parent_struct = (void*)this;
        AutoConnect->func.value_changed_callback = disable_autoconnect_callback;
        OptionMenu = menubar_add_menu(Menu, "IR-Files");
        Widget_t* ModelMenu = menu_add_entry(OptionMenu, "Tone3000 Impulse Responses");
        ModelMenu->parent_struct = (void*)this;
        ModelMenu->func.button_release_callback = check_irs_callback;

        getPresets(ui);
        widget_show_all(TopWin);
    }

    Xputty *getMain() {
        return &ui->main;
    }

    void quitGui() {
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XLockDisplay(ui->main.dpy);
        #endif
        destroy_widget(TopWin, &ui->main);
         #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        XFlush(ui->main.dpy);
        XUnlockDisplay(ui->main.dpy);
        #endif
    }

    void initEngine(uint32_t rate, int32_t prio, int32_t policy) {
        engine.init(rate, prio, policy);
        s_time = (1.0 / (double)rate) * 1000;
        fetch.startTimeout(120);
        fetch.set<ImpulseLoader, &ImpulseLoader::checkEngine>(this);
    }

    void enableEngine(int on) {
        adj_set_value(ui->widget[2]->adj, static_cast<float>(on));
    }

    inline void process(uint32_t n_samples, float* output) {
        if (processCounter > 2) engine.process(n_samples, output, output);
    }


    // send value changes from GUI to the engine
    void sendValueChanged(int port, float value) {
        switch (port) {
            // 0 + 1 audio ports
            case 2:
                engine.bypass = static_cast<uint32_t>(value);
            break;
            case 3:
                engine.plugin1->gain = value;
            break;
            case 4:
                engine.plugin2->dry_wet = value;
            break;
            case 7:
                engine.normA = static_cast<uint32_t>(value);
                engine._cd.store(1, std::memory_order_relaxed);
                engine.conv.set_normalisation(engine.normA);
                workToDo.store(true, std::memory_order_release);
            break;
            default:
            break;
        }
    }

    // send a file name from GUI to the engine
    void sendFileName(ModelPicker* m, int old) {
        X11_UI_Private_t *ps = (X11_UI_Private_t*)ui->private_ptr;
        if ((strcmp(m->filename, "None") == 0)) {
            if (old == 2) {
                if ( m == &ps->ir) {
                    engine.ir_file = m->filename;
                    engine._cd.fetch_add(1, std::memory_order_relaxed);
                }
            } else return;
        } else if (ends_with(m->filename, "wav")||
                   ends_with(m->filename, "WAV") ) {
            if ( m == &ps->ir) {
                engine.ir_file = m->filename;
                engine._cd.fetch_add(1, std::memory_order_relaxed);
            }
        } else return;
        settingsHaveChanged = true;
        workToDo.store(true, std::memory_order_release);
    }


    // load a saved preset
    void loadPreset(int v) {
        if ( v < PresetListNames.size())
            readPreset(PresetListNames[v]);
    }

    void readConfig(std::string name = "Default") {
        try {
            std::ifstream infile(configFile);
            std::string line;
            std::string key;
            std::string value;
            std::string LoadName = "None";
            if (infile.is_open()) {
                infile.imbue (std::locale("C"));
                while (std::getline(infile, line)) {
                    std::istringstream buf(line);
                    buf >> key;
                    buf >> value;
                    if (key.compare("[ShowValue]") == 0) {
                        adj_set_value(ShowValues->adj, check_stod(value));
                    }
                    if (key.compare("[AutoConnect]") == 0) {
                        adj_set_value(AutoConnect->adj, check_stod(value));
                    }
                    if (key.compare("[CurrentPreset]") == 0) {
                        currentPreset =  remove_sub(line, "[CurrentPreset] ");
                        //if (currentPreset.compare("Default") != 0) {
                        //    readPreset(currentPreset);
                        //    break;
                        //}
                    }
                    if (key.compare("[Connection]") == 0) {
                        std::string value2;
                        buf >> value2;
                        connections.push_back(std::tuple<std::string, std::string>(
                            value, value2));
                    }
                    if (key.compare("[Preset]") == 0) LoadName = remove_sub(line, "[Preset] ");
                    if (name.compare(LoadName) == 0) {
                        if (key.compare("[CONTROLS]") == 0) {
                            for (int i = 0; i < CONTROLS; i++) {
                                adj_set_value(ui->widget[i]->adj, check_stod(value));
                                if (!buf) break;
                                buf >> value;
                            }
                        } else if (key.compare("[IrFile]") == 0) {
                            engine.ir_file = remove_sub(line, "[IrFile] ");
                            engine._cd.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    key.clear();
                    value.clear();
                }
                infile.close();
                workToDo.store(true, std::memory_order_release);
                currentPreset = name;
                title = "ImpulseLoader - " + currentPreset;
                widget_set_title(TopWin, title.c_str());
            }
        } catch (std::ifstream::failure const&) {
            return;
        }
    }

    void saveConnections(std::string in_port, std::string out_port) {
        connections.push_back(std::tuple<std::string, std::string>(
            in_port, out_port));
    }

    void getConnections(std::vector<std::tuple< std::string, std::string> > *_connections) {
        if (disableAutoConnect) connections.clear();
        *_connections = connections;
    }

    void clearConnections() {
        connections.clear();
    }

    void cleanup() {
        fetch.stop();
        if (settingsHaveChanged)
            saveConfig();
        connections.clear();
        plugin_cleanup(ui);
        // Xputty free all memory used
        main_quit(&ui->main);
    }

private:
    ParallelThread          fetch;
    X11_UI*                 ui;
    impulseloader::Engine      engine;
    Widget_t*               PresetLoadMenu;
    Widget_t*               ShowValues;
    Widget_t*               AutoConnect;
    int                     processCounter;
    bool                    settingsHaveChanged;
    bool                    disableAutoConnect;
    std::atomic<bool>       workToDo;
    std::string             configFile;
    std::string             presetFile;
    double                  s_time;
    std::vector<std::string> PresetListNames;
    std::string             title;
    std::string             currentPreset;
    std::vector<std::tuple< std::string, std::string> > connections;

    void createPresetMenu() {
        Widget_t *menu = PresetLoadMenu->childlist->childs[0];
        Widget_t *view_port =  menu->childlist->childs[0];
        int i = view_port->childlist->elem-1;
        for(;i>-1;i--) {
            menu_remove_item(menu,view_port->childlist->childs[i]);
        }
        for (auto i = PresetListNames.begin(); i != PresetListNames.end(); i++) {
            menu_add_entry(PresetLoadMenu, (*i).c_str());
        }
    }

    static void draw_menubar(void *w_, void* user_data) noexcept{
        Widget_t *w = (Widget_t*)w_;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        int width = metrics.width;
        int height = metrics.height;
        if (!metrics.visible) return;
        use_bg_color_scheme(w, NORMAL_);
        cairo_rectangle(w->crb,0,0,width,height);
        cairo_fill (w->crb);
        use_bg_color_scheme(w, ACTIVE_);
        cairo_rectangle(w->crb,0,height-2,width,2);
        cairo_fill(w->crb);
    }

    static void quit_callback(void *w_, void* item_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        if (w->flags & HAS_POINTER){
            self->quitGui();
        }
    }

    static void openSite(std::string url) {
        std::string op = "";
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
        op =  std::string("xdg-open ").append(url);
        #else
        op = std::string("start ").append(url);
        #endif
        system(op.c_str());
    }

    static void check_irs_callback(void *w_, void* item_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if (w->flags & HAS_POINTER){
            openSite("\'https://www.tone3000.com/search?gear=ir&order=newest\'");
        }
    }

    static void show_values_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        if (adj_get_value(w->adj)) self->ui->setVerbose = true;
        else self->ui->setVerbose = false;
        for(int i = 0;i<CONTROLS;i++) {
            widget_draw(self->ui->widget[i], NULL);
        }
    }

    static void disable_autoconnect_callback(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        if (adj_get_value(w->adj)) self->disableAutoConnect = true;
        else self->disableAutoConnect = false;
    }

    void getPresets(X11_UI *ui) {
        try {
            std::ifstream infile(presetFile);
            std::string line;
            std::string key;
            std::string value;
            if (infile.is_open()) {
                PresetListNames.clear();
                infile.imbue (std::locale("C"));
                while (std::getline(infile, line)) {
                    std::istringstream buf(line);
                    buf >> key;
                    buf >> value;
                    if (key.compare("[Preset]") == 0) {
                        PresetListNames.push_back(remove_sub(line, "[Preset] "));
                    }
                    key.clear();
                    value.clear();
                }
                infile.close();
            }
        } catch (std::ifstream::failure const&) {
            return;
        }
        createPresetMenu();
    }

    // remove a preset from the config file
    void removePreset(std::string LoadName) {
        std::ifstream infile(presetFile);
        std::ofstream outfile(presetFile + "temp");
        bool save = true;
        std::string line;
        std::string key;
        std::string value;
        std::string ListName;
        if (infile.is_open() && outfile.is_open()) {
            while (std::getline(infile, line)) {
                std::istringstream buf(line);
                buf >> key;
                buf >> value;
                if (key.compare("[Preset]") == 0) {
                    ListName = remove_sub(line, "[Preset] ");
                }
                if (ListName.compare(LoadName) == 0) {
                    save = false;
                } else {
                    save = true;
                }
                if (save) outfile << line<< std::endl;
                key.clear();
                value.clear();
            }
        infile.close();
        outfile.close();
        std::remove(presetFile.c_str());
        std::rename((presetFile + "temp").c_str(), presetFile.c_str());
        getPresets(ui);
        }
    }

    static void question_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if(user_data !=NULL) {
            ImpulseLoader *self = static_cast<ImpulseLoader*>(w->private_struct);
            int response = *(int*)user_data;
            if(response == 0) {
                self->removePreset(self->currentPreset);
                self->currentPreset = "Default";
                self->title = "ImpulseLoader - " + self->currentPreset;
                widget_set_title(self->TopWin, self->title.c_str());
            }
        }
    }

    // delete menu callback
    static void delete_preset_callback(void* w_, void* item_, void* data_) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        std::string message = "Really delete preset " + self->currentPreset + "?";
        Widget_t *dia = open_message_dialog(self->ui->win, QUESTION_BOX, "Delete Current Preset", 
            message.c_str(),NULL);
        os_set_transient_for_hint(self->ui->win, dia);
        self->ui->win->func.dialog_callback = question_response;
   }

    static void save_response(void *w_, void* user_data) {
        Widget_t *w = (Widget_t*)w_;
        if(user_data !=NULL && strlen(*(const char**)user_data)) {
            ImpulseLoader *self = static_cast<ImpulseLoader*>(w->private_struct);
            std::string lname(*(const char**)user_data);
            self->savePreset(lname, true);
        }
    }

    // pop up a text entry to enter a name for a preset to save
    void save_as() {
        Widget_t* dia = showTextEntry(ui->win, 
                    "ImpulseLoader - save preset as:", "Save preset as:");
        int x1, y1;
        os_translate_coords( ui->win, ui->win->widget, 
            os_get_root_window(ui->win->app, IS_WIDGET), 0, 0, &x1, &y1);
        os_move_window(ui->win->app->dpy,dia,x1+190, y1+80);
        ui->win->func.dialog_callback = save_response;
    }

    // save menu callback
    static void save_preset_callback(void* w_, void* item_, void* data_) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        self->save_as();
    }

    // load a saved preset
    static void load_preset_callback(void* w_, void* data_) {
        Widget_t *w = (Widget_t*)w_;
        ImpulseLoader *self = static_cast<ImpulseLoader*>(w->parent_struct);
        self->loadPreset((int)adj_get_value(w->adj));
    }

    float check_stod (const std::string& str) {
        char* point = localeconv()->decimal_point;
        if (std::string(".") != point) {
            std::string::size_type point_it = str.find(".");
            std::string temp_str = str;
            if (point_it != std::string::npos)
                temp_str.replace(point_it, point_it + 1, point);
            return std::stod(temp_str);
        } else return std::stod(str);
    }

    std::string remove_sub(std::string a, std::string b) {
        std::string::size_type fpos = a.find(b);
        if (fpos != std::string::npos )
            a.erase(a.begin() + fpos, a.begin() + fpos + b.length());
        return (a);
    }

    void getConfigFilePath() {
         if (getenv("XDG_CONFIG_HOME")) {
            std::string path = getenv("XDG_CONFIG_HOME");
            configFile = path + "/impulseloader.conf";
            presetFile = path + "/impulseloader.presets";
        } else {
        #if defined(__linux__) || defined(__FreeBSD__) || \
            defined(__NetBSD__) || defined(__OpenBSD__)
            std::string path = getenv("HOME");
            configFile = path +"/.config/impulseloader.conf";
            presetFile = path +"/.config/impulseloader.presets";
        #else
            std::string path = getenv("APPDATA");
            configFile = path +"\\.config\\impulseloader.conf";
            presetFile = path +"\\.config\\impulseloader.presets";
        #endif
       }
    }

    void readPreset(std::string name = "Default") {
        try {
            std::ifstream infile(presetFile);
            std::string line;
            std::string key;
            std::string value;
            std::string LoadName = "None";
            if (infile.is_open()) {
                infile.imbue (std::locale("C"));
                while (std::getline(infile, line)) {
                    std::istringstream buf(line);
                    buf >> key;
                    buf >> value;
                    if (key.compare("[Preset]") == 0) LoadName = remove_sub(line, "[Preset] ");
                    if (name.compare(LoadName) == 0) {
                        if (key.compare("[CONTROLS]") == 0) {
                            for (int i = 0; i < CONTROLS; i++) {
                                adj_set_value(ui->widget[i]->adj, check_stod(value));
                                if (!buf) break;
                                buf >> value;
                            }
                        } else if (key.compare("[IrFile]") == 0) {
                            engine.ir_file = remove_sub(line, "[IrFile] ");
                            engine._cd.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    key.clear();
                    value.clear();
                }
                infile.close();
                workToDo.store(true, std::memory_order_release);
                currentPreset = name;
                title = "ImpulseLoader - " + currentPreset;
                widget_set_title(TopWin, title.c_str());
            }
        } catch (std::ifstream::failure const&) {
            return;
        }
    }

    void writePreset(std::ofstream *outfile, std::string name) {
        *outfile << "[Preset] " << name << std::endl;
        *outfile << "[CONTROLS] ";
        for(int i = 0;i<CONTROLS;i++) {
            *outfile << adj_get_value(ui->widget[i]->adj) << " ";
        }
        *outfile << std::endl;
        *outfile << "[IrFile] " << engine.ir_file << std::endl;
    }

    void savePreset(std::string name = "Default",  bool append = false) {
        if (std::find(PresetListNames.begin(), PresetListNames.end(), name) != PresetListNames.end()) {
            removePreset(name);
        } 
        std::ofstream outfile(presetFile, append ? std::ios::app : std::ios::trunc);
        if (outfile.is_open()) {
            writePreset(&outfile, name);
            outfile.close();
        }
        currentPreset = name;
        title = "ImpulseLoader - " + currentPreset;
        widget_set_title(TopWin, title.c_str());
        getPresets(ui);
    }

    void saveConfig() {
        std::ofstream outfile(configFile, std::ios::trunc);
        if (outfile.is_open()) {
            outfile.imbue (std::locale("C"));
            outfile << "[ShowValue] " << adj_get_value(ShowValues->adj) << std::endl;
            outfile << "[AutoConnect] " << adj_get_value(AutoConnect->adj) << std::endl;
            outfile << "[CurrentPreset] " << currentPreset << std::endl;
            for (auto it = connections.begin(); it != connections.end(); it++) {
                outfile << "[Connection] "  << std::get<0>(*it) << " " <<  std::get<1>(*it) << "\n";
            }

            writePreset(&outfile, "Default");
            outfile.close();
        }
    }

    // rebuild file menu when needed
    void rebuild_file_menu(ModelPicker *m) {
        xevfunc store = m->fbutton->func.value_changed_callback;
        m->fbutton->func.value_changed_callback = dummy_callback;
        combobox_delete_entrys(m->fbutton);
        fp_get_files(m->filepicker, m->dir_name, 0, 1);
        int active_entry = m->filepicker->file_counter-1;
        for(uint32_t i = 0;i<m->filepicker->file_counter;i++) {
            combobox_add_entry(m->fbutton, m->filepicker->file_names[i]);
            if (strcmp(basename(m->filename),m->filepicker->file_names[i]) == 0) 
                active_entry = i;
        }
        combobox_add_entry(m->fbutton, "None");
        adj_set_value(m->fbutton->adj, active_entry);
        combobox_set_menu_size(m->fbutton, min(14, m->filepicker->file_counter+1));
        m->fbutton->func.value_changed_callback = store;
    }

    // confirmation from engine that a file is loaded
    inline void get_file(std::string fileName, ModelPicker *m) {
        if (!fileName.empty() && (fileName.compare("None") != 0)) {
            const char* uri = fileName.c_str();
            if (strcmp(uri, (const char*)m->filename) !=0) {
                free(m->filename);
                m->filename = NULL;
                m->filename = strdup(uri);
                char *dn = strdup(dirname((char*)uri));
                if (m->dir_name == NULL || strcmp((const char*)m->dir_name,
                                                        (const char*)dn) !=0) {
                    free(m->dir_name);
                    m->dir_name = NULL;
                    m->dir_name = strdup(dn);
                    FileButton *filebutton = (FileButton*)m->filebutton->private_struct;
                    filebutton->path = m->dir_name;
                    rebuild_file_menu(m);
                }
                free(dn);
            }
        } else if (strcmp(m->filename, "None") != 0) {
            free(m->filename);
            m->filename = NULL;
            m->filename = strdup("None");
        }
    }

    // timeout loop to check output ports from engine
    void checkEngine() {
        // come back later
        if (processCounter < 3) {
            processCounter++;
            return;
        }
        if (workToDo.load(std::memory_order_acquire)) {
            if (engine.xrworker.getProcess()) {
                workToDo.store(false, std::memory_order_release);
                engine._execute.store(true, std::memory_order_release);
                engine.xrworker.runProcess();
            }
        } else if (engine._notify_ui.load(std::memory_order_acquire)) {
            engine._notify_ui.store(false, std::memory_order_release);
            #if defined(__linux__) || defined(__FreeBSD__) || \
                defined(__NetBSD__) || defined(__OpenBSD__)
            XLockDisplay(ui->main.dpy);
            #endif
            X11_UI_Private_t *ps = (X11_UI_Private_t*)ui->private_ptr;
            get_file(engine.ir_file, &ps->ir);
            expose_widget(ui->win);
            engine._cd.store(0, std::memory_order_release);
            #if defined(__linux__) || defined(__FreeBSD__) || \
                defined(__NetBSD__) || defined(__OpenBSD__)
            XFlush(ui->main.dpy);
            XUnlockDisplay(ui->main.dpy);
            #endif
        }
    }

};
