cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(dla C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Dark Lands")
set(VITA_TITLEID "DLA000000")
set(VITA_VPKNAME "dla")
set(VITA_VERSION "00.50")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.178.58" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/dla/" CACHE STRING "Path to data (with trailing /)")
set(SO_PATH "${DATA_PATH}libcocos2dcpp.so" CACHE STRING "Path to .so")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

option(ENABLE_MSAA_4X "Enable 4x MSAA. Disable for faster Vita release rendering." OFF)
if (ENABLE_MSAA_4X)
  add_definitions(-DDLA_GXM_MULTISAMPLE_MODE=SCE_GXM_MULTISAMPLE_4X)
else()
  add_definitions(-DDLA_GXM_MULTISAMPLE_MODE=SCE_GXM_MULTISAMPLE_NONE)
endif()

option(AUDIO_WARMUP_EFFECTS "Warm common gameplay effects to reduce first-use stutter" ON)
option(AUDIO_WARMUP_ASYNC "Warm gameplay effects on a paced background worker instead of blocking startup" ON)
option(AUDIO_SYNC_PRELOAD_EFFECTS "Decode selected preloadEffect requests synchronously during loading" OFF)
set(AUDIO_EFFECT_CACHE_LIMIT "32" CACHE STRING "Maximum decoded sound effects kept in the Vita audio cache")
set(AUDIO_WARMUP_MAX_EFFECTS "24" CACHE STRING "Maximum common sound effects decoded before first gameplay use")
set(AUDIO_WARMUP_START_DELAY_US "350000" CACHE STRING "Delay before async audio warmup starts")
set(AUDIO_WARMUP_STEP_DELAY_US "60000" CACHE STRING "Delay between async audio warmup decode attempts")
if (AUDIO_WARMUP_EFFECTS)
  add_definitions(-DDLA_AUDIO_WARMUP_EFFECTS=1)
else()
  add_definitions(-DDLA_AUDIO_WARMUP_EFFECTS=0)
endif()
if (AUDIO_WARMUP_ASYNC)
  add_definitions(-DDLA_AUDIO_WARMUP_ASYNC=1)
else()
  add_definitions(-DDLA_AUDIO_WARMUP_ASYNC=0)
endif()
if (AUDIO_SYNC_PRELOAD_EFFECTS)
  add_definitions(-DDLA_AUDIO_SYNC_PRELOAD_EFFECTS=1)
else()
  add_definitions(-DDLA_AUDIO_SYNC_PRELOAD_EFFECTS=0)
endif()
add_definitions(-DDLA_AUDIO_EFFECT_CACHE_LIMIT=${AUDIO_EFFECT_CACHE_LIMIT})
add_definitions(-DDLA_AUDIO_WARMUP_MAX_EFFECTS=${AUDIO_WARMUP_MAX_EFFECTS})
add_definitions(-DDLA_AUDIO_WARMUP_START_DELAY_US=${AUDIO_WARMUP_START_DELAY_US})
add_definitions(-DDLA_AUDIO_WARMUP_STEP_DELAY_US=${AUDIO_WARMUP_STEP_DELAY_US})

set(SHADER_FORMAT "GLSL" CACHE STRING "Preferred shader format (one of 'GLSL', 'CG', 'GXP')")
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" ON)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Compile-time logging switch:
#   cmake -S . -B build -DDEBUG=1   -> verbose Vita/loader/game-save logs
#   cmake -S . -B build -DDEBUG=0   -> release logs compiled out
set(DEBUG "0" CACHE STRING "Enable verbose debug/runtime logging (0=release, 1=debug)")
set_property(CACHE DEBUG PROPERTY STRINGS 0 1)

if (DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG=1)
  add_definitions(-DDLA_DEBUG_LOGS=1)
  add_definitions(-DDEBUG_SOLOADER)
  add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
  add_definitions(-DDEBUG_OPENGL)
  add_definitions(-DSO_DEBUG_INIT=1)
  add_definitions(-DSO_DEBUG_IMPORTS=1)
else()
  add_definitions(-DDLA_DEBUG_LOGS=0)
  add_definitions(-DFALSOJNI_DEBUGLEVEL=4)
  add_definitions(-DSO_DEBUG_INIT=0)
  add_definitions(-DSO_DEBUG_IMPORTS=0)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated -fno-optimize-sibling-calls")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif()

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/dynlib.c
               source/java.c
               source/patch.c
               source/reimpl/bits/_ctype.c
               source/reimpl/controls.c
               source/reimpl/controls.h
               source/reimpl/egl.c
               source/reimpl/errno.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/pthr.c
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/dialog.c
               source/utils/glutil.c
               source/utils/init.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/utils.c
               source/SharedPreferences.cpp
               source/audio.cpp
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/falso_jni/converter.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c)

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      OpenSLES
                      sndfile
                      vorbis
                      ogg
                      vorbisenc
                      vorbisfile
                      FLAC
                      soloud
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CMAKE_PROJECT_NAME}> /home/mint/Desktop/vita-parse-core/${CMAKE_PROJECT_NAME}.elf
    COMMENT "Copying raw ELF for parsing output"
)

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/urandom urandom
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                )

# Keep the installable VPK in the repository root synchronized with the file
# produced in the build directory. This avoids accidentally testing an older
# eboot (the previous root VPK predated the Dark Lands save implementation).
add_custom_command(TARGET ${VITA_VPKNAME}.vpk-vpk POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                           ${CMAKE_BINARY_DIR}/${VITA_VPKNAME}.vpk
                           ${CMAKE_SOURCE_DIR}/${VITA_VPKNAME}.vpk
                   COMMENT "Updating ${CMAKE_SOURCE_DIR}/${VITA_VPKNAME}.vpk"
                   )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
