#include <falso_jni/FalsoJNI.h>
#include "debug_log.h"
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <psp2/kernel/clib.h>
#include "SharedPreferences.h"
#include "audio.h"
#include "game_prefs.h"
#include "utils/utils.h"

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#define KV_FOPEN sceLibcBridge_fopen
#define KV_FREAD sceLibcBridge_fread
#define KV_FWRITE sceLibcBridge_fwrite
#define KV_FCLOSE sceLibcBridge_fclose
#define KV_FFLUSH sceLibcBridge_fflush
#else
#define KV_FOPEN fopen
#define KV_FREAD fread
#define KV_FWRITE fwrite
#define KV_FCLOSE fclose
#define KV_FFLUSH fflush
#endif

// --- Extern Declarations from FalsoJNI_ImplBridge.c ---
// These link to the functions we created in the other file
extern void Cocos2dxRenderer_setAnimationInterval(jmethodID id, va_list args);
extern jobject Cocos2dxHelper_getCocos2dxWritablePath(jmethodID id, va_list args);
extern jint Cocos2dxHelper_getSDKVersion(jmethodID id, va_list args);
extern void Cocos2dxHelper_setBackgroundMusicVolume(jmethodID id, va_list args);

// A single fake ClassLoader instance is enough for the resolver logic.
// NOTE: must be non-static because we reference it from FalsoJNI.c for
// fallback dispatch when method IDs fail to resolve.
uintptr_t g_fakeClassLoader = 0x69696969u;

static void secure_prefs_flush(void);
static void secure_prefs_flush_reason(const char* reason, int log_when_clean);
static int secure_prefs_has_key(const char* key);

// Activity/Context.getClassLoader(): ()Ljava/lang/ClassLoader;
static jobject getClassLoader(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    // Return a stable non-NULL object pointer.
    return (jobject)g_fakeClassLoader;
}

static jobject getDummyJClass(void)
{
    static jobject s_dummy_class = NULL;
    if (!s_dummy_class) {
        s_dummy_class = (jobject)jni->FindClass(&jni, "java/lang/Object");
    }
    return s_dummy_class;
}

// ClassLoader.loadClass(String): (Ljava/lang/String;)Ljava/lang/Class;
//
// Cocos2d-x's JniHelper relies on this to resolve classes like
// "org/cocos2dx/lib/Cocos2dxHelper" and then obtain method IDs.
//
// On this port, FalsoJNI method lookup is name-based and does not require a
// real class object here. Some Dark Lands startup calls arrive with a bogus or
// missing jstring argument, and attempting to decode it drives cocos2d-x's
// JniHelper into the bad_alloc path seen in the crash logs. Returning a stable
// dummy jclass is enough for subsequent GetStaticMethodID calls.
static jobject loadClass(jmethodID id, va_list args)
{
    (void)id;
    (void)args;

    static int warned = 0;
    if (!warned) {
        warned = 1;
        fjni_logv_warn("%s", "[FalsoJNI] ClassLoader.loadClass() returning dummy java/lang/Object");
    }

    return getDummyJClass();
}


/*
 * Hill Climb Racing / Shared methods
 */

jstring getCocos2dxPackageName(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getCocos2dxPackageName() called");
    return jni->NewStringUTF(&jni, "com.bulkypix.darklands");
}

jint getMarketVariation(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getMarketVariation() called");
    return 0;
}

jstring getDeviceLanguage(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getDeviceLanguage() called");
    return jni->NewStringUTF(&jni, "en");
}

// Cocos2dxHelper.getCurrentLanguage(): ()Ljava/lang/String;
jstring getCurrentLanguage(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getCurrentLanguage() called");
    return jni->NewStringUTF(&jni, "en");
}

// Cocos2dxHelper.getDeviceModel(): ()Ljava/lang/String;
jstring getDeviceModel(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getDeviceModel() called");
    return jni->NewStringUTF(&jni, "PlayStationVita");
}

// Cocos2dxHelper.getDPI(): ()I
jint getDPI(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getDPI() called");
    // Vita physical DPI is ~220; returning a plausible value avoids layout edge-cases.
    return 220;
}

// Game helper queried during graphics/UI setup. Return a sane mid-tier quality.
jint getDeviceQuality(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getDeviceQuality() called");
    return 1;
}

// Accelerometer controls (no-op on Vita unless you wire motion input)
void enableAccelerometer(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] enableAccelerometer() called");
}

void disableAccelerometer(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] disableAccelerometer() called");
}

void setAccelerometerInterval(jmethodID id, va_list args)
{
    (void)id;
    // float promoted to double in varargs
    jdouble interval = va_arg(args, jdouble);
    fjni_logv_info("[FalsoJNI] setAccelerometerInterval(%f) called", interval);
}

jstring getAndroidVersion(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getAndroidVersion() called");
    return jni->NewStringUTF(&jni, "4.4");
}

jstring getBundleVersion(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getBundleVersion() called");
    return jni->NewStringUTF(&jni, "1.0.0");
}

jstring getSystemLanguage(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getSystemLanguage() called");
    return jni->NewStringUTF(&jni, "en");
}

jstring getCountryCode(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getCountryCode() called");
    return jni->NewStringUTF(&jni, "US");
}

jint getDeviceType(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getDeviceType() called");
    return 0;
}

void showMarketplace(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] showMarketplace() called");
}

jint getSettingInt(jmethodID id, va_list args)
{
    jstring key = va_arg(args, jstring);
    jint defaultValue = va_arg(args, jint);
    const char* _key = NULL;
if (key) _key = jni->GetStringUTFChars(&jni, key, NULL);
fjni_logv_info("[FalsoJNI] getSettingInt(\"%s\", %d) called", _key ? _key : "<null>", defaultValue);
if (_key) jni->ReleaseStringUTFChars(&jni, key, (char*)_key);
    return defaultValue;
}

void flush(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] flush() called");
    DLA_DEBUG_PRINTF("[SAVE] Java flush() requested\n");
    prefs_flush_with_reason("Java flush()");
    secure_prefs_flush_reason("Java flush()", 1);
}

jboolean hasInstallReward(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] hasInstallReward() called");
    return JNI_FALSE;
}

jint getIAPCoins(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getIAPCoins() called");
    return 0;
}

jint getIAPAdFree(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getIAPAdFree() called");
    return 0;
}

jboolean hasValue(jmethodID id, va_list args)
{
    (void)id;
    jstring key = va_arg(args, jstring);
    const char* _key = NULL;
    if (key) _key = jni->GetStringUTFChars(&jni, key, NULL);
    jboolean found = secure_prefs_has_key(_key) ? JNI_TRUE : JNI_FALSE;
    fjni_logv_info("[FalsoJNI] hasValue(\"%s\") called -> %d", _key ? _key : "<null>", (int)found);
    if (_key) jni->ReleaseStringUTFChars(&jni, key, (char*)_key);
    return found;
}

void startAdView(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] startAdView() called");
}

void trackPage(jmethodID id, va_list args)
{
    jstring arg1 = va_arg(args, jstring);
    const char* _arg1 = NULL;
if (arg1) _arg1 = jni->GetStringUTFChars(&jni, arg1, NULL);
fjni_logv_info("[FalsoJNI] trackPage(\"%s\") called", _arg1 ? _arg1 : "<null>");
if (_arg1) jni->ReleaseStringUTFChars(&jni, arg1, (char*)_arg1);
}

void stopAdView(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] stopAdView() called");
}

jint getApiLevel(jmethodID id, va_list args)
{
    fjni_logv_info("%s", "[FalsoJNI] getApiLevel() called");
    return 19;
}

/*
 * Dark Lands - PSVita stubs (String storage / localization)
 *
 * These methods are normally implemented on the Java side. The game expects them
 * to exist and may crash if they return NULL.
 */

typedef struct {
    char* key;
    char* value;
} FJniKV;

#define KV_MAX_ENTRIES 512u
#define KV_MAGIC 0x31564b44u
#define KV_MAX_KEY_LEN 1024u
#define KV_MAX_VALUE_LEN 65536u

static FJniKV g_kv[KV_MAX_ENTRIES];
static int g_kv_count = 0;
static int g_kv_loaded = 0;
static int g_kv_dirty = 0;
static unsigned g_kv_save_serial = 0;
static const char* g_secure_prefs_path = DATA_PATH "DarkLandsSecurePrefs.bin";

static char* _kv_strdup_or_empty(const char* value) {
    char* copy = strdup(value ? value : "");
    if (!copy) {
        fjni_logv_warn("%s", "[FalsoJNI] secure prefs allocation failed");
    }
    return copy;
}

static void _kv_clear(void) {
    for (int i = 0; i < g_kv_count; i++) {
        free(g_kv[i].key);
        free(g_kv[i].value);
        g_kv[i].key = NULL;
        g_kv[i].value = NULL;
    }
    g_kv_count = 0;
}

static int _kv_read_exact(FILE* file, void* out, size_t size) {
    return file && KV_FREAD(out, 1, size, file) == size;
}

static int _kv_write_exact(FILE* file, const void* data, size_t size) {
    return file && KV_FWRITE(data, 1, size, file) == size;
}

static void _kv_log_value_preview(const char* prefix, const char* key, const char* value) {
    const char* safe_key = key ? key : "<null>";
    const char* safe_value = value ? value : "";
    char preview[97];
    size_t value_len = strlen(safe_value);
    size_t preview_len = value_len < (sizeof(preview) - 1u) ? value_len : (sizeof(preview) - 1u);

    memcpy(preview, safe_value, preview_len);
    preview[preview_len] = '\0';

    DLA_DEBUG_PRINTF("[SAVE][DarkLandsSecurePrefs] %s key=\"%s\" len=%u preview=\"%s%s\"\n",
                     prefix ? prefix : "value",
                     safe_key,
                     (unsigned)value_len,
                     preview,
                     value_len > preview_len ? "..." : "");
}

static void _kv_set_loaded(const char* key, const char* value, int mark_dirty) {
    if (!key) return;

    for (int i = 0; i < g_kv_count; i++) {
        if (g_kv[i].key && strcmp(g_kv[i].key, key) == 0) {
            char* copy = _kv_strdup_or_empty(value);
            if (!copy) return;
            free(g_kv[i].value);
            g_kv[i].value = copy;
            if (mark_dirty) g_kv_dirty = 1;
            return;
        }
    }

    if (g_kv_count >= (int)KV_MAX_ENTRIES) {
        fjni_logv_warn("[FalsoJNI] secure prefs full; dropping key \"%s\"", key);
        return;
    }

    char* key_copy = _kv_strdup_or_empty(key);
    char* value_copy = _kv_strdup_or_empty(value);
    if (!key_copy || !value_copy) {
        free(key_copy);
        free(value_copy);
        return;
    }

    g_kv[g_kv_count].key = key_copy;
    g_kv[g_kv_count].value = value_copy;
    g_kv_count++;
    if (mark_dirty) g_kv_dirty = 1;
}

static void secure_prefs_load_once(void) {
    if (g_kv_loaded) return;
    g_kv_loaded = 1;

    FILE* file = KV_FOPEN(g_secure_prefs_path, "rb");
    if (!file) return;

    uint32_t magic = 0;
    uint32_t count = 0;
    if (!_kv_read_exact(file, &magic, sizeof(magic)) ||
        !_kv_read_exact(file, &count, sizeof(count)) ||
        magic != KV_MAGIC ||
        count > KV_MAX_ENTRIES) {
        fjni_logv_warn("[FalsoJNI] ignoring invalid secure prefs file: %s", g_secure_prefs_path);
        KV_FCLOSE(file);
        return;
    }

    _kv_clear();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key_len = 0;
        uint32_t value_len = 0;
        if (!_kv_read_exact(file, &key_len, sizeof(key_len)) ||
            !_kv_read_exact(file, &value_len, sizeof(value_len)) ||
            key_len == 0 ||
            key_len > KV_MAX_KEY_LEN ||
            value_len > KV_MAX_VALUE_LEN) {
            fjni_logv_warn("[FalsoJNI] secure prefs stopped at invalid entry %u", (unsigned)i);
            break;
        }

        char* key = (char*)malloc((size_t)key_len + 1u);
        char* value = (char*)malloc((size_t)value_len + 1u);
        if (!key || !value) {
            free(key);
            free(value);
            fjni_logv_warn("%s", "[FalsoJNI] secure prefs load allocation failed");
            break;
        }

        if (!_kv_read_exact(file, key, key_len) ||
            !_kv_read_exact(file, value, value_len)) {
            free(key);
            free(value);
            fjni_logv_warn("[FalsoJNI] secure prefs truncated at entry %u", (unsigned)i);
            break;
        }

        key[key_len] = '\0';
        value[value_len] = '\0';
        _kv_set_loaded(key, value, 0);
        free(key);
        free(value);
    }

    g_kv_dirty = 0;
    int close_result = KV_FCLOSE(file);
    if (close_result != 0) {
        fjni_logv_warn("[FalsoJNI] failed to close secure prefs after reading: %s", g_secure_prefs_path);
    }
    fjni_logv_info("[FalsoJNI] loaded %d secure prefs from %s", g_kv_count, g_secure_prefs_path);
}

static void secure_prefs_flush(void) {
    secure_prefs_flush_reason("secure_prefs_flush", 0);
}

static void secure_prefs_flush_reason(const char* reason, int log_when_clean) {
    secure_prefs_load_once();
    if (!g_kv_dirty) {
        if (DLA_DEBUG_LOGS && log_when_clean) {
            DLA_DEBUG_PRINTF("[SAVE][DarkLandsSecurePrefs] no changes reason=\"%s\" entries=%d path=%s\n",
                             reason ? reason : "<unknown>", g_kv_count, g_secure_prefs_path);
        }
        return;
    }

    FILE* file = KV_FOPEN(g_secure_prefs_path, "wb");
    if (!file) {
        fjni_logv_warn("[FalsoJNI] failed to open secure prefs for write: %s", g_secure_prefs_path);
        DLA_ERROR_PRINTF("[SAVE][DarkLandsSecurePrefs][ERROR] open failed reason=\"%s\" path=%s\n",
                         reason ? reason : "<unknown>", g_secure_prefs_path);
        return;
    }

    uint32_t valid_count = 0;
    for (int i = 0; i < g_kv_count; i++) {
        if (!g_kv[i].key || !g_kv[i].value) continue;
        size_t key_len = strlen(g_kv[i].key);
        size_t value_len = strlen(g_kv[i].value);
        if (key_len == 0 || key_len > KV_MAX_KEY_LEN || value_len > KV_MAX_VALUE_LEN) continue;
        valid_count++;
    }

    uint32_t magic = KV_MAGIC;
    int ok = _kv_write_exact(file, &magic, sizeof(magic)) &&
             _kv_write_exact(file, &valid_count, sizeof(valid_count));

    for (int i = 0; ok && i < g_kv_count; i++) {
        if (!g_kv[i].key || !g_kv[i].value) continue;
        size_t raw_key_len = strlen(g_kv[i].key);
        size_t raw_value_len = strlen(g_kv[i].value);
        if (raw_key_len == 0 || raw_key_len > KV_MAX_KEY_LEN || raw_value_len > KV_MAX_VALUE_LEN) continue;

        uint32_t key_len = (uint32_t)raw_key_len;
        uint32_t value_len = (uint32_t)raw_value_len;
        ok = _kv_write_exact(file, &key_len, sizeof(key_len)) &&
             _kv_write_exact(file, &value_len, sizeof(value_len)) &&
             _kv_write_exact(file, g_kv[i].key, key_len) &&
             _kv_write_exact(file, g_kv[i].value, value_len);
    }

    if (KV_FFLUSH(file) != 0) {
        ok = 0;
    }
    if (KV_FCLOSE(file) != 0) {
        ok = 0;
    }
    if (ok) {
        g_kv_dirty = 0;
        g_kv_save_serial++;
        fjni_logv_info("[FalsoJNI] saved %u secure prefs to %s", (unsigned)valid_count, g_secure_prefs_path);
        DLA_DEBUG_PRINTF("[SAVE][DarkLandsSecurePrefs] SAVED #%u reason=\"%s\" entries=%u path=%s\n",
                         g_kv_save_serial,
                         reason ? reason : "<unknown>",
                         (unsigned)valid_count,
                         g_secure_prefs_path);
    } else {
        fjni_logv_warn("[FalsoJNI] failed while writing secure prefs: %s", g_secure_prefs_path);
        DLA_ERROR_PRINTF("[SAVE][DarkLandsSecurePrefs][ERROR] write failed reason=\"%s\" path=%s\n",
                         reason ? reason : "<unknown>", g_secure_prefs_path);
    }
}

void game_prefs_init(void) {
    if (!file_mkpath(g_secure_prefs_path, 0777)) {
        fjni_logv_warn("[FalsoJNI] could not create save directory for %s", g_secure_prefs_path);
        return;
    }

    int save_exists = file_exists(g_secure_prefs_path);
    secure_prefs_load_once();

    // Create a valid empty database immediately on first boot. This both proves
    // that the data directory is writable and gives later saveInt/saveString
    // calls a durable file to update.
    if (!save_exists) {
        g_kv_dirty = 1;
        secure_prefs_flush_reason("first boot create secure prefs", 1);
    }
}

void game_prefs_flush(const char* reason) {
    secure_prefs_flush_reason(reason ? reason : "game_prefs_flush", 1);
}

void game_prefs_shutdown(void) {
    secure_prefs_flush_reason("game_prefs_shutdown", 1);
}

static const char* _kv_get(const char* key) {
    if (!key) return NULL;
    secure_prefs_load_once();
    for (int i = 0; i < g_kv_count; i++) {
        if (g_kv[i].key && strcmp(g_kv[i].key, key) == 0) {
            return g_kv[i].value;
        }
    }
    return NULL;
}

static int secure_prefs_has_key(const char* key) {
    return _kv_get(key) != NULL;
}

static void _kv_set(const char* key, const char* value) {
    if (!key) return;
    secure_prefs_load_once();
    _kv_set_loaded(key, value, 1);
}

void initialize(jmethodID id, va_list args)
{
    (void)id; (void)args;
    fjni_logv_info("%s", "[FalsoJNI] initialize() called");
}

jstring getEnvironmentLang(jmethodID id, va_list args)
{
    (void)id; (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getEnvironmentLang() called");
    return jni->NewStringUTF(&jni, "en");
}

// Returns a 2-letter country code. Keep non-NULL for game logic that expects it.
jstring getEnvironmentCountry(jmethodID id, va_list args)
{
    (void)id; (void)args;
    fjni_logv_info("%s", "[FalsoJNI] getEnvironmentCountry() called");
    // Conservative default; adjust if the game expects a specific locale.
    return jni->NewStringUTF(&jni, "US");
}

jstring loadString(jmethodID id, va_list args)
{
    (void)id;
    jstring jkey = va_arg(args, jstring);

    const char* key = NULL;
    if (jkey) key = jni->GetStringUTFChars(&jni, jkey, NULL);

    const char* val = _kv_get(key);
    if (!val) val = "";

    fjni_logv_info("[FalsoJNI] loadString(\"%s\") called -> len=%u",
                   key ? key : "<null>", (unsigned)strlen(val ? val : ""));

    jstring out = jni->NewStringUTF(&jni, val ? val : "");
    if (key) jni->ReleaseStringUTFChars(&jni, jkey, (char*)key);
    return out;
}

jint loadInt(jmethodID id, va_list args)
{
    (void)id;
    jstring jkey = va_arg(args, jstring);
    jint def = va_arg(args, jint);

    const char* key = NULL;
    if (jkey) key = jni->GetStringUTFChars(&jni, jkey, NULL);

    const char* val = _kv_get(key);
    jint out = (val && val[0]) ? (jint)atoi(val) : def;

    fjni_logv_info("[FalsoJNI] loadInt(\"%s\", %d) called -> %d",
                   key ? key : "<null>", (int)def, (int)out);

    if (key) jni->ReleaseStringUTFChars(&jni, jkey, (char*)key);
    return out;
}

void saveString(jmethodID id, va_list args)
{
    (void)id;
    jstring jkey = va_arg(args, jstring);
    jstring jval = va_arg(args, jstring);

    const char* key = NULL;
    const char* val = NULL;

    if (jkey) key = jni->GetStringUTFChars(&jni, jkey, NULL);
    if (jval) val = jni->GetStringUTFChars(&jni, jval, NULL);

    fjni_logv_info("[FalsoJNI] saveString(\"%s\") called len=%u",
                   key ? key : "<null>",
                   (unsigned)strlen(val ? val : ""));
    if (DLA_DEBUG_LOGS)
        _kv_log_value_preview("saveString", key, val);

    _kv_set(key, val);
    secure_prefs_flush_reason("saveString", 1);

    if (key) jni->ReleaseStringUTFChars(&jni, jkey, (char*)key);
    if (val) jni->ReleaseStringUTFChars(&jni, jval, (char*)val);
}

void saveInt(jmethodID id, va_list args)
{
    (void)id;
    jstring jkey = va_arg(args, jstring);
    jint val = va_arg(args, jint);

    const char* key = NULL;
    if (jkey) key = jni->GetStringUTFChars(&jni, jkey, NULL);

    char value[32];
    snprintf(value, sizeof(value), "%d", (int)val);

    fjni_logv_info("[FalsoJNI] saveInt(\"%s\", %d) called",
                   key ? key : "<null>",
                   (int)val);
    DLA_DEBUG_PRINTF("[SAVE][DarkLandsSecurePrefs] saveInt key=\"%s\" value=%d\n",
                     key ? key : "<null>", (int)val);

    _kv_set(key, value);
    secure_prefs_flush_reason("saveInt", 1);

    if (key) jni->ReleaseStringUTFChars(&jni, jkey, (char*)key);
}

void beginBatch(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    secure_prefs_load_once();
    fjni_logv_info("%s", "[FalsoJNI] BeginBatch() called");
}

void endBatch(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] EndBatch() called");
    secure_prefs_flush_reason("EndBatch", 1);
}

void LogStart(jmethodID id, va_list args)
{
    (void)id;
    jint arg = va_arg(args, jint);
    fjni_logv_info("[FalsoJNI] LogStart(%d) called", (int)arg);
}

void initAds(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    fjni_logv_info("%s", "[FalsoJNI] initAds() called");
}

// Dark Lands polls Java ad helpers from menu/gameplay code. On Vita there is no
// ad backend, so return "not available" instead of letting JniHelper fail and
// emit an error every frame.
jboolean isAdAvailable(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
    return JNI_FALSE;
}

void sendOnMainMenuLoading(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
}

void logLevelStart(jmethodID id, va_list args)
{
    (void)id;
    (void)args;
}

void GetItems(jmethodID id, va_list args)
{
    (void)id;
    jobject items = va_arg(args, jobject);
    fjni_logv_info("[FalsoJNI] GetItems(%p) called; no Java inventory bridge on Vita", items);
}

/*
 * JNI Methods Table
 */

NameToMethodID nameToMethodId[] = {
    // --- Original methods ---
    {100, "setAnimationInterval", METHOD_TYPE_VOID},
    {101, "getIntegerForKey", METHOD_TYPE_INT},
    {102, "getCocos2dxPackageName", METHOD_TYPE_OBJECT},
    {103, "getMarketVariation", METHOD_TYPE_INT},
    {104, "getDeviceLanguage", METHOD_TYPE_OBJECT},
    {136, "getCurrentLanguage", METHOD_TYPE_OBJECT},
    {137, "getDeviceModel", METHOD_TYPE_OBJECT},
    {138, "getDPI", METHOD_TYPE_INT},
    {148, "getDeviceQuality", METHOD_TYPE_INT},
    {105, "getAndroidVersion", METHOD_TYPE_OBJECT},
    {109, "getSettingInt", METHOD_TYPE_INT},
    {110, "setStringForKey", METHOD_TYPE_VOID},
    {111, "flush", METHOD_TYPE_VOID},
    {112, "getStringForKey", METHOD_TYPE_OBJECT},
    {113, "setIntegerForKey", METHOD_TYPE_VOID},
    {142, "getBoolForKey", METHOD_TYPE_BOOLEAN},
    {143, "setBoolForKey", METHOD_TYPE_VOID},
    {144, "getFloatForKey", METHOD_TYPE_FLOAT},
    {145, "setFloatForKey", METHOD_TYPE_VOID},
    {146, "getDoubleForKey", METHOD_TYPE_DOUBLE},
    {147, "setDoubleForKey", METHOD_TYPE_VOID},
    {114, "hasInstallReward", METHOD_TYPE_BOOLEAN},
    {115, "getIAPCoins", METHOD_TYPE_INT},
    {116, "getIAPAdFree", METHOD_TYPE_INT},
    {117, "hasValue", METHOD_TYPE_BOOLEAN},
    {118, "startAdView", METHOD_TYPE_VOID},
    {119, "stopAdView", METHOD_TYPE_VOID},
    {120, "trackPage", METHOD_TYPE_VOID},
    {121, "getApiLevel", METHOD_TYPE_INT},
    {139, "enableAccelerometer", METHOD_TYPE_VOID},
    {140, "disableAccelerometer", METHOD_TYPE_VOID},
    {141, "setAccelerometerInterval", METHOD_TYPE_VOID},

    // Sound / Audio
    {108, "preloadEffect", METHOD_TYPE_VOID},
    {122, "unloadEffect", METHOD_TYPE_VOID},
    {123, "playEffect", METHOD_TYPE_INT},
    {124, "setEffectVolume", METHOD_TYPE_VOID},
    {125, "setEffectRate", METHOD_TYPE_VOID},
    {126, "stopEffect", METHOD_TYPE_VOID},
    {127, "pauseEffect", METHOD_TYPE_VOID},
    {128, "resumeEffect", METHOD_TYPE_VOID},
    {129, "pauseAllEffects", METHOD_TYPE_VOID},
    {130, "resumeAllEffects", METHOD_TYPE_VOID},
    {131, "stopAllEffects", METHOD_TYPE_VOID},
    {132, "getEffectsVolume", METHOD_TYPE_FLOAT},
    {133, "setEffectsVolume", METHOD_TYPE_VOID},
    {134, "preloadEffect", METHOD_TYPE_VOID},

    // Music
    {200, "preloadBackgroundMusic", METHOD_TYPE_VOID},
    {201, "playBackgroundMusic", METHOD_TYPE_VOID},
    {202, "stopBackgroundMusic", METHOD_TYPE_VOID},
    {203, "pauseBackgroundMusic", METHOD_TYPE_VOID},
    {204, "resumeBackgroundMusic", METHOD_TYPE_VOID},
    {205, "rewindBackgroundMusic", METHOD_TYPE_VOID},
    {206, "isBackgroundMusicPlaying", METHOD_TYPE_BOOLEAN},
    {207, "endBackgroundMusic", METHOD_TYPE_VOID},
    {208, "getBackgroundVolume", METHOD_TYPE_FLOAT},
    {209, "setBackgroundMusicVolume", METHOD_TYPE_VOID},

    // --- Dark Lands / New additions ---
    {301, "getCocos2dxWritablePath", METHOD_TYPE_OBJECT},
    {302, "getSDKVersion", METHOD_TYPE_INT},
	{400, "getClassLoader", METHOD_TYPE_OBJECT},
	{401, "loadClass", METHOD_TYPE_OBJECT},

// App / game-specific Java helpers (stubs)
{410, "initialize", METHOD_TYPE_VOID},
{411, "getEnvironmentLang", METHOD_TYPE_OBJECT},
{414, "getEnvironmentCountry", METHOD_TYPE_OBJECT},
{412, "loadString", METHOD_TYPE_OBJECT},
{413, "saveString", METHOD_TYPE_VOID},
{415, "loadInt", METHOD_TYPE_INT},
{416, "LogStart", METHOD_TYPE_VOID},
{417, "initAds", METHOD_TYPE_VOID},
{418, "getBundleVersion", METHOD_TYPE_OBJECT},
{419, "GetBundleVersion", METHOD_TYPE_OBJECT},
{420, "getSystemLanguage", METHOD_TYPE_OBJECT},
{421, "GetSystemLanguage", METHOD_TYPE_OBJECT},
{422, "getCountryCode", METHOD_TYPE_OBJECT},
{423, "GetCountryCode", METHOD_TYPE_OBJECT},
{424, "getDeviceType", METHOD_TYPE_INT},
{425, "GetDeviceType", METHOD_TYPE_INT},
{426, "showMarketplace", METHOD_TYPE_VOID},
{427, "ShowMarketplace", METHOD_TYPE_VOID},
{428, "GetItems", METHOD_TYPE_VOID},
{429, "getItems", METHOD_TYPE_VOID},
{430, "saveInt", METHOD_TYPE_VOID},
{431, "SaveInt", METHOD_TYPE_VOID},
{432, "LoadInt", METHOD_TYPE_INT},
{433, "LoadString", METHOD_TYPE_OBJECT},
{434, "SaveString", METHOD_TYPE_VOID},
{435, "BeginBatch", METHOD_TYPE_VOID},
{436, "EndBatch", METHOD_TYPE_VOID},
{437, "beginBatch", METHOD_TYPE_VOID},
{438, "endBatch", METHOD_TYPE_VOID},
{439, "IsAdAvailable", METHOD_TYPE_BOOLEAN},
{440, "isAdAvailable", METHOD_TYPE_BOOLEAN},
{441, "sendOnMainMenuLoading", METHOD_TYPE_VOID},
{442, "LogLevelStart", METHOD_TYPE_VOID},
{443, "logLevelStart", METHOD_TYPE_VOID},


};

MethodsBoolean methodsBoolean[] = {
    {142, getBoolForKey},
    {114, hasInstallReward},
    {117, hasValue},
    {206, isBackgroundMusicPlaying},
    {439, isAdAvailable},
    {440, isAdAvailable},
};

MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {
    {146, getDoubleForKey},
};

MethodsFloat methodsFloat[] = {
    {144, getFloatForKey},
    {132, getEffectsVolume},
    {208, getBackgroundVolume},
};

MethodsInt methodsInt[] = {
    {101, getIntegerForKey},
    {103, getMarketVariation},
    {109, getSettingInt},
    {115, getIAPCoins},
    {116, getIAPAdFree},
    {121, getApiLevel},
    {123, playEffect},
    {138, getDPI},
    {148, getDeviceQuality},
    // New additions
    {302, Cocos2dxHelper_getSDKVersion},
    {415, loadInt},
    {432, loadInt},
    {424, getDeviceType},
    {425, getDeviceType},
};

MethodsLong methodsLong[] = {};

MethodsObject methodsObject[] = {
    {102, getCocos2dxPackageName},
    {104, getDeviceLanguage},
    {136, getCurrentLanguage},
    {137, getDeviceModel},
    {105, getAndroidVersion},
    {112, getStringForKey},
    // New additions
    {301, Cocos2dxHelper_getCocos2dxWritablePath},
	{400, getClassLoader},
	{401, loadClass},

{411, getEnvironmentLang},
{414, getEnvironmentCountry},
{412, loadString},
{433, loadString},
{418, getBundleVersion},
{419, getBundleVersion},
{420, getSystemLanguage},
{421, getSystemLanguage},
{422, getCountryCode},
{423, getCountryCode},

};

MethodsShort methodsShort[] = {};

MethodsVoid methodsVoid[] = {
    // Replaced local function with Bridge implementation:
    {100, Cocos2dxRenderer_setAnimationInterval},
    {108, preloadEffect},
    {110, setStringForKey},
    {143, setBoolForKey},
    {145, setFloatForKey},
    {147, setDoubleForKey},
    {111, flush},
    {113, setIntegerForKey},
    {139, enableAccelerometer},
    {140, disableAccelerometer},
    {141, setAccelerometerInterval},
    {118, startAdView},
    {119, stopAdView},
    {120, trackPage},
    {122, unloadEffect},
    {124, setEffectVolume},
    {125, setEffectRate},
    {126, stopEffect},
    {127, pauseEffect},
    {128, resumeEffect},
    {129, pauseAllEffects},
    {130, resumeAllEffects},
    {131, stopAllEffects},
    {133, setEffectsVolume},
    {200, preloadBackgroundMusic},
    {201, playBackgroundMusic},
    {202, stopBackgroundMusic},
    {203, pauseBackgroundMusic},
    {204, resumeBackgroundMusic},
    {205, rewindBackgroundMusic},
    {207, endBackgroundMusic},
    // Replaced with Bridge implementation:
    {209, Cocos2dxHelper_setBackgroundMusicVolume},

// App / game-specific Java helpers (stubs)
{410, initialize},
{413, saveString},
{430, saveInt},
{431, saveInt},
{434, saveString},
{435, beginBatch},
{436, endBatch},
{437, beginBatch},
{438, endBatch},
{416, LogStart},
{417, initAds},
{426, showMarketplace},
{427, showMarketplace},
{428, GetItems},
{429, GetItems},
{441, sendOnMainMenuLoading},
{442, logLevelStart},
{443, logLevelStart},

};

/*
 * JNI Fields
 */

// System-wide constant that applications sometimes request
char WINDOW_SERVICE[] = "window";

// System-wide constant for Android version
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
    {0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT},
    {1, "SDK_INT", FIELD_TYPE_INT},
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};

FieldsInt fieldsInt[] = {
    {1, SDK_INT},
};

FieldsObject fieldsObject[] = {
    {0, WINDOW_SERVICE},
};

FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
