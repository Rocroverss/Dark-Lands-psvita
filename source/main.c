#include "utils/init.h"
#include "debug_log.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <psp2/kernel/threadmgr.h>
#include <stdarg.h>
#include <stdint.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include <so_util/so_util.h>

#include <pthread.h>
#include "reimpl/asset_manager.h"
#include "reimpl/controls.h"
#include "SharedPreferences.h"
#include "game_prefs.h"
#include "audio.h"
#include <string.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>

// Vita game partition is ~342 MB total.  Budget breakdown:
//   SO (libcocos2dcpp.so mapped):   ~17 MB
//   Kernel / system overhead:       ~30 MB
//   newlib heap (loader C code):    ~32 MB   <- _newlib_heap_size_user
//   SceLibc heap (C++ operator new):~96 MB   <- sceLibcHeapSize
//   GXM / GPU buffers, stack, etc.: ~20 MB
//   Headroom:                       ~147 MB
// Total committed: ~195 MB -- safely under the 342 MB ceiling.
//
// The newlib heap only serves our loader's own malloc() calls (so_util,
// asset_manager init, FalsoJNI, etc.) and does NOT need to be large.
// Cocos2d-x C++ operator new / STL containers all go through SceLibc.
int _newlib_heap_size_user = 32 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
// 96 MB for the Cocos2d-x C++ runtime (operator new, std::map/vector/string,
// FlatBuffers, animation cache, texture descriptors, etc.).
// If you still see OOM during gameplay, raise this in 16 MB increments,
// keeping _newlib_heap_size_user + sceLibcHeapSize + SO + overhead < 300 MB.
int sceLibcHeapSize = 96 * 1024 * 1024;
#endif

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

so_module so_mod;

void (*Cocos2dx_nativeTouchesBegin)(JNIEnv *jni, jobject thiz, jint id, jfloat x, jfloat y);
void (*Cocos2dx_nativeTouchesMove)(JNIEnv *jni, jobject thiz, jintArray ids, jfloatArray xs, jfloatArray ys);
void (*Cocos2dx_nativeTouchesEnd)(JNIEnv *jni, jobject thiz, jint id, jfloat x, jfloat y);
int (*Cocos2dx_nativeKeyDown)(JNIEnv *jni, jobject thiz, jint keyCode);

JavaDynArray *touch_ids, *touch_xs, *touch_ys;

// Defined in source/java.c and used by FalsoJNI's resilient classloader path.
extern uintptr_t g_fakeClassLoader;

// ---------------- JNI STUBS ----------------
#ifdef __cplusplus
extern "C" {
#endif

static void logjni(const char *fmt, ...)
{
#if DLA_DEBUG_LOGS
    va_list ap;
    va_start(ap, fmt);
    sceClibVprintf(fmt, ap);
    va_end(ap);
#else
    (void)fmt;
#endif
}

// forward strings
static const char *jstring_to_cstr(JNIEnv *env, jstring js)
{
    if (!js || !env) return NULL;
    const char *s = jni->GetStringUTFChars(env, js, 0);
    if (!s) return NULL;
    static char buf[1024];
    strncpy(buf, s, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    jni->ReleaseStringUTFChars(env, js, s);
    return buf;
}

// Touch & key events now forward to your handlers
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(JNIEnv *env, jclass cls, jint id, jfloat x, jfloat y)
{
    controls_handler_touch(id, x, y, CONTROLS_ACTION_DOWN);
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove(JNIEnv *env, jclass cls, jintArray ids, jfloatArray xs, jfloatArray ys)
{
    if (!ids || !xs || !ys) return;
    jint id0; jfloat x0, y0;
    jni->GetIntArrayRegion(env, ids, 0, 1, &id0);
    jni->GetFloatArrayRegion(env, xs, 0, 1, &x0);
    jni->GetFloatArrayRegion(env, ys, 0, 1, &y0);
    controls_handler_touch(id0, x0, y0, CONTROLS_ACTION_MOVE);
}

JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(JNIEnv *env, jclass cls, jint id, jfloat x, jfloat y)
{
    controls_handler_touch(id, x, y, CONTROLS_ACTION_UP);
}

JNIEXPORT int JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown(JNIEnv *env, jclass cls, jint keyCode)
{
    controls_handler_key(keyCode, CONTROLS_ACTION_DOWN);
    return 0;
}

// Generic stubs for Java symbols expected by the game
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext(JNIEnv *env, jclass cls, jobject ctx, jobject assetMgr) { (void)env; (void)cls; (void)ctx; (void)assetMgr; }
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath(JNIEnv *env, jclass cls, jstring apkPath) { (void)env; (void)cls; (void)apkPath; }
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText(JNIEnv *env, jclass cls, jstring text) { (void)env; (void)cls; (void)text; }
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeDeleteBackward(JNIEnv *env, jclass cls) { (void)env; (void)cls; }
JNIEXPORT jstring JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeGetContentText(JNIEnv *env, jclass cls) { return jni->NewStringUTF(env,""); }
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnSurfaceChanged(JNIEnv *env, jclass cls, jint w, jint h) { (void)env; (void)cls; (void)w; (void)h; }
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    DLA_DEBUG_PRINTF("[SAVE] Cocos2dxRenderer.nativeOnPause -> flushing saves\n");
    prefs_flush_with_reason("Cocos2dxRenderer.nativeOnPause");
    game_prefs_flush("Cocos2dxRenderer.nativeOnPause");
}
JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume(JNIEnv *env, jclass cls) { (void)env; (void)cls; }
JNIEXPORT void JNICALL Java_bulkypix_darklands_DarkLands_OnResumeApp(JNIEnv *env, jclass cls) { (void)env; (void)cls; }

// Simple stubs for services
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_PurchaseSucceed(JNIEnv *env, jclass cls, jstring sku) { (void)env; (void)cls; (void)sku; }
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_AddNonConsumable(JNIEnv *env, jclass cls, jstring sku) { (void)env; (void)cls; (void)sku; }
JNIEXPORT jboolean JNICALL Java_minglegames_platform_InAppPurchaseService_IsDoubleCoinsPurchased(JNIEnv *env, jclass cls) { return 0; }
JNIEXPORT jboolean JNICALL Java_minglegames_platform_InAppPurchaseService_IsConsumable(JNIEnv *env, jclass cls, jstring item) { return 0; }
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_AddItem(JNIEnv *env, jclass cls, jstring item, jstring price) { (void)env; (void)cls; (void)item; (void)price; }
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_PurchaseFailed(JNIEnv *env, jclass cls, jint code) { (void)env; (void)cls; (void)code; }
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_QueryInventoryError(JNIEnv *env, jclass cls, jint code) { (void)env; (void)cls; (void)code; }
JNIEXPORT void JNICALL Java_minglegames_platform_InAppPurchaseService_GetPublicKeyForThumzap(JNIEnv *env, jclass cls) { (void)env; (void)cls; }

JNIEXPORT void JNICALL Java_com_fingersoft_game_MainActivity_setInAppItemPrice(JNIEnv *env, jclass cls, jstring item, jstring price) { (void)env; (void)cls; (void)item; (void)price; }
JNIEXPORT jint JNICALL Java_com_fingersoft_game_MainActivity_isTestingMode(JNIEnv *env, jclass cls) { return 0; }

#ifdef __cplusplus
}
#endif
// ---------------- END JNI STUBS ----------------

// ---------------- Controls ----------------
enum {
    VITA_TOUCH_STOP = 100,
    VITA_TOUCH_ATTACK,
    VITA_TOUCH_BLOCK,
    VITA_TOUCH_SWIPE_UP,
    VITA_TOUCH_SWIPE_DOWN,
};

static void controls_tap(int32_t id, float x, float y)
{
    controls_handler_touch(id, x, y, CONTROLS_ACTION_DOWN);
    controls_handler_touch(id, x, y, CONTROLS_ACTION_UP);
}

static void controls_swipe(int32_t id, float start_y, float end_y)
{
    const float x = SCREEN_WIDTH * 0.5f;
    const float middle_y = (start_y + end_y) * 0.5f;

    controls_handler_touch(id, x, start_y, CONTROLS_ACTION_DOWN);
    controls_handler_touch(id, x, middle_y, CONTROLS_ACTION_MOVE);
    controls_handler_touch(id, x, end_y, CONTROLS_ACTION_UP);
}

void controls_handler_key(int32_t keycode, ControlsAction action)
{
    switch (keycode)
    {
    case AKEYCODE_DPAD_UP:
        if (action == CONTROLS_ACTION_DOWN)
            controls_swipe(VITA_TOUCH_SWIPE_UP, 408.0f, 136.0f);
        return;
    case AKEYCODE_DPAD_DOWN:
        if (action == CONTROLS_ACTION_DOWN)
            controls_swipe(VITA_TOUCH_SWIPE_DOWN, 136.0f, 408.0f);
        return;
    case AKEYCODE_DPAD_CENTER:
        if (action == CONTROLS_ACTION_DOWN)
            controls_tap(VITA_TOUCH_STOP, 480.0f, 272.0f);
        return;
    case AKEYCODE_A:
        controls_handler_touch(VITA_TOUCH_ATTACK, 817.0f, 410.0f, action);
        return;
    case AKEYCODE_BUTTON_X:
        controls_handler_touch(VITA_TOUCH_BLOCK, 156.0f, 410.0f, action);
        return;
    case AKEYCODE_BUTTON_L1:
        controls_handler_touch(69, 156.0, 410.0, action);
        return;
    case AKEYCODE_BUTTON_R1:
        controls_handler_touch(420, 817.0, 410.0, action);
        return;
    }
    if (action == CONTROLS_ACTION_DOWN && Cocos2dx_nativeKeyDown)
        Cocos2dx_nativeKeyDown(&jni, NULL, keycode);
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action)
{
    const jint android_id = id + 8;
    static int touch_log_count = 0;

    if (!touch_ids || !touch_xs || !touch_ys)
        return;

    ((int *)touch_ids->array)[0] = android_id;
    ((float *)touch_xs->array)[0] = x;
    ((float *)touch_ys->array)[0] = y;

    if (DLA_DEBUG_LOGS && touch_log_count < 32) {
        const char *name = action == CONTROLS_ACTION_DOWN ? "down" :
                           action == CONTROLS_ACTION_UP ? "up" : "move";
        DLA_DEBUG_PRINTF("[touch] %s id=%d android=%d x=%.1f y=%.1f\n",
                         name, id, android_id, x, y);
        touch_log_count++;
    }

    switch (action)
    {
    case CONTROLS_ACTION_DOWN:
        if(Cocos2dx_nativeTouchesBegin) Cocos2dx_nativeTouchesBegin(&jni, NULL, android_id, x, y);
        break;
    case CONTROLS_ACTION_UP:
        if(Cocos2dx_nativeTouchesEnd) Cocos2dx_nativeTouchesEnd(&jni, NULL, android_id, x, y);
        break;
    case CONTROLS_ACTION_MOVE:
        if(Cocos2dx_nativeTouchesMove) Cocos2dx_nativeTouchesMove(&jni, NULL, (jintArray)touch_ids, (jfloatArray)touch_xs, (jfloatArray)touch_ys);
        break;
    }
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action)
{
    (void)which; (void)x; (void)y; (void)action;
}

// ---------------- Main ----------------
static void poll_app_events_for_save(void)
{
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));

    while (sceAppUtilReceiveAppEvent(&eventParam) >= 0 && eventParam.type != 0) {
        DLA_DEBUG_PRINTF("[SAVE] app event type=0x%08x -> flushing saves\n", (unsigned)eventParam.type);
        prefs_flush_with_reason("Vita app event");
        game_prefs_flush("Vita app event");
        sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    }
}

int main()
{
    DLA_DEBUG_PRINTF("Dark lands 0.130\n");
    DLA_DEBUG_PRINTF("1. soloader_init_all();\n");
    soloader_init_all();

    DLA_DEBUG_PRINTF("audio_init();\n");
    audio_init();
    DLA_DEBUG_PRINTF("gl_init();\n");
    gl_init();
    DLA_DEBUG_PRINTF("controls_init();\n");
    controls_init();

    touch_ids = jda_alloc(1, FIELD_TYPE_INT);
    touch_xs = jda_alloc(1, FIELD_TYPE_FLOAT);
    touch_ys = jda_alloc(1, FIELD_TYPE_FLOAT);



    int (*JNI_OnLoad)(void *vm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    void (*Cocos2dx_nativeSetContext)(void *env, void *obj, jobject context, jobject assetManager) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext");
    void (*Cocos2dx_nativeSetPaths)(void *env, void *obj, jstring apkFilePath) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths");
    void (*Cocos2dx_nativeSetApkPath)(void *env, void *obj, jstring apkFilePath) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
    int (*Game_isTestingMode)() = (void *)so_symbol(&so_mod, "Java_com_fingersoft_game_MainActivity_isTestingMode");
    int (*Game_nativeInit)(void *env, void *obj, jint screen_width, jint screen_height) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    int (*Game_nativeResize)(void *env, void *obj, jint screen_width, jint screen_height) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeResize");
    void (*Game_nativeOnSurfaceChanged)(void *env, void *obj, jint screen_width, jint screen_height) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnSurfaceChanged");
    int (*Game_nativeRender)(void *env) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");

    

    Cocos2dx_nativeTouchesBegin = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    Cocos2dx_nativeTouchesMove = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    Cocos2dx_nativeTouchesEnd = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    Cocos2dx_nativeKeyDown = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    DLA_DEBUG_PRINTF("Touch handlers begin=%p move=%p end=%p keyDown=%p\n",
                     Cocos2dx_nativeTouchesBegin, Cocos2dx_nativeTouchesMove,
                     Cocos2dx_nativeTouchesEnd, Cocos2dx_nativeKeyDown);

    if (!JNI_OnLoad) {
        DLA_ERROR_PRINTF("Error: JNI_OnLoad not found in module!\n");
        return -1;
    }
    if (!Game_nativeInit) {
        DLA_ERROR_PRINTF("Error: Game_nativeInit not found in module!\n");
        return -1;
    }
    if (!Game_nativeRender) {
        DLA_ERROR_PRINTF("Error: Game_nativeRender not found in module!\n");
        return -1;
    }
    DLA_DEBUG_PRINTF("JNI_OnLoad found at %p\n", JNI_OnLoad);
    DLA_DEBUG_PRINTF("Game_nativeInit=%p nativeResize=%p nativeOnSurfaceChanged=%p nativeRender=%p\n",
                     Game_nativeInit, Game_nativeResize, Game_nativeOnSurfaceChanged, Game_nativeRender);
    JNI_OnLoad(&jvm);
    DLA_DEBUG_PRINTF("game_prefs_init();\n");
    game_prefs_init();
    DLA_DEBUG_PRINTF("prefs_init();\n");
    prefs_init();
    {
        const char *apk_path = DATA_PATH "base.apk";
        jstring j_apk_path = NULL;
        int bootstrapped_paths = 0;
        int bootstrapped_context = 0;

        if (Cocos2dx_nativeSetContext) {
            // Dark Lands expects cocos2d-x JniHelper to cache a classloader via
            // nativeSetContext(). Passing our stable fake object is enough for
            // FalsoJNI to satisfy getClassLoader/loadClass lookups.
            DLA_DEBUG_PRINTF("Cocos2dx_nativeSetContext(fakeContext=%p, assetManager=NULL)\n", (void *)g_fakeClassLoader);
            Cocos2dx_nativeSetContext(&jni, NULL, (jobject)g_fakeClassLoader, NULL);
            bootstrapped_context = 1;
        }

        if (!file_exists(apk_path)) {
            DLA_DEBUG_PRINTF("[main][WARN] %s not found; passing expected apk path to native path bootstrap anyway\n", apk_path);
        }
        asset_manager_set_apk_path(apk_path);

        j_apk_path = jni->NewStringUTF(&jni, apk_path);
        if (!j_apk_path) {
            DLA_DEBUG_PRINTF("[main][WARN] failed to allocate apk path jstring; continuing without native path bootstrap\n");
        } else {
            if (Cocos2dx_nativeSetApkPath) {
                // Dark Lands later aborts in libc++ allocation/guard code with
                // nativeSetApkPath still present in the crash-side stack state.
                // Our loader already provides the APK path to the asset layer,
                // so avoid touching the game's internal APK-path bootstrap.
                DLA_DEBUG_PRINTF("[main][INFO] skipping Cocos2dx_nativeSetApkPath(%s); loader asset bootstrap owns base.apk\n", apk_path);
                bootstrapped_paths = 1;
            }
            if (Cocos2dx_nativeSetPaths) {
                DLA_DEBUG_PRINTF("Cocos2dx_nativeSetPaths(%s)\n", apk_path);
                Cocos2dx_nativeSetPaths(&jni, NULL, j_apk_path);
                bootstrapped_paths = 1;
            }
            jni->DeleteLocalRef(&jni, j_apk_path);
        }

        if (!bootstrapped_paths) {
            DLA_DEBUG_PRINTF("[main][WARN] no native apk/path bootstrap export found; continuing without one\n");
        }
        if (!bootstrapped_context) {
            DLA_DEBUG_PRINTF("[main][WARN] Cocos2dx_nativeSetContext not exported; JniHelper will rely on fallback classloader resolution\n");
        }
    }
    DLA_DEBUG_PRINTF("Game_nativeInit\n");
    Game_nativeInit(&jni, NULL, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (Game_nativeOnSurfaceChanged) {
        DLA_DEBUG_PRINTF("Game_nativeOnSurfaceChanged\n");
        Game_nativeOnSurfaceChanged(&jni, NULL, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else if (Game_nativeResize) {
        DLA_DEBUG_PRINTF("Game_nativeResize\n");
        Game_nativeResize(&jni, NULL, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        DLA_DEBUG_PRINTF("[main][WARN] no native resize/surface callback exported; continuing without one\n");
    }

    while (1)
    {
        poll_app_events_for_save();
        controls_poll();

        Game_nativeRender(&jni);

        gl_swap();
    }

    audio_destroy();
    game_prefs_shutdown();
    prefs_destroy();

    return sceKernelExitDeleteThread(0);
}
