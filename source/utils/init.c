/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"
#include "debug_log.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

void soloader_init_all() {
    DLA_DEBUG_PRINTF("Starting soloader_init_all() \n");
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif
    DLA_DEBUG_PRINTF("Loading kubridge \n");
    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    DLA_DEBUG_PRINTF("checking if exists SO_PATH file: %s\n", SO_PATH);
    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }
    DLA_DEBUG_PRINTF("Loading SO_PATH file: %s\n", SO_PATH);
    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }

    DLA_DEBUG_PRINTF("Loading settings \n");
    settings_load();
    l_success("Settings loaded.");

    DLA_DEBUG_PRINTF("SO relocated \n");
    so_relocate(&so_mod);
    l_success("SO relocated.");

    DLA_DEBUG_PRINTF("SO imports resolved. \n");
    resolve_imports(&so_mod);
    l_success("SO imports resolved.");

    DLA_DEBUG_PRINTF("SO patched. \n");
    so_patch();
    l_success("SO patched.");

    DLA_DEBUG_PRINTF("SO caches flushed. \n");
    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    DLA_DEBUG_PRINTF("SO initialized. \n");
    so_initialize(&so_mod);
    l_success("SO initialized.");

    DLA_DEBUG_PRINTF("OpenGL preloaded. \n");
    gl_preload();
    l_success("OpenGL preloaded.");

    DLA_DEBUG_PRINTF("FalsoJNI initialized. \n");
    jni_init();
    l_success("FalsoJNI initialized.");
}
