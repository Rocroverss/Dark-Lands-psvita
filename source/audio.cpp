#include "audio.h"
#include "debug_log.h"
#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

static std::string audio_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

static std::string audio_extension(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return {};

    return audio_lower(path.substr(dot));
}

static std::string audio_asset_path(const std::string &relative)
{
    return std::string(DATA_PATH) + "assets/" + relative;
}

static bool audio_file_exists(const std::string &path)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

template <typename AudioSource>
static SoLoud::result audio_load_asset(AudioSource &source,
                                       const std::string &requested,
                                       std::string &resolved,
                                       bool log_errors = true)
{
    if (audio_extension(requested) == ".mp3")
    {
        const std::string stem = requested.substr(0, requested.size() - 4);
        const std::string ogg_relative = stem + ".ogg";
        const std::string ogg_path = audio_asset_path(ogg_relative);
        const std::string wav_relative = stem + ".wav";
        const std::string wav_path = audio_asset_path(wav_relative);

        const char *format = nullptr;
        std::string replacement;
        std::string replacement_path;
        if (audio_file_exists(ogg_path))
        {
            format = "OGG";
            replacement = ogg_relative;
            replacement_path = ogg_path;
        }
        else if (audio_file_exists(wav_path))
        {
            format = "WAV";
            replacement = wav_relative;
            replacement_path = wav_path;
        }
        else
        {
            if (log_errors)
            {
                DLA_ERROR_PRINTF("[Audio][MP3->OGG][ERROR] requested \"%s\" but replacement is missing: \"%s\"\n",
                                 requested.c_str(), ogg_path.c_str());
            }
            return SoLoud::FILE_LOAD_FAILED;
        }

        DLA_DEBUG_PRINTF("[Audio][MP3->%s] redirect \"%s\" -> \"%s\"\n",
                         format, requested.c_str(), replacement_path.c_str());
        SoLoud::result result = SoLoud::FILE_LOAD_FAILED;
        try
        {
            result = source.load(replacement_path.c_str());
        }
        catch (const std::bad_alloc &)
        {
            if (log_errors)
            {
                DLA_ERROR_PRINTF("[Audio][MP3->%s][ERROR] out of memory while loading \"%s\"\n",
                                 format, replacement_path.c_str());
            }
            return SoLoud::FILE_LOAD_FAILED;
        }
        if (result == SoLoud::SO_NO_ERROR)
        {
            resolved = replacement;
            DLA_DEBUG_PRINTF("[Audio][MP3->%s][OK] loaded \"%s\"\n",
                             format, replacement_path.c_str());
            return result;
        }

        if (log_errors)
        {
            DLA_ERROR_PRINTF("[Audio][MP3->%s][ERROR] decoder rejected \"%s\" (SoLoud error %d)\n",
                             format, replacement_path.c_str(), result);
        }
        return result;
    }

    const std::string path = audio_asset_path(requested);
    if (!audio_file_exists(path))
    {
        if (log_errors)
            DLA_ERROR_PRINTF("[Audio][ERROR] asset missing: \"%s\"\n", path.c_str());
        return SoLoud::FILE_LOAD_FAILED;
    }

    SoLoud::result result = SoLoud::FILE_LOAD_FAILED;
    try
    {
        result = source.load(path.c_str());
    }
    catch (const std::bad_alloc &)
    {
        if (log_errors)
            DLA_ERROR_PRINTF("[Audio][ERROR] out of memory while loading \"%s\"\n", path.c_str());
        return SoLoud::FILE_LOAD_FAILED;
    }
    if (result == SoLoud::SO_NO_ERROR)
        resolved = requested;
    else if (log_errors)
        DLA_ERROR_PRINTF("[Audio][ERROR] decoder rejected \"%s\" (SoLoud error %d)\n",
                         path.c_str(), result);
    return result;
}

struct AudioSystem
{
    SoLoud::Soloud soloud;
    std::mutex mutex;
} gAudioSystem;

struct MusicManager
{
    SoLoud::WavStream bgm;
    bool hasBgmLoaded = false;
    float volumeLeft = 0.5f;
    float volumeRight = 0.5f;
    SoLoud::handle handle = 0;
    std::string currentPath;
    std::mutex mutex;
} gMusicManager;

struct SoundManager
{
    std::map<std::string, SoLoud::Wav *> sounds;
    std::map<std::string, std::vector<SoLoud::handle>> handles;
    std::map<std::string, uint64_t> lastUsed;
    std::vector<std::string> warmQueue;
    uint64_t useCounter = 0;
    size_t warmedEffects = 0;
    float volumeLeft = 0.5f;
    float volumeRight = 0.5f;
    std::mutex mutex;
} gSoundManager;

#ifndef DLA_AUDIO_EFFECT_CACHE_LIMIT
#define DLA_AUDIO_EFFECT_CACHE_LIMIT 32
#endif

#ifndef DLA_AUDIO_WARMUP_EFFECTS
#define DLA_AUDIO_WARMUP_EFFECTS 1
#endif

#ifndef DLA_AUDIO_WARMUP_MAX_EFFECTS
#define DLA_AUDIO_WARMUP_MAX_EFFECTS 24
#endif

#ifndef DLA_AUDIO_WARMUP_ASYNC
#define DLA_AUDIO_WARMUP_ASYNC 1
#endif

#ifndef DLA_AUDIO_SYNC_PRELOAD_EFFECTS
#define DLA_AUDIO_SYNC_PRELOAD_EFFECTS 0
#endif

#ifndef DLA_AUDIO_WARMUP_START_DELAY_US
#define DLA_AUDIO_WARMUP_START_DELAY_US 350000
#endif

#ifndef DLA_AUDIO_WARMUP_STEP_DELAY_US
#define DLA_AUDIO_WARMUP_STEP_DELAY_US 60000
#endif

static constexpr size_t MAX_CACHED_EFFECTS =
    DLA_AUDIO_EFFECT_CACHE_LIMIT > 0 ? DLA_AUDIO_EFFECT_CACHE_LIMIT : 16;
static constexpr size_t MAX_WARMED_EFFECTS =
    DLA_AUDIO_WARMUP_MAX_EFFECTS > 0 ? DLA_AUDIO_WARMUP_MAX_EFFECTS : 0;

static pthread_t gAudioWarmupThread;
static bool gAudioWarmupThreadStarted = false;
static volatile bool gAudioWarmupStop = false;

static const char *const kCoreWarmupEffects[] = {
    "hero_jump_01.mp3",
    "hero_jump_02.mp3",
    "sword_attack_01.mp3",
    "sword_attack_02.mp3",
    "sword_attack_03.mp3",
    "sword_attack_04.mp3",
    "sword_attack_05.mp3",
    "sword_attack_07.mp3",
    "sword_block_01.mp3",
    "sword_block_02.mp3",
    "scorpion_attack_a.mp3",
    "wasp_attack.mp3"};
static constexpr size_t CORE_WARMUP_EFFECT_COUNT =
    sizeof(kCoreWarmupEffects) / sizeof(kCoreWarmupEffects[0]);

static void cleanup_finished_effect_handles_locked()
{
    for (auto &pair : gSoundManager.handles)
    {
        auto &handles = pair.second;
        handles.erase(std::remove_if(handles.begin(), handles.end(), [](SoLoud::handle handle) {
                          return !gAudioSystem.soloud.isValidVoiceHandle(handle);
                      }),
                      handles.end());
    }
}

static bool effect_has_active_voice_locked(const std::string &key)
{
    auto handles = gSoundManager.handles.find(key);
    return handles != gSoundManager.handles.end() && !handles->second.empty();
}

static void trim_effect_cache_locked()
{
    cleanup_finished_effect_handles_locked();

    while (gSoundManager.sounds.size() >= MAX_CACHED_EFFECTS)
    {
        auto victim = gSoundManager.sounds.end();
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (auto it = gSoundManager.sounds.begin(); it != gSoundManager.sounds.end(); ++it)
        {
            if (effect_has_active_voice_locked(it->first))
                continue;

            const auto used = gSoundManager.lastUsed.find(it->first);
            const uint64_t age = used == gSoundManager.lastUsed.end() ? 0 : used->second;
            if (victim == gSoundManager.sounds.end() || age < oldest)
            {
                victim = it;
                oldest = age;
            }
        }

        // All cached sounds are currently playing. Temporarily exceeding the
        // cache cap is safer than deleting a source used by an active voice.
        if (victim == gSoundManager.sounds.end())
            break;

        DLA_DEBUG_PRINTF("[Audio][CACHE] evict inactive effect \"%s\"\n", victim->first.c_str());
        delete victim->second;
        gSoundManager.handles.erase(victim->first);
        gSoundManager.lastUsed.erase(victim->first);
        gSoundManager.sounds.erase(victim);
    }
}

static bool cache_effect_locked(const std::string &key, bool looping, bool log_errors)
{
    if (key.empty())
        return false;

    auto it = gSoundManager.sounds.find(key);
    if (it != gSoundManager.sounds.end())
        return true;

    trim_effect_cache_locked();

    auto *wav = new (std::nothrow) SoLoud::Wav();
    if (!wav)
    {
        if (log_errors)
            l_error("playEffect: out of memory creating source for %s", key.c_str());
        return false;
    }

    std::string resolved;
    const SoLoud::result res = audio_load_asset(*wav, key, resolved, log_errors);
    if (res != SoLoud::SO_NO_ERROR)
    {
        if (log_errors)
        {
            l_error("playEffect: failed to load %s: %s (%d)", key.c_str(),
                    gAudioSystem.soloud.getErrorString(res), res);
        }
        delete wav;
        return false;
    }

    wav->setLooping(looping);
    gSoundManager.sounds[key] = wav;
    gSoundManager.lastUsed[key] = ++gSoundManager.useCounter;
    DLA_DEBUG_PRINTF("[Audio][CACHE] decoded effect \"%s\" from \"%s\"\n",
                     key.c_str(), resolved.c_str());
    return true;
}

static bool warm_effect_locked(const std::string &key)
{
#if DLA_AUDIO_WARMUP_EFFECTS
    if (MAX_WARMED_EFFECTS == 0 || gSoundManager.warmedEffects >= MAX_WARMED_EFFECTS)
        return false;

    const bool alreadyCached = gSoundManager.sounds.find(key) != gSoundManager.sounds.end();
    if (!cache_effect_locked(key, false, false))
        return false;

    if (!alreadyCached)
    {
        ++gSoundManager.warmedEffects;
        DLA_DEBUG_PRINTF("[Audio][WARMUP] %u/%u \"%s\"\n",
                         (unsigned)gSoundManager.warmedEffects,
                         (unsigned)MAX_WARMED_EFFECTS,
                         key.c_str());
    }
    return true;
#else
    (void)key;
    return false;
#endif
}

static bool should_warm_effect(const std::string &relative)
{
#if DLA_AUDIO_WARMUP_EFFECTS
    const std::string key = audio_lower(relative);
    static const char *const tokens[] = {
        "hero_jump",
        "hero_fall",
        "sword_attack",
        "sword_block",
        "goblin_hit",
        "orc_hit",
        "skeleton_hit",
        "zombie_hit",
        "scorpion_attack",
        "wasp_attack",
        "yeti_zombie_attack",
        "minotaur_horns_attack",
        "minotaur_prepare_attack",
        "minotaur_run_attack",
        "electriceye_attack",
        "electriceye_feeler_attack",
        "electriceye_ray"};

    for (const char *token : tokens)
    {
        if (key.find(token) != std::string::npos)
            return true;
    }
#else
    (void)relative;
#endif
    return false;
}

static void queue_warm_effect_locked(const std::string &key)
{
#if DLA_AUDIO_WARMUP_EFFECTS
    if (key.empty() || MAX_WARMED_EFFECTS == 0 ||
        gSoundManager.warmedEffects >= MAX_WARMED_EFFECTS ||
        gSoundManager.sounds.find(key) != gSoundManager.sounds.end() ||
        std::find(gSoundManager.warmQueue.begin(), gSoundManager.warmQueue.end(), key) != gSoundManager.warmQueue.end())
        return;

    gSoundManager.warmQueue.push_back(key);
    DLA_DEBUG_PRINTF("[Audio][WARMUP] queued \"%s\"\n", key.c_str());
#else
    (void)key;
#endif
}

static std::string next_warmup_effect_locked(size_t *coreIndex)
{
#if DLA_AUDIO_WARMUP_EFFECTS
    if (!gSoundManager.warmQueue.empty())
    {
        std::string key = gSoundManager.warmQueue.front();
        gSoundManager.warmQueue.erase(gSoundManager.warmQueue.begin());
        return key;
    }

    if (coreIndex && *coreIndex < CORE_WARMUP_EFFECT_COUNT)
        return kCoreWarmupEffects[(*coreIndex)++];
#else
    (void)coreIndex;
#endif
    return {};
}

static void audio_warmup_delay(unsigned int usec)
{
    if (usec > 0)
        sceKernelDelayThread(usec);
}

static void *audio_warmup_thread_main(void *)
{
#if DLA_AUDIO_WARMUP_EFFECTS && DLA_AUDIO_WARMUP_ASYNC
    size_t coreIndex = 0;
    int idlePasses = 0;

    audio_warmup_delay(DLA_AUDIO_WARMUP_START_DELAY_US);

    while (!gAudioWarmupStop)
    {
        std::string effect;
        {
            std::unique_lock<std::mutex> lock(gSoundManager.mutex, std::try_to_lock);
            if (lock.owns_lock())
            {
                effect = next_warmup_effect_locked(&coreIndex);
                if (!effect.empty())
                    warm_effect_locked(effect);
            }
        }

        if (effect.empty())
        {
            if (coreIndex >= CORE_WARMUP_EFFECT_COUNT && ++idlePasses > 80)
                break;
        }
        else
        {
            idlePasses = 0;
        }

        audio_warmup_delay(DLA_AUDIO_WARMUP_STEP_DELAY_US);
    }
#endif
    return nullptr;
}

static void start_audio_warmup_thread()
{
#if DLA_AUDIO_WARMUP_EFFECTS && DLA_AUDIO_WARMUP_ASYNC
    if (gAudioWarmupThreadStarted)
        return;

    gAudioWarmupStop = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    const int rc = pthread_create(&gAudioWarmupThread, &attr, audio_warmup_thread_main, nullptr);
    pthread_attr_destroy(&attr);
    if (rc == 0)
    {
        gAudioWarmupThreadStarted = true;
        DLA_DEBUG_PRINTF("[Audio][WARMUP] async worker started\n");
    }
    else
    {
        DLA_ERROR_PRINTF("[Audio][WARMUP][ERROR] failed to start async worker (%d)\n", rc);
    }
#endif
}

static void stop_audio_warmup_thread()
{
#if DLA_AUDIO_WARMUP_EFFECTS && DLA_AUDIO_WARMUP_ASYNC
    if (!gAudioWarmupThreadStarted)
        return;

    gAudioWarmupStop = true;
    pthread_join(gAudioWarmupThread, nullptr);
    gAudioWarmupThreadStarted = false;
#endif
}

void audio_init()
{
    std::lock_guard<std::mutex> lock(gAudioSystem.mutex);

    l_debug("[Audio] Initializing SoLoud...");
    SoLoud::result res = gAudioSystem.soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF,
        SoLoud::Soloud::VITA_HOMEBREW,
        44100,
        2048,
        2);
    if (res != SoLoud::SO_NO_ERROR)
    {
        l_error("[Audio] Failed to initialize SoLoud: %s",
                gAudioSystem.soloud.getErrorString(res));
        return;
    }

    gAudioSystem.soloud.setGlobalVolume(1.0f);
    gAudioSystem.soloud.setMaxActiveVoiceCount(64);
    gAudioSystem.soloud.setVisualizationEnable(false);

    {
        std::lock_guard<std::mutex> soundLock(gSoundManager.mutex);
        gSoundManager.sounds.clear();
        gSoundManager.handles.clear();
        gSoundManager.lastUsed.clear();
        gSoundManager.warmQueue.clear();
        gSoundManager.useCounter = 0;
        gSoundManager.warmedEffects = 0;
        gSoundManager.volumeLeft = 0.5f;
        gSoundManager.volumeRight = 0.5f;
    }

    gMusicManager.hasBgmLoaded = false;
    gMusicManager.volumeLeft = 0.5f;
    gMusicManager.volumeRight = 0.5f;
    gMusicManager.currentPath.clear();
    gMusicManager.handle = 0;

    start_audio_warmup_thread();

    l_debug("[Audio] SoLoud initialized successfully.");
}

void audio_destroy()
{
    std::lock_guard<std::mutex> lock(gAudioSystem.mutex);

    stop_audio_warmup_thread();

    for (auto &pair : gSoundManager.handles)
    {
        for (auto h : pair.second)
            gAudioSystem.soloud.stop(h);
    }
    gSoundManager.handles.clear();
    gSoundManager.lastUsed.clear();
    gSoundManager.warmQueue.clear();
    gSoundManager.useCounter = 0;
    gSoundManager.warmedEffects = 0;

    for (auto &pair : gSoundManager.sounds)
        delete pair.second;
    gSoundManager.sounds.clear();

    gAudioSystem.soloud.stopAll();
    gMusicManager.bgm.stop();
    gMusicManager.hasBgmLoaded = false;
    gMusicManager.currentPath.clear();

    gAudioSystem.soloud.deinit();

    l_debug("[Audio] SoLoud destroyed.");
}

// sound
void preloadEffect(jmethodID, va_list args)
{
    jstring jpath = va_arg(args, jstring);
    const char *relPath = jni.GetStringUTFChars(jpath, nullptr);
    std::string relative(relPath ? relPath : "");
    jni.ReleaseStringUTFChars(jpath, relPath);

    if (relative.empty())
        return;

    // Android preloads the complete effect catalog. SoLoud::Wav expands every
    // Ogg to PCM, which exhausts Vita memory before gameplay starts. On Vita we
    // only warm tiny/high-frequency gameplay sounds so jumps and attacks do not
    // decode on the first action frame.
    if (should_warm_effect(relative))
    {
        std::lock_guard<std::mutex> lock(gSoundManager.mutex);
#if DLA_AUDIO_SYNC_PRELOAD_EFFECTS || !DLA_AUDIO_WARMUP_ASYNC
        warm_effect_locked(relative);
#else
        queue_warm_effect_locked(relative);
#endif
    }
    else
    {
        DLA_DEBUG_PRINTF("[Audio][WARMUP] deferred \"%s\"\n", relative.c_str());
    }
}

void unloadEffect(jmethodID, va_list args)
{
    jstring jpath = va_arg(args, jstring);
    const char *path = jni.GetStringUTFChars(jpath, nullptr);
    std::string key(path ? path : "");
    jni.ReleaseStringUTFChars(jpath, path);

    std::lock_guard<std::mutex> lock(gSoundManager.mutex);
    auto it = gSoundManager.sounds.find(key);
    if (it != gSoundManager.sounds.end())
    {

        auto &handles = gSoundManager.handles[key];
        for (auto h : handles)
        {
            gAudioSystem.soloud.stop(h);
        }
        handles.clear();
        delete it->second;
        gSoundManager.sounds.erase(it);
        gSoundManager.lastUsed.erase(key);
        gSoundManager.handles.erase(key);
    }
}

jint playEffect(jmethodID, va_list args)
{
    jstring jpath = va_arg(args, jstring);
    jboolean isLoop = va_arg(args, jint);
    jfloat volume = va_arg(args, jdouble);
    jfloat rate = va_arg(args, jdouble);

    const char *relPath = jni.GetStringUTFChars(jpath, nullptr);
    std::string relative(relPath ? relPath : "");
    jni.ReleaseStringUTFChars(jpath, relPath);

    if (relative.empty())
        return -1;

    const std::string key = relative;
    l_debug("playEffect: called for %s (loop=%d, vol=%.2f, rate=%.2f)", key.c_str(), isLoop, volume, rate);

    std::lock_guard<std::mutex> lock(gSoundManager.mutex);
    auto it = gSoundManager.sounds.find(key);
    if (it == gSoundManager.sounds.end())
    {
        l_debug("playEffect: sound not preloaded, attempting to load %s", key.c_str());
        if (!cache_effect_locked(relative, isLoop, true))
            return -1;

        it = gSoundManager.sounds.find(key);
        l_debug("playEffect: loaded on-demand %s", key.c_str());
    }

    gSoundManager.lastUsed[key] = ++gSoundManager.useCounter;

    volume = std::fmax(0.0f, std::fmin(volume, 1.0f));
    rate = std::fmax(0.5f, std::fmin(rate, 2.0f));

    auto *wav = it->second;
    wav->setLooping(isLoop);
    SoLoud::handle handle = gAudioSystem.soloud.play(*wav, volume, 0, true, 0);
    if (handle == 0)
    {
        l_debug("playEffect: failed to play %s", key.c_str());
        return -1;
    }

    gSoundManager.handles[key].push_back(handle);

    gAudioSystem.soloud.setPause(handle, false);

    l_debug("playEffect: playing %s (handle=%d)", key.c_str(), handle);

    return handle;
}

void setEffectVolume(jmethodID, va_list args)
{
    jint streamID = va_arg(args, jint);
    jdouble volume = va_arg(args, jdouble);
    l_debug("setEffectVolume called for stream %d to volume %.2f", streamID, volume);
    volume = std::fmax(0.0f, std::fmin(volume, 1.0f));
    gAudioSystem.soloud.setVolume(streamID, volume);
}

void setEffectRate(jmethodID, va_list args)
{
    jint streamID = va_arg(args, jint);
    jdouble rate = va_arg(args, jdouble);
    l_debug("setEffectRate called for stream %d to rate %.2f", streamID, rate);
    rate = std::fmax(0.5f, std::fmin(rate, 2.0f));
    gAudioSystem.soloud.setRelativePlaySpeed(streamID, rate);
}

void stopEffect(jmethodID, va_list args)
{
    jint streamID = va_arg(args, jint);
    l_debug("stopEffect called for stream %d", streamID);
    gAudioSystem.soloud.stop(streamID);

    std::lock_guard<std::mutex> lock(gSoundManager.mutex);
    for (auto &pair : gSoundManager.handles)
    {
        auto &vec = pair.second;
        vec.erase(std::remove(vec.begin(), vec.end(), streamID), vec.end());
    }
}

void pauseEffect(jmethodID, va_list args)
{
    jint streamID = va_arg(args, jint);
    l_debug("pauseEffect called for stream %d", streamID);
    gAudioSystem.soloud.setPause(streamID, true);
}

void resumeEffect(jmethodID, va_list args)
{
    jint streamID = va_arg(args, jint);
    l_debug("resumeEffect called for stream %d", streamID);
    gAudioSystem.soloud.setPause(streamID, false);
}

void pauseAllEffects(jmethodID, va_list)
{
    gAudioSystem.soloud.setPauseAll(true);
}

void resumeAllEffects(jmethodID, va_list)
{
    gAudioSystem.soloud.setPauseAll(false);
}

void stopAllEffects(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gSoundManager.mutex);
    for (auto &pair : gSoundManager.handles)
    {
        for (auto h : pair.second)
        {
            gAudioSystem.soloud.stop(h);
        }
        pair.second.clear();
    }
}

jfloat getEffectsVolume(jmethodID, va_list)
{
    return (gSoundManager.volumeLeft + gSoundManager.volumeRight) * 0.5f;
}

void setEffectsVolume(jmethodID, va_list args)
{
    jdouble volume = va_arg(args, jdouble);
    volume = std::fmax(0.0f, std::fmin(volume, 1.0f));
    l_debug("setEffectsVolume called to volume %.2f", volume);
    std::lock_guard<std::mutex> lock(gSoundManager.mutex);
    gSoundManager.volumeLeft = gSoundManager.volumeRight = volume;
    for (auto &pair : gSoundManager.handles)
    {
        for (auto h : pair.second)
        {
            gAudioSystem.soloud.setVolume(h, volume);
        }
    }
}

// music

void preloadBackgroundMusic(jmethodID, va_list args)
{
    jstring jpath = va_arg(args, jstring);
    const char *relPath = jni.GetStringUTFChars(jpath, nullptr);
    std::string relative(relPath ? relPath : "");
    jni.ReleaseStringUTFChars(jpath, relPath);

    if (relative.empty())
        return;

    std::lock_guard<std::mutex> lock(gMusicManager.mutex);

    if (gMusicManager.currentPath == relative && gMusicManager.hasBgmLoaded)
    {
        l_debug("preloadBackgroundMusic: already loaded %s", relative.c_str());
        return;
    }

    gMusicManager.bgm.stop();
    gMusicManager.currentPath.clear();

    std::string resolved;
    const SoLoud::result result = audio_load_asset(gMusicManager.bgm, relative, resolved);
    if (result != SoLoud::SO_NO_ERROR)
    {
        l_error("preloadBackgroundMusic: failed to load %s: %s (%d)", relative.c_str(),
                gAudioSystem.soloud.getErrorString(result), result);
        gMusicManager.hasBgmLoaded = false;
        return;
    }

    gMusicManager.currentPath = relative;
    gMusicManager.hasBgmLoaded = true;
    l_debug("preloadBackgroundMusic: loaded %s from %s", relative.c_str(), resolved.c_str());
}

void playBackgroundMusic(jmethodID, va_list args)
{
    jstring jpath = va_arg(args, jstring);
    jboolean isLoop = va_arg(args, jint);

    const char *relPath = jni.GetStringUTFChars(jpath, nullptr);
    std::string relative(relPath ? relPath : "");
    jni.ReleaseStringUTFChars(jpath, relPath);

    if (relative.empty())
        return;

    std::lock_guard<std::mutex> lock(gMusicManager.mutex);

    if (!gMusicManager.hasBgmLoaded || gMusicManager.currentPath != relative)
    {
        gMusicManager.bgm.stop();
        std::string resolved;
        const SoLoud::result result = audio_load_asset(gMusicManager.bgm, relative, resolved);
        if (result != SoLoud::SO_NO_ERROR)
        {
            l_error("playBackgroundMusic: failed to load %s: %s (%d)", relative.c_str(),
                    gAudioSystem.soloud.getErrorString(result), result);
            gMusicManager.hasBgmLoaded = false;
            gMusicManager.currentPath.clear();
            return;
        }
        gMusicManager.currentPath = relative;
        gMusicManager.hasBgmLoaded = true;
        l_debug("playBackgroundMusic: loaded %s from %s", relative.c_str(), resolved.c_str());
    }

    if (isLoop)
        gMusicManager.bgm.mFlags |= SoLoud::AudioSource::SHOULD_LOOP;
    else
        gMusicManager.bgm.mFlags &= ~SoLoud::AudioSource::SHOULD_LOOP;

    gMusicManager.handle = gAudioSystem.soloud.playBackground(gMusicManager.bgm);
    gAudioSystem.soloud.setVolume(gMusicManager.handle,
                                  (gMusicManager.volumeLeft + gMusicManager.volumeRight) / 2.0f);

    l_debug("playBackgroundMusic: playing %s (loop=%d)", relative.c_str(), isLoop);
}

void stopBackgroundMusic(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    if (gMusicManager.hasBgmLoaded)
    {
        gAudioSystem.soloud.stopAll();
        l_debug("stopBackgroundMusic: stopped");
    }
}

void pauseBackgroundMusic(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    if (gMusicManager.handle)
    {
        gAudioSystem.soloud.setPause(gMusicManager.handle, true);
        l_debug("pauseBackgroundMusic");
    }
}

void resumeBackgroundMusic(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    if (gMusicManager.handle)
    {
        gAudioSystem.soloud.setPause(gMusicManager.handle, false);
        l_debug("resumeBackgroundMusic");
    }
}

void rewindBackgroundMusic(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    if (gMusicManager.hasBgmLoaded)
    {
        gAudioSystem.soloud.stopAll();
        gMusicManager.handle = gAudioSystem.soloud.play(gMusicManager.bgm);
        l_debug("rewindBackgroundMusic");
    }
}

jboolean isBackgroundMusicPlaying(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    if (!gMusicManager.hasBgmLoaded)
        return JNI_FALSE;

    bool playing = gAudioSystem.soloud.isValidVoiceHandle(gMusicManager.handle) &&
                   gAudioSystem.soloud.getPause(gMusicManager.handle) == 0;
    return playing ? JNI_TRUE : JNI_FALSE;
}

void endBackgroundMusic(jmethodID, va_list)
{
    stop_audio_warmup_thread();

    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    gAudioSystem.soloud.stopAll();
    gMusicManager.bgm.stop();
    gMusicManager.hasBgmLoaded = false;
    gMusicManager.currentPath.clear();
    gAudioSystem.soloud.deinit();
    l_debug("endBackgroundMusic: deinitialized SoLoud");
}

jfloat getBackgroundVolume(jmethodID, va_list)
{
    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    return (gMusicManager.volumeLeft + gMusicManager.volumeRight) * 0.5f;
}

void setBackgroundVolume(jmethodID, va_list args)
{
    jfloat volume = va_arg(args, jdouble);
    volume = std::fmax(0.0f, std::fmin(1.0f, volume));

    std::lock_guard<std::mutex> lock(gMusicManager.mutex);
    gMusicManager.volumeLeft = gMusicManager.volumeRight = volume;
    if (gMusicManager.hasBgmLoaded && gMusicManager.handle)
    {
        gAudioSystem.soloud.setVolume(gMusicManager.handle, volume);
    }
    l_debug("setBackgroundVolume: %.2f", volume);
}
