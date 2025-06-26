/*
 * Ratatouille.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

#ifdef _WIN32
#define MINGW_STDTHREAD_REDUNDANCY_WARNING
#endif

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <cstring>
#include <thread>
#include <unistd.h>


#include "uris.h"

#include "engine.h"

#include "ParallelThread.h"


namespace impulseloader {

////////////////////////////// PLUG-IN CLASS ///////////////////////////

class Ximpulseloader : public uris
{
private:
    Engine                       engine;

    int32_t                      rt_prio;
    int32_t                      rt_policy;
    float*                       input0;
    float*                       output0;
    float*                       _bypass;
    float*                       _gain;
    float*                       _wet_dry;
    float*                       _normA;

    uint32_t                     s_rate;
    double                       s_time;
    int                          processCounter;
    bool                         doit;

    std::atomic<bool>            _restore;

    // private functions
    inline void check_messages(uint32_t n_samples);
    inline void runBufferedDsp(uint32_t n_samples);
    inline void connect_(uint32_t port,void* data);
    inline void init_dsp_(uint32_t rate);
    inline void connect_all__ports(uint32_t port, void* data);
    inline void activate_f();
    inline void clean_up();
    inline void deactivate_f();

public:
    inline LV2_Atom* write_set_file(LV2_Atom_Forge* forge,
                    const LV2_URID xlv2_model, const char* filename);
    inline const LV2_Atom* read_set_file(const LV2_Atom_Object* obj);
    inline void storeFile(LV2_State_Store_Function store,
            LV2_State_Handle handle, const LV2_URID urid, const std::string file);
    inline bool restoreFile(LV2_State_Retrieve_Function retrieve,
                LV2_State_Handle handle, const LV2_URID urid, std::string *file);
    // LV2 Descriptor
    static const LV2_Descriptor descriptor;
    static const void* extension_data(const char* uri);
    // static wrapper to private functions
    static void deactivate(LV2_Handle instance);
    static void cleanup(LV2_Handle instance);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void activate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void* data);

    static LV2_State_Status save_state(LV2_Handle instance,
                                       LV2_State_Store_Function store,
                                       LV2_State_Handle handle, uint32_t flags,
                                       const LV2_Feature* const* features);

    static LV2_State_Status restore_state(LV2_Handle instance,
                                          LV2_State_Retrieve_Function retrieve,
                                          LV2_State_Handle handle, uint32_t flags,
                                          const LV2_Feature* const*   features);

    static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                                double rate, const char* bundle_path,
                                const LV2_Feature* const* features);
  
    static LV2_Worker_Status work(LV2_Handle                 instance,
                                LV2_Worker_Respond_Function respond,
                                LV2_Worker_Respond_Handle   handle,
                                uint32_t size, const void*    data);
  
    static LV2_Worker_Status work_response(LV2_Handle  instance,
                                         uint32_t    size,
                                         const void* data);
    Ximpulseloader();
    ~Ximpulseloader();
};

// constructor
Ximpulseloader::Ximpulseloader() :
    engine(),
    rt_prio(0),
    rt_policy(0),
    input0(NULL),
    output0(NULL),
    _bypass(0),
    _gain(0),
    _wet_dry(0),
    _normA(0) {
        map = nullptr;
        schedule = nullptr;
        control = nullptr;
        notify = nullptr;
        log = nullptr;
        memset(&logger,0,sizeof(logger));
};

// destructor
Ximpulseloader::~Ximpulseloader() {
};

///////////////////////// PRIVATE CLASS  FUNCTIONS /////////////////////

void Ximpulseloader::init_dsp_(uint32_t rate)
{
    s_rate = rate;
    s_time = (1.0 / (double)s_rate) * 1000;

    if (!rt_policy) rt_policy = 1; //SCHED_FIFO;
    engine.init(rate, rt_prio, rt_policy);

    processCounter = 0;
    doit = false;
    _restore.store(false, std::memory_order_release);
}

// connect the Ports used by the plug-in class
void Ximpulseloader::connect_(uint32_t port,void* data)
{
    switch (port)
    {
        case 0:
            input0 = static_cast<float*>(data);
            break;
        case 1:
            output0 = static_cast<float*>(data);
            break;
        case 2:
            _bypass = static_cast<float*>(data);
            break;
        case 3:
            _gain = static_cast<float*>(data);
            break;
        case 4:
            _wet_dry = static_cast<float*>(data);
            break;
        case 5:
            control = (const LV2_Atom_Sequence*)data;
            break;
        case 6:
            notify = (LV2_Atom_Sequence*)data;
            break;
        case 7:
            _normA = static_cast<float*>(data);
            break;
        default:
            break;
    }
}

void Ximpulseloader::activate_f()
{
    // allocate the internal DSP mem
}

void Ximpulseloader::clean_up()
{
    engine.clean_up();
    // delete the internal DSP mem
}

void Ximpulseloader::deactivate_f()
{
    // delete the internal DSP mem
}

// prepare atom message with file path
inline LV2_Atom* Ximpulseloader::write_set_file(LV2_Atom_Forge* forge,
                    const LV2_URID xlv2_model, const char* filename) {

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(forge, 0);
    LV2_Atom* set = (LV2_Atom*)lv2_atom_forge_object(
                        forge, &frame, 1, patch_Set);

    lv2_atom_forge_key(forge, patch_property);
    lv2_atom_forge_urid(forge, xlv2_model);
    lv2_atom_forge_key(forge, patch_value);
    lv2_atom_forge_path(forge, filename, strlen(filename) + 1);

    lv2_atom_forge_pop(forge, &frame);
    return set;
}

// read atom message with file path
inline const LV2_Atom* Ximpulseloader::read_set_file(const LV2_Atom_Object* obj) {
    if (obj->body.otype != patch_Set) {
        return NULL;
    }

    const LV2_Atom* property = NULL;
    lv2_atom_object_get(obj, patch_property, &property, 0);

    if (property && (property->type == atom_URID)) {
        if (((LV2_Atom_URID*)property)->body == xlv2_ir_file)
            engine._cd.store(1, std::memory_order_release);
        else return NULL;
    }

    const LV2_Atom* file_path = NULL;
    lv2_atom_object_get(obj, patch_value, &file_path, 0);
    if (!file_path || (file_path->type != atom_Path)) {
        return NULL;
    }

    return file_path;
}

// read all incoming atom messages
inline void Ximpulseloader::check_messages(uint32_t n_samples)
{
    if(n_samples<1) return;
    const uint32_t notify_capacity = this->notify->atom.size;
    lv2_atom_forge_set_buffer(&forge, (uint8_t*)notify, notify_capacity);
    lv2_atom_forge_sequence_head(&forge, &notify_frame, 0);

    engine.bufsize = n_samples;

    LV2_ATOM_SEQUENCE_FOREACH(control, ev) {
        if (lv2_atom_forge_is_object_type(&forge, ev->body.type)) {
            const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == patch_Get) {
                if (engine.ir_file != "None")
                    write_set_file(&forge, xlv2_ir_file, engine.ir_file.data());
           } else if (obj->body.otype == patch_Set) {
                const LV2_Atom* file_path = read_set_file(obj);
                if (file_path) {
                    if (engine._cd.load(std::memory_order_acquire) == 1)
                        engine.ir_file = (const char*)(file_path+1);
                    if (!doit) doit = true;
                }
            }
        }
    }

    // fetch parameters from host
    engine.bypass = static_cast<uint32_t>(*_bypass);
    engine.plugin1->gain = static_cast<float>(*_gain);
    engine.plugin2->dry_wet = static_cast<float>(*_wet_dry);

    // check if normalisation is pressed for conv
    if (engine.normA != static_cast<uint32_t>(*(_normA))) {
        engine.normA = static_cast<uint32_t>(*(_normA));
        engine._cd.fetch_add(1, std::memory_order_relaxed);
        engine.conv.set_normalisation(engine.normA);
        if (engine.ir_file.compare("None") != 0) {
            if (!doit) doit = true;
        }
    }

    // check if a model or IR file is to be removed
 /*   if ((*_eraseIr)) {
        engine._cd.fetch_add(1, std::memory_order_relaxed);
        engine.ir_file = "None";
        if (!doit) doit = true;
        (*_eraseIr) = 0.0;
    }*/

    if (_restore.load(std::memory_order_acquire)) {
        if (!doit) doit = true;
        _restore.store(false, std::memory_order_release);
    }

    // run worker thread when needed
    if (doit && !engine._execute.load(std::memory_order_acquire)) {
        engine._execute.store(true, std::memory_order_release);
        engine.xrworker.runProcess();
        doit = false;
    } 
    // notify UI on changed model files
    if (engine._notify_ui.load(std::memory_order_acquire)) {
        engine._notify_ui.store(false, std::memory_order_release);

        write_set_file(&forge, xlv2_ir_file, engine.ir_file.data());
        engine._cd.store(0, std::memory_order_release);
    }
}

inline void Ximpulseloader::runBufferedDsp(uint32_t n_samples)
{
    // nothing to do for zero samples
    if(n_samples<1) return;

    // copy input to output when they are not the same buffers
    // doing in place processing
    if(output0 != input0)
        memcpy(output0, input0, n_samples*sizeof(float));

    // the early bird die
    if (processCounter < 5) {
        processCounter++;
        return;
    }
    // check atom messages (full cycle)
    check_messages(n_samples);
    // run engine
    engine.process(n_samples, output0, output0);
}

void Ximpulseloader::connect_all__ports(uint32_t port, void* data)
{
    // connect the Ports used by the plug-in class
    connect_(port,data);
}

// write file path to state
inline void Ximpulseloader::storeFile(LV2_State_Store_Function store,
            LV2_State_Handle handle, const LV2_URID urid, const std::string file) {

    store(handle, urid, file.data(), strlen(file.data()) + 1,
          atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

// retrieve file path from state
inline bool Ximpulseloader::restoreFile(LV2_State_Retrieve_Function retrieve,
                LV2_State_Handle handle, const LV2_URID urid, std::string *file) {

    size_t      size;
    uint32_t    type;
    uint32_t    fflags;
    const void* name = retrieve(handle, urid, &size, &type, &fflags);
    if (name) {
        *file = (const char*)(name);
        return (!(*file).empty() && ((*file) != "None"));
    }
    return false;
}

////////////////////// STATIC CLASS  FUNCTIONS  ////////////////////////

LV2_State_Status Ximpulseloader::save_state(LV2_Handle instance,
                                     LV2_State_Store_Function store,
                                     LV2_State_Handle handle, uint32_t flags,
                                     const LV2_Feature* const* features) {

    Ximpulseloader* self = static_cast<Ximpulseloader*>(instance);

    self->storeFile(store, handle, self->xlv2_ir_file, self->engine.ir_file);

    return LV2_STATE_SUCCESS;
}

LV2_State_Status Ximpulseloader::restore_state(LV2_Handle instance,
                                        LV2_State_Retrieve_Function retrieve,
                                        LV2_State_Handle handle, uint32_t flags,
                                        const LV2_Feature* const*   features) {

    Ximpulseloader* self = static_cast<Ximpulseloader*>(instance);

    if (self->restoreFile(retrieve, handle, self->xlv2_ir_file, &self->engine.ir_file))
        self->engine._cd.fetch_add(1, std::memory_order_relaxed);

    self-> _restore.store(true, std::memory_order_release);
    return LV2_STATE_SUCCESS;
}

LV2_Handle 
Ximpulseloader::instantiate(const LV2_Descriptor* descriptor,
                            double rate, const char* bundle_path,
                            const LV2_Feature* const* features)
{
    // init the plug-in class
    Ximpulseloader *self = new Ximpulseloader();
    if (!self) {
        return NULL;
    }

    const LV2_Options_Option* options  = NULL;
    uint32_t bufsize = 0;

    for (int32_t i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->schedule = (LV2_Worker_Schedule*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_OPTIONS__options)) {
            options = (const LV2_Options_Option*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            self->log = (LV2_Log_Log*)features[i]->data;
        }
    }

    if (!self->map) {
        fprintf(stderr,"Missing required feature uri:map.\n EXIT here!!\n");
        cleanup((LV2_Handle)self);
        return nullptr;
    } else {
        self->map_uris(self->map);
    }

    if (self->log) {
        lv2_log_logger_init(&self->logger, self->map, self->log);
    }

    if (!self->schedule) {
        lv2_log_error(&self->logger, "Missing feature work:schedule.\n");
    }

    if (!options) {
        lv2_log_error(&self->logger, "Missing feature options.\n");
    }
    else {
        LV2_URID bufsz_max = self->map->map(self->map->handle, LV2_BUF_SIZE__maxBlockLength);
        LV2_URID bufsz_    = self->map->map(self->map->handle,"http://lv2plug.in/ns/ext/buf-size#nominalBlockLength");
        LV2_URID atom_Int = self->map->map(self->map->handle, LV2_ATOM__Int);
        LV2_URID tshed_pol = self->map->map (self->map->handle, "http://ardour.org/lv2/threads/#schedPolicy");
        LV2_URID tshed_pri = self->map->map (self->map->handle, "http://ardour.org/lv2/threads/#schedPriority");

        for (const LV2_Options_Option* o = options; o->key; ++o) {
            if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_ && o->type == atom_Int) {
                bufsize = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_max && o->type == atom_Int) {
                if (!bufsize)
                    bufsize = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
                o->key == tshed_pol && o->type == atom_Int) {
                self->rt_policy = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
                o->key == tshed_pri && o->type == atom_Int) {
                self->rt_prio = *(const int32_t*)o->value;
            }
        }

        if (bufsize == 0) {
            lv2_log_error(&self->logger, "No maximum buffer size given.\n");
        } else {
            self->engine.bufsize = bufsize;
            lv2_log_note(&self->logger, "using block size: %d\n", bufsize);
        }
    }

    self->map_uris(self->map);
    lv2_atom_forge_init(&self->forge, self->map);
    self->init_dsp_((uint32_t)rate);

    return (LV2_Handle)self;
}

void Ximpulseloader::connect_port(LV2_Handle instance, 
                                   uint32_t port, void* data)
{
    // connect all ports
    static_cast<Ximpulseloader*>(instance)->connect_all__ports(port, data);
}

void Ximpulseloader::activate(LV2_Handle instance)
{
    // allocate needed mem
    static_cast<Ximpulseloader*>(instance)->activate_f();
}

void Ximpulseloader::run(LV2_Handle instance, uint32_t n_samples)
{
    // run dsp
    static_cast<Ximpulseloader*>(instance)->runBufferedDsp(n_samples);
}

void Ximpulseloader::deactivate(LV2_Handle instance)
{
    // free allocated mem
    static_cast<Ximpulseloader*>(instance)->deactivate_f();
}

void Ximpulseloader::cleanup(LV2_Handle instance)
{
    // well, clean up after us
    Ximpulseloader* self = static_cast<Ximpulseloader*>(instance);
    self->clean_up();
    delete self;
}

LV2_Worker_Status Ximpulseloader::work(LV2_Handle instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
  static_cast<Ximpulseloader*>(instance)->engine.do_work_mono();
  return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status Ximpulseloader::work_response(LV2_Handle instance,
              uint32_t    size,
              const void* data)
{
  //printf("worker respose.\n");
  return LV2_WORKER_SUCCESS;
}

const void* Ximpulseloader::extension_data(const char* uri)
{
    static const LV2_Worker_Interface worker = { work, work_response, NULL };
    static const LV2_State_Interface  state  = { save_state, restore_state };

    if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    else if (!strcmp(uri, LV2_STATE__interface)) {
        return &state;
    }

    return NULL;
}

const LV2_Descriptor Ximpulseloader::descriptor =
{
    PLUGIN_URI ,
    Ximpulseloader::instantiate,
    Ximpulseloader::connect_port,
    Ximpulseloader::activate,
    Ximpulseloader::run,
    Ximpulseloader::deactivate,
    Ximpulseloader::cleanup,
    Ximpulseloader::extension_data
};

} // end namespace impulseloader

////////////////////////// LV2 SYMBOL EXPORT ///////////////////////////

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index)
    {
        case 0:
            return &impulseloader::Ximpulseloader::descriptor;
        default:
            return NULL;
    }
}

///////////////////////////// FIN //////////////////////////////////////
