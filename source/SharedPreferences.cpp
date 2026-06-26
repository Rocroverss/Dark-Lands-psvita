#include "SharedPreferences.h"
#include "debug_log.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <psp2/kernel/clib.h>
#include <falso_jni/FalsoJNI.h>
#include <falso_jni/jni.h>

std::map<std::string, bool> prefsBools;
std::map<std::string, int> prefsInts;
std::map<std::string, jstring> prefsStrings;
std::map<std::string, float> prefsFloats;
std::map<std::string, double> prefsDoubles;

std::mutex prefsMutex;

const std::string shared_prefs_path = DATA_PATH "SharedPreferences.bin";
const std::string shared_prefs_bak_path = DATA_PATH "SharedPreferences.bin.bak";
const std::string shaders_path = DATA_PATH "gxp/";

static constexpr uint32_t PREFS_MAGIC = 0x31504644u;
static constexpr uint32_t PREFS_VERSION = 1u;
static constexpr uint32_t PREFS_MAX_ENTRIES = 512u;
static constexpr uint32_t PREFS_MAX_STRING_BYTES = 16u * 1024u;

static std::thread prefsSaverThread;
static std::condition_variable prefsSaverCond;
static std::mutex prefsSaverMutex;
static std::atomic<bool> prefsSaverRunning{false};
static std::atomic<bool> needsSaving{false};
static int saveIntervalSeconds = 2;

static void markPrefsDirty()
{
    needsSaving.exchange(true);
    prefs_flush();
}

static bool prefsFileHasData(const std::string &filename)
{
    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    return in.is_open() && in.tellg() > 0;
}

template <typename T>
void writeValue(std::ofstream &out, const T &value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

template <typename T>
bool readValue(std::ifstream &in, T &value)
{
    return static_cast<bool>(in.read(reinterpret_cast<char *>(&value), sizeof(T)));
}

void writeString(std::ofstream &out, jstring str)
{
    const char *val = jni.GetStringUTFChars(str, NULL);
    uint32_t len = static_cast<uint32_t>(jni.GetStringLength(str));
    writeValue(out, len);
    out.write(val, len);
    jni.ReleaseStringUTFChars(str, val);
}

void writeString(std::ofstream &out, const std::string &str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    writeValue(out, len);
    out.write(str.data(), len);
}

bool readString(std::ifstream &in, std::string &str)
{
    uint32_t len = 0;
    if (!readValue(in, len) || len > PREFS_MAX_STRING_BYTES)
        return false;
    str.resize(len);
    return len == 0 || static_cast<bool>(in.read(&str[0], len));
}

bool readString(std::ifstream &in, jstring &str)
{
    uint32_t len = 0;
    if (!readValue(in, len) || len > PREFS_MAX_STRING_BYTES)
        return false;
    std::string s(len, '\0');
    if (len != 0 && !in.read(&s[0], len))
        return false;
    str = jni.NewStringUTF(s.c_str());
    return str != nullptr;
}

bool savePrefs(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(prefsMutex);
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    writeValue(out, PREFS_MAGIC);
    writeValue(out, PREFS_VERSION);

    // bools
    writeValue(out, static_cast<uint32_t>(prefsBools.size()));
    for (auto &[k, v] : prefsBools)
    {
        writeString(out, k);
        writeValue(out, v);
    }

    // ints
    writeValue(out, static_cast<uint32_t>(prefsInts.size()));
    for (auto &[k, v] : prefsInts)
    {
        writeString(out, k);
        writeValue(out, v);
    }

    // strings
    writeValue(out, static_cast<uint32_t>(prefsStrings.size()));
    for (auto &[k, v] : prefsStrings)
    {
        writeString(out, k);
        writeString(out, v);
    }

    // floats (added later; safe to append)
    writeValue(out, static_cast<uint32_t>(prefsFloats.size()));
    for (auto &[k, v] : prefsFloats)
    {
        writeString(out, k);
        writeValue(out, v);
    }

    // doubles (added later; safe to append)
    writeValue(out, static_cast<uint32_t>(prefsDoubles.size()));
    for (auto &[k, v] : prefsDoubles)
    {
        writeString(out, k);
        writeValue(out, v);
    }

    out.flush();
    return out.good();
}

void loadPrefs(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(prefsMutex);
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open())
        return;

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!readValue(in, magic) || !readValue(in, version) ||
        magic != PREFS_MAGIC || version != PREFS_VERSION)
    {
        l_warn("Ignoring incompatible preferences file: %s", filename.c_str());
        return;
    }

    prefsBools.clear();
    prefsInts.clear();
    prefsStrings.clear();
    prefsFloats.clear();
    prefsDoubles.clear();

    uint32_t count = 0;

    // bools
    if (!readValue(in, count) || count > PREFS_MAX_ENTRIES)
        return;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string key;
        bool val = false;
        if (!readString(in, key) || !readValue(in, val))
            return;
        prefsBools[key] = val;
    }

    // ints
    if (!readValue(in, count) || count > PREFS_MAX_ENTRIES)
        return;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string key;
        int val = 0;
        if (!readString(in, key) || !readValue(in, val))
            return;
        prefsInts[key] = val;
    }

    // strings
    if (!readValue(in, count) || count > PREFS_MAX_ENTRIES)
        return;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string key;
        jstring val = nullptr;
        if (!readString(in, key) || !readString(in, val))
            return;
        prefsStrings[key] = val;
    }

    // floats
    if (!readValue(in, count) || count > PREFS_MAX_ENTRIES)
        return;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string key;
        float val = 0.0f;
        if (!readString(in, key) || !readValue(in, val))
            return;
        prefsFloats[key] = val;
    }

    // doubles
    if (!readValue(in, count) || count > PREFS_MAX_ENTRIES)
        return;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string key;
        double val = 0.0;
        if (!readString(in, key) || !readValue(in, val))
            return;
        prefsDoubles[key] = val;
    }
}

jboolean getBoolForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    int default_value = va_arg(args, int);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    if (key == "remove_ads")
        return 1;

    std::lock_guard<std::mutex> lock(prefsMutex);
    auto it = prefsBools.find(key);
    return it != prefsBools.end() ? it->second : default_value;
}

jint getIntegerForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    int default_value = va_arg(args, int);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    std::lock_guard<std::mutex> lock(prefsMutex);
    auto it = prefsInts.find(key);
    return it != prefsInts.end() ? it->second : default_value;
}

jfloat getFloatForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    // float is promoted to double in varargs
    double default_value = va_arg(args, double);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    std::lock_guard<std::mutex> lock(prefsMutex);
    auto it = prefsFloats.find(key);
    return it != prefsFloats.end() ? static_cast<jfloat>(it->second) : static_cast<jfloat>(default_value);
}

jdouble getDoubleForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    double default_value = va_arg(args, double);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    std::lock_guard<std::mutex> lock(prefsMutex);
    auto it = prefsDoubles.find(key);
    return it != prefsDoubles.end() ? static_cast<jdouble>(it->second) : static_cast<jdouble>(default_value);
}

jobject getStringForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    jstring _default_value = va_arg(args, jstring);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    std::lock_guard<std::mutex> lock(prefsMutex);
    auto it = prefsStrings.find(key);
    return it != prefsStrings.end() ? it->second : _default_value;
}

void setIntegerForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    int val = va_arg(args, int);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    {
        std::lock_guard<std::mutex> lock(prefsMutex);
        prefsInts[key] = val;
    }
    markPrefsDirty();
}

void setFloatForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    // float promoted to double
    double val = va_arg(args, double);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    {
        std::lock_guard<std::mutex> lock(prefsMutex);
        prefsFloats[key] = static_cast<float>(val);
    }
    markPrefsDirty();
}

void setDoubleForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    double val = va_arg(args, double);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    {
        std::lock_guard<std::mutex> lock(prefsMutex);
        prefsDoubles[key] = val;
    }
    markPrefsDirty();
}

void setBoolForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    int val = va_arg(args, int);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    {
        std::lock_guard<std::mutex> lock(prefsMutex);
        prefsBools[key] = val;
    }
    markPrefsDirty();
}

void setStringForKey(jmethodID, va_list args)
{
    jstring _key = va_arg(args, jstring);
    jstring val = va_arg(args, jstring);

    const char *chars = jni.GetStringUTFChars(_key, nullptr);
    std::string key(chars);
    jni.ReleaseStringUTFChars(_key, chars);

    {
        std::lock_guard<std::mutex> lock(prefsMutex);
        prefsStrings[key] = val;
    }
    markPrefsDirty();
}

void prefs_flush()
{
    prefs_flush_with_reason("prefs_flush");
}

void prefs_flush_with_reason(const char *reason)
{
    if (needsSaving.exchange(false))
    {
        l_debug("Flushing preferences...");
        bool ok = savePrefs(shared_prefs_path);
        if (ok)
        {
            DLA_DEBUG_PRINTF("[SAVE][SharedPreferences] SAVED reason=\"%s\" bools=%u ints=%u strings=%u floats=%u doubles=%u path=%s\n",
                             reason ? reason : "<unknown>",
                             static_cast<unsigned>(prefsBools.size()),
                             static_cast<unsigned>(prefsInts.size()),
                             static_cast<unsigned>(prefsStrings.size()),
                             static_cast<unsigned>(prefsFloats.size()),
                             static_cast<unsigned>(prefsDoubles.size()),
                             shared_prefs_path.c_str());
        }
        else
        {
            DLA_ERROR_PRINTF("[SAVE][SharedPreferences][ERROR] FAILED reason=\"%s\" path=%s\n",
                             reason ? reason : "<unknown>",
                             shared_prefs_path.c_str());
        }
    }
    else
    {
        DLA_DEBUG_PRINTF("[SAVE][SharedPreferences] no changes reason=\"%s\" path=%s\n",
                         reason ? reason : "<unknown>",
                         shared_prefs_path.c_str());
    }
}

static void prefsSaverLoop()
{
    std::unique_lock<std::mutex> lock(prefsSaverMutex);
    while (prefsSaverRunning)
    {
        prefsSaverCond.wait_for(lock, std::chrono::seconds(saveIntervalSeconds));
        if (!prefsSaverRunning)
            break;
        if (needsSaving.exchange(false))
        {
            l_debug("Saving preferences...");
            bool ok = savePrefs(shared_prefs_path);
            if (ok)
                DLA_DEBUG_PRINTF("[SAVE][SharedPreferences] SAVED reason=\"background thread\" path=%s\n",
                                 shared_prefs_path.c_str());
            else
                DLA_ERROR_PRINTF("[SAVE][SharedPreferences][ERROR] FAILED reason=\"background thread\" path=%s\n",
                                 shared_prefs_path.c_str());
        }
    }
}

void startPrefsSaver(int intervalSec)
{
    saveIntervalSeconds = intervalSec;
    prefsSaverRunning = true;
    prefsSaverThread = std::thread(prefsSaverLoop);
}

void prefs_destroy()
{
    prefs_flush_with_reason("prefs_destroy begin");
    prefsSaverRunning = false;
    prefsSaverCond.notify_all();
    if (prefsSaverThread.joinable())
        prefsSaverThread.join();
    prefs_flush_with_reason("prefs_destroy end");
}

void prefs_init()
{
    try
    {
        loadPrefs(shared_prefs_path);
        DLA_DEBUG_PRINTF("[SAVE][SharedPreferences] loaded bools=%u ints=%u strings=%u floats=%u doubles=%u path=%s\n",
                         static_cast<unsigned>(prefsBools.size()),
                         static_cast<unsigned>(prefsInts.size()),
                         static_cast<unsigned>(prefsStrings.size()),
                         static_cast<unsigned>(prefsFloats.size()),
                         static_cast<unsigned>(prefsDoubles.size()),
                         shared_prefs_path.c_str());
    }
    catch (...)
    {
        l_error("Ignoring preferences file after a load exception: %s", shared_prefs_path.c_str());
    }
    if (prefsFileHasData(shared_prefs_path))
        file_copy(shared_prefs_path.c_str(), shared_prefs_bak_path.c_str()); // make backup so user can restore if the dreaded crash happens
}
