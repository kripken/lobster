// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Engine integration with Lobster VM and frame management.

#include "lobster/stdafx.h"

#include "lobster/compiler.h"  // For RegisterBuiltin().
#ifdef __EMSCRIPTEN__
    #include "emscripten.h"
#endif

#include "lobster/sdlinterface.h"

#include "lobster/engine.h"

using namespace lobster;

void RegisterCoreEngineBuiltins(NativeRegistry &nfr) {
    RegisterCoreLanguageBuiltins(nfr);

    extern void AddGraphics(NativeRegistry &nfr); RegisterBuiltin(nfr, "graphics",  AddGraphics);
    extern void AddFont(NativeRegistry &nfr);     RegisterBuiltin(nfr, "font",      AddFont);
    extern void AddSound(NativeRegistry &nfr);    RegisterBuiltin(nfr, "sound",     AddSound);
    extern void AddPhysics(NativeRegistry &nfr);  RegisterBuiltin(nfr, "physics",   AddPhysics);
    extern void AddNoise(NativeRegistry &nfr);    RegisterBuiltin(nfr, "noise",     AddNoise);
    extern void AddMeshGen(NativeRegistry &nfr);  RegisterBuiltin(nfr, "meshgen",   AddMeshGen);
    extern void AddCubeGen(NativeRegistry &nfr);  RegisterBuiltin(nfr, "cubegen",   AddCubeGen);
    extern void AddOcTree(NativeRegistry &nfr);   RegisterBuiltin(nfr, "octree",    AddOcTree);
    extern void AddVR(NativeRegistry &nfr);       RegisterBuiltin(nfr, "vr",        AddVR);
    extern void AddSteam(NativeRegistry &nfr);    RegisterBuiltin(nfr, "steam",     AddSteam);
    extern void AddIMGUI(NativeRegistry &nfr);    RegisterBuiltin(nfr, "imgui",     AddIMGUI);
}

void EngineSuspendIfNeeded(StackPtr sp, VM &vm) {
    #ifdef USE_MAIN_LOOP_CALLBACK
        // Here we have to something hacky: emscripten requires us to not take over the main
        // loop. So we use this exception to suspend the VM right inside the gl_frame() call.
        // FIXME: do this at the start of the frame instead?
        vm.SuspendSP(sp);
        THROW_OR_ABORT(string("SUSPEND-VM-MAINLOOP"));
    #else
        (void)sp;
        (void)vm;
    #endif
}

void EngineExit(int code) {
    GraphicsShutDown();

    #ifdef __EMSCRIPTEN__
    emscripten_force_exit(code);
    #endif

    exit(code); // Needed at least on iOS to forcibly shut down the wrapper main()
}

void one_frame_callback(void *arg) {
    auto &vm = *(lobster::VM *)arg;
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        GraphicsFrameStart();
        vm.vml.LogFrame();
        vm.OneMoreFrame(vm.ResumeSP());
        // If this returns, we didn't hit a gl_frame() again and exited normally.
        EngineExit(0);
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        if (s != "SUSPEND-VM-MAINLOOP") {
            // An actual error.
            LOG_ERROR(s);
            EngineExit(1);
        }
    }
    #endif
}

void EngineRunByteCode(VMArgs &&vmargs) {
    lobster::VMAllocator vma(std::move(vmargs));
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        vma.vm->EvalProgram();
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        #ifdef USE_MAIN_LOOP_CALLBACK
        if (s == "SUSPEND-VM-MAINLOOP") {
            // emscripten requires that we don't control the main loop.
            // We just got to the start of the first frame inside gl_frame(), and the VM is suspended.
            // Install the one-frame callback:
            #ifdef __EMSCRIPTEN__
            // This has a better explanation of the last argument than the emscripten docs:
            // http://flohofwoe.blogspot.com/2013/09/emscripten-and-pnacl-app-entry-in.html
            // We're passing true as last argument, which means emscripten is not going to
            // return from this function until completely done, emulating behavior as if we
            // control the main loop. What it really does is throw a JS exception to escape from
            // C++ execution, leaving this main loop in a frozen state to later return to.
            emscripten_set_main_loop_arg(one_frame_callback, vma.vm, 0, true);
            // When we return here, we're done and exit normally.
            #else
            // Emulate this behavior so we can debug it.
            while (vma.vm->evalret == "") one_frame_callback(&vm);
            #endif
        } else
        #endif
        {
            // An actual error.
            THROW_OR_ABORT(s);
        }
    }
    #endif
}

extern "C" int EngineRunCompiledCodeMain(int argc, char *argv[], const void *entry_point,
                                         const void *bytecodefb, size_t static_size,
                                         const lobster::block_base_t *vtables) {
    #ifdef USE_EXCEPTION_HANDLING
    try
    #endif
    {
        NativeRegistry nfr;
        RegisterCoreEngineBuiltins(nfr);
        EngineRunByteCode(CompiledInit(argc, argv, entry_point, bytecodefb, static_size, vtables,
                                       SDLLoadFile, nfr));
    }
    #ifdef USE_EXCEPTION_HANDLING
    catch (string &s) {
        LOG_ERROR(s);
        EngineExit(1);
    }
    #endif
    EngineExit(0);
    return 0;
}
