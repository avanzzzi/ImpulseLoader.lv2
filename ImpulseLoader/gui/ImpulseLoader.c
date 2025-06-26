/*
 * ImpulseLoader.c
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

#ifdef STANDALONE
#include "standalone.h"
#elif defined(CLAPPLUG)
#include "clapplug.h"
#else
#include "lv2_plugin.cc"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "widgets.cc"

void set_custom_theme(X11_UI *ui) {
    ui->main.color_scheme->normal = (Colors) {
         /* cairo    / r  / g  / b  / a  /  */
        .fg =       { 0.686, 0.729, 0.773, 1.000},
        .bg =       { 0.083, 0.083, 0.083, 1.000},
        .base =     { 0.093, 0.093, 0.093, 1.000},
        .text =     { 0.686, 0.729, 0.773, 1.000},
        .shadow =   { 0.000, 0.000, 0.000, 0.200},
        .frame =    { 0.000, 0.000, 0.000, 1.000},
        .light =    { 0.100, 0.100, 0.100, 1.000}
    };

    ui->main.color_scheme->prelight = (Colors) {
         /* cairo    / r  / g  / b  / a  /  */
        .fg =       { 0.600, 0.600, 0.600, 1.000},
        .bg =       { 0.250, 0.250, 0.250, 1.000},
        .base =     { 0.300, 0.300, 0.300, 1.000},
        .text =     { 1.000, 1.000, 1.000, 1.000},
        .shadow =   { 0.100, 0.100, 0.100, 0.400},
        .frame =    { 0.033, 0.033, 0.033, 1.000},
        .light =    { 0.300, 0.300, 0.300, 1.000}
    };

    ui->main.color_scheme->selected = (Colors) {
         /* cairo    / r  / g  / b  / a  /  */
        .fg =       { 0.900, 0.900, 0.900, 1.000},
        .bg =       { 0.083, 0.083, 0.083, 1.000},
        .base =     { 0.500, 0.180, 0.180, 1.000},
        .text =     { 1.000, 1.000, 1.000, 1.000},
        .shadow =   { 0.800, 0.180, 0.180, 0.200},
        .frame =    { 0.500, 0.180, 0.180, 1.000},
        .light =    { 0.500, 0.180, 0.180, 1.000}
    };

    ui->main.color_scheme->active = (Colors) {
         /* cairo    / r  / g  / b  / a  /  */
        .fg =       { 0.000, 1.000, 1.000, 1.000},
        .bg =       { 0.000, 0.000, 0.000, 1.000},
        .base =     { 0.180, 0.380, 0.380, 1.000},
        .text =     { 0.750, 0.750, 0.750, 1.000},
        .shadow =   { 0.180, 0.380, 0.380, 0.500},
        .frame =    { 0.180, 0.380, 0.380, 1.000},
        .light =    { 0.180, 0.380, 0.380, 1.000}
    };

    ui->main.color_scheme->insensitive = (Colors) {
         /* cairo    / r  / g  / b  / a  /  */
        .fg =       { 0.850, 0.850, 0.850, 0.500},
        .bg =       { 0.100, 0.100, 0.100, 0.500},
        .base =     { 0.000, 0.000, 0.000, 0.500},
        .text =     { 0.900, 0.900, 0.900, 0.500},
        .shadow =   { 0.000, 0.000, 0.000, 0.100},
        .frame =    { 0.000, 0.000, 0.000, 0.500},
        .light =    { 0.100, 0.100, 0.100, 0.500}
    };
}

static void file_load_response(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    ModelPicker* m = (ModelPicker*) w->parent_struct;
    Widget_t *p = (Widget_t*)w->parent;
    X11_UI *ui = (X11_UI*) p->parent_struct;
    if(user_data !=NULL) {
        int old = 0;
        if (ends_with(m->filename, "wav") ||
                   ends_with(m->filename, "WAV") ) {
            old = 2;
        }
        free(m->filename);
        m->filename = NULL;
        m->filename = strdup(*(const char**)user_data);

        sendFileName(ui, m, old);

        free(m->filename);
        m->filename = NULL;
        m->filename = strdup("None");
        expose_widget(ui->win);
        ui->loop_counter = 12;
    }
}

void set_ctl_val_from_host(Widget_t *w, float value) {
    xevfunc store = w->func.value_changed_callback;
    w->func.value_changed_callback = dummy_callback;
    adj_set_value(w->adj, value);
    w->func.value_changed_callback = *(*store);
}

static void file_menu_callback(void *w_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    ModelPicker* m = (ModelPicker*) w->parent_struct;
    Widget_t *p = (Widget_t*)w->parent;
    X11_UI *ui = (X11_UI*) p->parent_struct;
    X11_UI_Private_t *ps = (X11_UI_Private_t*)ui->private_ptr;
    if (!m->filepicker->file_counter) return;
    int v = (int)adj_get_value(w->adj);
    if (v >= m->filepicker->file_counter) {
        free(ps->fname);
        ps->fname = NULL;
        asprintf(&ps->fname, "%s", "None");
    } else {
        free(ps->fname);
        ps->fname = NULL;
        asprintf(&ps->fname, "%s%s%s", m->dir_name, PATH_SEPARATOR, m->filepicker->file_names[v]);
    }
    file_load_response(m->filebutton, (void*)&ps->fname);
}

void plugin_set_window_size(int *w,int *h,const char * plugin_uri) {
    (*w) = 500; //set initial width of main window
    (*h) = 309; //set initial height of main window
}

const char* plugin_set_name() {
    return "ImpulseLoader"; //set plugin name to display on UI
}

void plugin_create_controller_widgets(X11_UI *ui, const char * plugin_uri) {

    ui->win->label = plugin_set_name();
    // connect the expose func
    ui->win->func.expose_callback = draw_window;

    X11_UI_Private_t *ps =(X11_UI_Private_t*)malloc(sizeof(X11_UI_Private_t));
    ui->private_ptr = (void*)ps;
    ps->ir.filename = strdup("None");
    ps->ir.dir_name = NULL;
    ps->fname = NULL;
    ps->ir.filepicker = (FilePicker*)malloc(sizeof(FilePicker));
    fp_init(ps->ir.filepicker, "/");
    asprintf(&ps->ir.filepicker->filter ,"%s", ".wav|.WAV");
    ps->ir.filepicker->use_filter = 1;

// IR

    ps->ir.filebutton = add_lv2_irfile_button (ps->ir.filebutton, ui->win, -3, "IR File", ui, 45,  258, 25, 25);
    ps->ir.filebutton->parent_struct = (void*)&ps->ir;
    ps->ir.filebutton->func.user_callback = file_load_response;

   // ps->wview = add_lv2_waveview (ps->wview, ui->win, "", ui, 180,  80, 135, 60);
   // set_widget_color(ps->wview, 0, 0, 0.3, 0.55, 0.91, 1.0);

    ui->widget[0] = add_lv2_knob (ui->widget[0], ui->win, 3, "Input", ui, 55,  80, 120, 140);
    set_adjustment(ui->widget[0]->adj, 0.0, 0.0, -20.0, 20.0, 0.2, CL_CONTINUOS);
    set_widget_color(ui->widget[0], (Color_state)0, (Color_mod)0, 0.3, 0.55, 0.91, 1.0);
    set_widget_color(ui->widget[0], (Color_state)0, (Color_mod)3,  0.682, 0.686, 0.686, 1.0);

    ui->widget[1] = add_lv2_knob (ui->widget[1], ui->win, 4, "Dry/Wet ", ui, 325,  80, 120, 140);
    set_adjustment(ui->widget[1]->adj, 100.0, 100.0, 0.0, 100.0, 1.0, CL_CONTINUOS);
    set_widget_color(ui->widget[1], (Color_state)0, (Color_mod)0, 0.3, 0.55, 0.91, 1.0);
    set_widget_color(ui->widget[1], (Color_state)0, (Color_mod)3,  0.682, 0.686, 0.686, 1.0);

    ui->widget[2] = add_lv2_switch (ui->widget[2], ui->win, 2, "Off/On", ui, 220,  160, 60, 60);
    set_widget_color(ui->widget[2], (Color_state)0, (Color_mod)0, 0.3, 0.55, 0.91, 1.0);
    set_widget_color(ui->widget[2], (Color_state)0, (Color_mod)3,  0.682, 0.686, 0.686, 1.0);

    ps->ir.fbutton = add_lv2_button(ps->ir.fbutton, ui->win, "", ui, 435,  254, 22, 30);
    ps->ir.fbutton->parent_struct = (void*)&ps->ir;
    combobox_set_pop_position(ps->ir.fbutton, 0);
    combobox_set_entry_length(ps->ir.fbutton, 48);
    combobox_add_entry(ps->ir.fbutton, "None");
    ps->ir.fbutton->func.value_changed_callback = file_menu_callback;

    ui->widget[3] = add_lv2_toggle_button (ui->widget[3], ui->win, 7, "", ui, 75,  258, 25, 25);
    //ui->widget[13] = add_lv2_erase_button (ui->widget[13], ui->elem[0], 17, "", ui, 470, 24, 25, 25);

}


void plugin_cleanup(X11_UI *ui) {
    X11_UI_Private_t *ps = (X11_UI_Private_t*)ui->private_ptr;
    free(ps->fname);
    free(ps->ir.filename);
    free(ps->ir.dir_name);
    fp_free(ps->ir.filepicker);
    free(ps->ir.filepicker);
    // clean up used sources when needed
}


#ifdef __cplusplus
}
#endif
