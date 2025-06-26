/*
 * engine.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2025 brummer <brummer@web.de>
 */


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

#ifdef __SSE__
 #include <immintrin.h>
 #ifndef _IMMINTRIN_H_INCLUDED
  #include <fxsrintrin.h>
 #endif
 #ifdef __SSE3__
  #ifndef _PMMINTRIN_H_INCLUDED
   #include <pmmintrin.h>
  #endif
 #else
  #ifndef _XMMINTRIN_H_INCLUDED
   #include <xmmintrin.h>
  #endif
 #endif //__SSE3__
#endif //__SSE__

#include "dry_wet.cc"
#include "gain.cc"

#include "fftconvolver.h"

#pragma once

#ifndef ENGINE_H_
#define ENGINE_H_
namespace impulseloader {

/////////////////////////// DENORMAL PROTECTION   //////////////////////

class DenormalProtection {
private:
#ifdef USE_SSE
    uint32_t  mxcsr_mask;
    uint32_t  mxcsr;
    uint32_t  old_mxcsr;
#endif

public:
    inline void set_() {
#ifdef USE_SSE
        old_mxcsr = _mm_getcsr();
        mxcsr = old_mxcsr;
        _mm_setcsr((mxcsr | _MM_DENORMALS_ZERO_MASK | _MM_FLUSH_ZERO_MASK) & mxcsr_mask);
#endif
    };
    inline void reset_() {
#ifdef USE_SSE
        _mm_setcsr(old_mxcsr);
#endif
    };

    inline DenormalProtection() {
#ifdef USE_SSE
        mxcsr_mask = 0xffbf; // Default MXCSR mask
        mxcsr      = 0;
        uint8_t fxsave[512] __attribute__ ((aligned (16))); // Structure for storing FPU state with FXSAVE command

        memset(fxsave, 0, sizeof(fxsave));
        __builtin_ia32_fxsave(&fxsave);
        uint32_t mask = *(reinterpret_cast<uint32_t *>(&fxsave[0x1c])); // Obtain the MXCSR mask from FXSAVE structure
        if (mask != 0)
            mxcsr_mask = mask;
#endif
    };

    inline ~DenormalProtection() {};
};

class Engine
{
public:
    ParallelThread               xrworker;
    ConvolverSelector            conv;
    gain::Dsp*                   plugin1;
    wet_dry::Dsp*                plugin2;

    int32_t                      rt_prio;
    int32_t                      rt_policy;
    uint32_t                     s_rate;
    uint32_t                     bypass;
    uint32_t                     bufsize;
    uint32_t                     normA;

    std::string                  ir_file;

    std::atomic<bool>            _execute;
    std::atomic<bool>            _notify_ui;
    std::atomic<int>             _cd;

    inline Engine();
    inline ~Engine();

    inline void init(uint32_t rate, int32_t rt_prio_, int32_t rt_policy_);
    inline void clean_up();
    inline void do_work_mono();
    inline void process(uint32_t n_samples, float* output0, float* output1);

private:
    DenormalProtection           MXCSR;
    std::condition_variable      Sync;
    std::mutex                   WMutex;

    inline void setIRFile(ConvolverSelector *co, std::string *file);
};

inline Engine::Engine() :
    xrworker(), 
    plugin1(gain::plugin()),
    plugin2(wet_dry::plugin()) {
        bypass = 0;
        bufsize = 0;
        normA = 0;
        ir_file = "None";
        xrworker.start();
};

inline Engine::~Engine(){
    xrworker.stop();
    conv.stop_process();
    conv.cleanup();
    plugin1->del_instance(plugin1);
    plugin2->del_instance(plugin2);
};

inline void Engine::init(uint32_t rate, int32_t rt_prio_, int32_t rt_policy_) {
    s_rate = rate;
    plugin1->init(rate);
    plugin2->init(rate);

    rt_prio = rt_prio_;
    rt_policy = rt_policy_;

    _execute.store(false, std::memory_order_release);
    _notify_ui.store(false, std::memory_order_release);
    _cd.store(0, std::memory_order_release);

    xrworker.setThreadName("Worker");
    xrworker.set<Engine, &Engine::do_work_mono>(this);
};

void Engine::clean_up()
{
}

inline void Engine::setIRFile(ConvolverSelector *co, std::string *file) {
    if (co->is_runnable()) {
        co->set_not_runnable();
        co->stop_process();
        std::unique_lock<std::mutex> lk(WMutex);
        Sync.wait_for(lk, std::chrono::milliseconds(160));
    }

    co->cleanup();
    co->set_samplerate(s_rate);
    co->set_buffersize(bufsize);

    if (*file != "None") {
        co->configure(*file, 1.0, 0, 0, 0, 0, 0);
        while (!co->checkstate());
        if(!co->start(rt_prio, rt_policy)) {
            *file = "None";
           // lv2_log_error(&logger,"impulse convolver update fail\n");
        }
    }
}

void Engine::do_work_mono() {
    // set ir files
    if (_cd.load(std::memory_order_acquire) == 1) {
        setIRFile(&conv, &ir_file);
    }
    // set flag that work is done ready
    _execute.store(false, std::memory_order_release);
    // set flag that GUI need information about changed state
    _notify_ui.store(true, std::memory_order_release);
}

inline void Engine::process(uint32_t n_samples, float* input0, float* output0) {
    if(n_samples<1) return;

    // basic bypass
    if (!bypass) {
        Sync.notify_all();
        return;
    }
    // do inplace processing on default
    if(output0 != input0)
        memcpy(output0, input0, n_samples*sizeof(float));

    bufsize = n_samples;
    float buf0[n_samples];
    memcpy(buf0, input0, n_samples*sizeof(float));

    MXCSR.set_();

    // process conv
    plugin1->compute(n_samples, output0, output0);
    if (!_execute.load(std::memory_order_acquire) && conv.is_runnable())
        conv.compute(n_samples, output0, output0);
    plugin2->compute(n_samples, buf0, output0, output0);
    
   
    // notify neural modeller that process cycle is done
    Sync.notify_all();
    MXCSR.reset_();

}

}; // end namespace neuralrack
#endif
