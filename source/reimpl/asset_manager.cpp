#include "reimpl/asset_manager.h"
#include "debug_log.h"
#include "reimpl/zlib_compat.h"
#include "utils/logger.h"

#include <pthread.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <algorithm>
#include <map>
#include <set>
#include <psp2/kernel/clib.h>
#include <libc_bridge/libc_bridge.h>
#include <string>
#include <fcntl.h>
#include <vector>
#include <dirent.h>

typedef struct assetManager {
    int dummy = 0; // TODO: mb we will need to store something here in future
    pthread_mutex_t mLock;
} assetManager;

typedef struct aAsset {
    char * filename = nullptr;
    FILE* f = nullptr;
    size_t bytesRead = 0;
    size_t fileSize = 0;
    bool opened = false;
    bool memoryBacked = false;
    uint8_t *buffer = nullptr;
} asset;

typedef struct assetDirState {
    std::vector<std::string> entries;
    size_t index = 0;
} assetDirState;

typedef struct apkEntry {
    uint32_t localHeaderOffset;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t compressionMethod;
} apkEntry;

static AAssetManager * g_AAssetManager = nullptr;
static std::string g_apk_path = DATA_PATH "base.apk";
static std::map<std::string, apkEntry> g_apk_entries;
static std::map<std::string, std::string> g_apk_lower_names;
static bool g_apk_index_ready = false;
static bool g_apk_index_failed = false;
static pthread_mutex_t g_apk_lock = PTHREAD_MUTEX_INITIALIZER;
static std::map<std::string, std::string> g_loose_file_index;
static std::map<std::string, std::set<std::string>> g_loose_dir_index;
static bool g_loose_index_ready = false;
static pthread_mutex_t g_loose_index_lock = PTHREAD_MUTEX_INITIALIZER;
static std::map<std::string, std::string> g_asset_resolved_cache;
static std::set<std::string> g_asset_negative_cache;
static pthread_mutex_t g_asset_lookup_cache_lock = PTHREAD_MUTEX_INITIALIZER;

static bool asset_path_is_absolute(const char *filename) {
    return filename && (strstr(filename, ":/") || filename[0] == '/');
}

static void asset_push_unique(std::vector<std::string> *values, const std::string &value) {
    if (!values || value.empty())
        return;
    if (std::find(values->begin(), values->end(), value) == values->end())
        values->push_back(value);
}

static std::string asset_normalize_relative_path(const char *filename) {
    std::string rel(filename ? filename : "");
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (rel.rfind("./", 0) == 0)
        rel.erase(0, 2);
    while (!rel.empty() && rel[0] == '/')
        rel.erase(0, 1);
    return rel;
}

static std::string asset_lower_path(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    for (char &ch : value)
        ch = (char)std::tolower((unsigned char)ch);
    return value;
}

static std::string asset_join_path(const std::string &root, const std::string &rel) {
    if (root.empty())
        return rel;
    if (rel.empty())
        return root;
    if (root.back() == '/')
        return root + rel;
    return root + "/" + rel;
}

static std::string asset_ensure_trailing_slash(std::string path) {
    if (!path.empty() && path.back() != '/')
        path.push_back('/');
    return path;
}

static bool asset_dir_exists(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (!dir)
        return false;
    closedir(dir);
    return true;
}

static void asset_push_existing_dir(std::vector<std::string> *roots, const std::string &path) {
    if (!roots)
        return;
    const std::string root = asset_ensure_trailing_slash(path);
    if (asset_dir_exists(root))
        asset_push_unique(roots, root);
}

static std::vector<std::string> asset_loose_roots(void) {
    std::vector<std::string> roots;
    asset_push_unique(&roots, std::string(DATA_PATH) + "assets/");
    asset_push_unique(&roots, std::string(DATA_PATH) + "assets/assets/");
    asset_push_unique(&roots, std::string(DATA_PATH) + "base/assets/");
    asset_push_unique(&roots, std::string(DATA_PATH) + "base.apk/assets/");
    return roots;
}

static size_t asset_fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fread(ptr, size, nmemb, f);
#else
    return fread(ptr, size, nmemb, f);
#endif
}

static int asset_fseek(FILE *f, long offset, int whence) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fseek(f, offset, whence);
#else
    return fseek(f, offset, whence);
#endif
}

static long asset_ftell(FILE *f) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_ftell(f);
#else
    return ftell(f);
#endif
}

static int asset_fclose(FILE *f) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fclose(f);
#else
    return fclose(f);
#endif
}

static int asset_feof(FILE *f) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_feof(f);
#else
    return feof(f);
#endif
}

static FILE *asset_try_fopen(const char *path) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fopen(path, "rb");
#else
    return fopen(path, "rb");
#endif
}

static std::string asset_normalize_index_key(const std::string &key) {
    std::string normalized = asset_normalize_relative_path(key.c_str());
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();
    return normalized;
}

static std::string asset_lookup_cache_key(const char *filename) {
    if (!filename || !filename[0])
        return {};
    if (asset_path_is_absolute(filename))
        return filename;
    return asset_lower_path(asset_normalize_index_key(filename));
}

static bool asset_lookup_cache_get(const char *filename, std::string *resolved_path, bool *negative) {
    if (resolved_path)
        resolved_path->clear();
    if (negative)
        *negative = false;

    const std::string key = asset_lookup_cache_key(filename);
    if (key.empty())
        return false;

    pthread_mutex_lock(&g_asset_lookup_cache_lock);
    const auto hit = g_asset_resolved_cache.find(key);
    if (hit != g_asset_resolved_cache.end()) {
        if (resolved_path)
            *resolved_path = hit->second;
        pthread_mutex_unlock(&g_asset_lookup_cache_lock);
        return true;
    }

    if (g_asset_negative_cache.find(key) != g_asset_negative_cache.end()) {
        if (negative)
            *negative = true;
        pthread_mutex_unlock(&g_asset_lookup_cache_lock);
        return true;
    }

    pthread_mutex_unlock(&g_asset_lookup_cache_lock);
    return false;
}

static void asset_lookup_cache_store_file(const char *filename, const std::string &resolved_path) {
    const std::string key = asset_lookup_cache_key(filename);
    if (key.empty() || resolved_path.empty())
        return;

    pthread_mutex_lock(&g_asset_lookup_cache_lock);
    g_asset_negative_cache.erase(key);
    g_asset_resolved_cache[key] = resolved_path;
    pthread_mutex_unlock(&g_asset_lookup_cache_lock);
}

static void asset_lookup_cache_store_negative(const char *filename) {
    const std::string key = asset_lookup_cache_key(filename);
    if (key.empty())
        return;

    pthread_mutex_lock(&g_asset_lookup_cache_lock);
    g_asset_resolved_cache.erase(key);
    g_asset_negative_cache.insert(key);
    pthread_mutex_unlock(&g_asset_lookup_cache_lock);
}

static void asset_lookup_cache_forget(const char *filename) {
    const std::string key = asset_lookup_cache_key(filename);
    if (key.empty())
        return;

    pthread_mutex_lock(&g_asset_lookup_cache_lock);
    g_asset_resolved_cache.erase(key);
    g_asset_negative_cache.erase(key);
    pthread_mutex_unlock(&g_asset_lookup_cache_lock);
}

static void asset_lookup_cache_clear(void) {
    pthread_mutex_lock(&g_asset_lookup_cache_lock);
    g_asset_resolved_cache.clear();
    g_asset_negative_cache.clear();
    pthread_mutex_unlock(&g_asset_lookup_cache_lock);
}

static void asset_loose_index_add_dir_entries_locked(const std::string &key) {
    const std::string normalized = asset_normalize_index_key(key);
    if (normalized.empty())
        return;

    std::string dir_key;
    size_t start = 0;
    while (start < normalized.size()) {
        const size_t slash = normalized.find('/', start);
        const std::string segment = normalized.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!segment.empty())
            g_loose_dir_index[asset_lower_path(dir_key)].insert(segment);

        if (slash == std::string::npos)
            break;
        if (!segment.empty())
            dir_key = dir_key.empty() ? segment : dir_key + "/" + segment;
        start = slash + 1;
    }
}

static void asset_loose_index_add_alias_locked(const std::string &key, const std::string &path, bool is_file) {
    const std::string normalized = asset_normalize_index_key(key);
    if (normalized.empty())
        return;

    asset_loose_index_add_dir_entries_locked(normalized);
    if (!is_file)
        return;

    const std::string lower_key = asset_lower_path(normalized);
    if (g_loose_file_index.find(lower_key) == g_loose_file_index.end())
        g_loose_file_index[lower_key] = path;
}

static void asset_loose_index_add_marker_aliases_locked(const std::string &normalized,
                                                        const std::string &lower,
                                                        const std::string &marker,
                                                        const std::string &path,
                                                        bool is_file) {
    size_t pos = lower.find(marker);
    while (pos != std::string::npos) {
        const size_t alias_start = pos + marker.size();
        if (alias_start < normalized.size())
            asset_loose_index_add_alias_locked(normalized.substr(alias_start), path, is_file);
        pos = lower.find(marker, pos + 1);
    }
}

static void asset_loose_index_add_aliases_locked(const std::string &rel, const std::string &path, bool is_file) {
    const std::string normalized = asset_normalize_index_key(rel);
    if (normalized.empty())
        return;

    const std::string lower = asset_lower_path(normalized);
    asset_loose_index_add_alias_locked(normalized, path, is_file);

    if (lower.rfind("assets/", 0) == 0)
        asset_loose_index_add_alias_locked(normalized.substr(7), path, is_file);
    if (lower.rfind("res/", 0) == 0)
        asset_loose_index_add_alias_locked(normalized.substr(4), path, is_file);
    if (lower.rfind("resources/", 0) == 0)
        asset_loose_index_add_alias_locked(normalized.substr(10), path, is_file);

    asset_loose_index_add_marker_aliases_locked(normalized, lower, "/assets/", path, is_file);
    asset_loose_index_add_marker_aliases_locked(normalized, lower, "/res/", path, is_file);
    asset_loose_index_add_marker_aliases_locked(normalized, lower, "/resources/", path, is_file);
}

static void asset_loose_index_scan_dir_locked(const std::string &root,
                                              const std::string &rel,
                                              int depth,
                                              int *visited_dirs,
                                              int *indexed_files) {
    static const int kMaxDepth = 12;
    static const int kMaxVisitedDirs = 4096;
    static const int kMaxIndexedFiles = 50000;
    if (!visited_dirs || !indexed_files || depth > kMaxDepth ||
        *visited_dirs >= kMaxVisitedDirs || *indexed_files >= kMaxIndexedFiles)
        return;

    DIR *dir = opendir(root.c_str());
    if (!dir)
        return;
    (*visited_dirs)++;

    for (dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (*visited_dirs >= kMaxVisitedDirs || *indexed_files >= kMaxIndexedFiles)
            break;

        const std::string full_path = asset_join_path(root, entry->d_name);
        const std::string child_rel = rel.empty() ? std::string(entry->d_name) : rel + "/" + entry->d_name;

        DIR *child = opendir(full_path.c_str());
        if (child) {
            closedir(child);
            asset_loose_index_add_aliases_locked(child_rel, full_path, false);
            if (depth < kMaxDepth)
                asset_loose_index_scan_dir_locked(full_path, child_rel, depth + 1, visited_dirs, indexed_files);
        } else {
            asset_loose_index_add_aliases_locked(child_rel, full_path, true);
            (*indexed_files)++;
        }
    }

    closedir(dir);
}

static void asset_loose_index_discover_roots(std::vector<std::string> *roots) {
    if (!roots)
        return;

    DIR *data_dir = opendir(DATA_PATH);
    if (!data_dir)
        return;

    for (dirent *entry = readdir(data_dir); entry; entry = readdir(data_dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        const std::string top_path = asset_join_path(DATA_PATH, entry->d_name);
        DIR *top_dir = opendir(top_path.c_str());
        if (!top_dir)
            continue;

        const std::string top_lower = asset_lower_path(entry->d_name);
        if (top_lower == "assets" || top_lower == "res" || top_lower == "resources")
            asset_push_unique(roots, asset_ensure_trailing_slash(top_path));

        for (dirent *child = readdir(top_dir); child; child = readdir(top_dir)) {
            if (strcmp(child->d_name, ".") == 0 || strcmp(child->d_name, "..") == 0)
                continue;
            const std::string child_lower = asset_lower_path(child->d_name);
            const std::string child_path = asset_join_path(top_path, child->d_name);
            if (child_lower == "assets" || child_lower == "res" || child_lower == "resources") {
                asset_push_existing_dir(roots, child_path);
                continue;
            }

            DIR *grandchild_dir = opendir(child_path.c_str());
            if (!grandchild_dir)
                continue;

            for (dirent *grandchild = readdir(grandchild_dir); grandchild; grandchild = readdir(grandchild_dir)) {
                if (strcmp(grandchild->d_name, ".") == 0 || strcmp(grandchild->d_name, "..") == 0)
                    continue;
                const std::string grandchild_lower = asset_lower_path(grandchild->d_name);
                if (grandchild_lower == "assets" || grandchild_lower == "res" || grandchild_lower == "resources")
                    asset_push_existing_dir(roots, asset_join_path(child_path, grandchild->d_name));
            }

            closedir(grandchild_dir);
        }

        closedir(top_dir);
    }

    closedir(data_dir);
}

static std::vector<std::string> asset_loose_index_roots(void) {
    std::vector<std::string> roots;
    for (const std::string &root : asset_loose_roots())
        asset_push_existing_dir(&roots, root);
    asset_loose_index_discover_roots(&roots);
    return roots;
}

static bool asset_loose_index_build_locked(void) {
    if (g_loose_index_ready)
        return true;

    g_loose_file_index.clear();
    g_loose_dir_index.clear();

    int visited_dirs = 0;
    int indexed_files = 0;
    const std::vector<std::string> roots = asset_loose_index_roots();
    for (const std::string &root : roots)
        asset_loose_index_scan_dir_locked(root, "", 0, &visited_dirs, &indexed_files);

    g_loose_index_ready = true;
    DLA_DEBUG_PRINTF("[asset] indexed %u loose files, %u loose dirs from %u roots (visited %u dirs)\n",
                  (unsigned)g_loose_file_index.size(),
                  (unsigned)g_loose_dir_index.size(),
                  (unsigned)roots.size(),
                  (unsigned)visited_dirs);
    return true;
}

static std::vector<std::string> asset_build_lookup_keys(const char *filename) {
    std::vector<std::string> keys;
    if (!filename || !filename[0] || asset_path_is_absolute(filename))
        return keys;

    const std::string rel = asset_normalize_relative_path(filename);
    asset_push_unique(&keys, rel);
    asset_push_unique(&keys, "assets/" + rel);
    if (rel.rfind("assets/", 0) == 0)
        asset_push_unique(&keys, rel.substr(7));
    return keys;
}

static bool asset_loose_index_find_file(const char *filename, std::string *resolved_path) {
    if (!filename || !filename[0] || !resolved_path || asset_path_is_absolute(filename))
        return false;

    pthread_mutex_lock(&g_loose_index_lock);
    asset_loose_index_build_locked();

    const std::vector<std::string> keys = asset_build_lookup_keys(filename);
    for (const std::string &key : keys) {
        const auto it = g_loose_file_index.find(asset_lower_path(asset_normalize_index_key(key)));
        if (it != g_loose_file_index.end()) {
            *resolved_path = it->second;
            pthread_mutex_unlock(&g_loose_index_lock);
            return true;
        }
    }

    pthread_mutex_unlock(&g_loose_index_lock);
    return false;
}

static void asset_loose_index_collect_dir_entries(const char *dir_name, std::set<std::string> *names) {
    if (!names || asset_path_is_absolute(dir_name))
        return;

    pthread_mutex_lock(&g_loose_index_lock);
    asset_loose_index_build_locked();

    std::vector<std::string> keys;
    if (!dir_name || !dir_name[0]) {
        keys.emplace_back("");
    } else {
        keys = asset_build_lookup_keys(dir_name);
    }

    for (const std::string &key : keys) {
        const auto it = g_loose_dir_index.find(asset_lower_path(asset_normalize_index_key(key)));
        if (it == g_loose_dir_index.end())
            continue;
        names->insert(it->second.begin(), it->second.end());
    }

    pthread_mutex_unlock(&g_loose_index_lock);
}

static std::vector<std::string> asset_build_candidates(const char *filename) {
    std::vector<std::string> candidates;
    if (!filename || !filename[0])
        return candidates;

    if (asset_path_is_absolute(filename)) {
        candidates.emplace_back(filename);
        return candidates;
    }

    const std::string rel = asset_normalize_relative_path(filename);
    std::vector<std::string> rels;
    asset_push_unique(&rels, rel);
    if (rel.rfind("assets/", 0) == 0)
        asset_push_unique(&rels, rel.substr(7));

    for (const std::string &candidate_rel : rels) {
        for (const std::string &root : asset_loose_roots())
            asset_push_unique(&candidates, asset_join_path(root, candidate_rel));
        asset_push_unique(&candidates, std::string(DATA_PATH) + candidate_rel);
        asset_push_unique(&candidates, std::string(DATA_PATH) + "res/" + candidate_rel);
        asset_push_unique(&candidates, std::string(DATA_PATH) + "Resources/" + candidate_rel);
    }

    return candidates;
}

static std::vector<std::string> asset_build_apk_candidates(const char *filename) {
    std::vector<std::string> candidates;
    if (!filename || !filename[0] || asset_path_is_absolute(filename))
        return candidates;

    const std::string rel = asset_normalize_relative_path(filename);
    asset_push_unique(&candidates, "assets/" + rel);
    asset_push_unique(&candidates, rel);

    if (rel.rfind("assets/", 0) == 0)
        asset_push_unique(&candidates, rel.substr(7));

    return candidates;
}

static void asset_collect_loose_dir_entries(const char *dir_name, std::set<std::string> *names) {
    if (!names)
        return;

    std::vector<std::string> candidates;
    if (!dir_name || !dir_name[0]) {
        candidates = asset_loose_roots();
        asset_push_unique(&candidates, std::string(DATA_PATH));
    } else {
        candidates = asset_build_candidates(dir_name);
    }

    for (const std::string &candidate : candidates) {
        DIR *dir = opendir(candidate.c_str());
        if (!dir)
            continue;

        for (dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            names->insert(entry->d_name);
        }
        closedir(dir);
    }

    if (names->empty())
        asset_loose_index_collect_dir_entries(dir_name, names);
}

static uint16_t asset_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t asset_u32le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static long asset_file_size(FILE *f) {
    if (!f)
        return -1;
    const long cur = asset_ftell(f);
    if (cur < 0)
        return -1;
    if (asset_fseek(f, 0, SEEK_END) != 0)
        return -1;
    const long end = asset_ftell(f);
    if (end < 0)
        return -1;
    if (asset_fseek(f, cur, SEEK_SET) != 0)
        return -1;
    return end;
}

static bool asset_read_exact(FILE *f, void *buf, size_t len) {
    if (len == 0)
        return true;
    return asset_fread(buf, 1, len, f) == len;
}

static void asset_apk_index_reset_locked(void) {
    g_apk_entries.clear();
    g_apk_lower_names.clear();
    g_apk_index_ready = false;
    g_apk_index_failed = false;
}

void asset_manager_set_apk_path(const char *path) {
    pthread_mutex_lock(&g_apk_lock);
    g_apk_path = (path && path[0]) ? path : "";
    asset_apk_index_reset_locked();
    pthread_mutex_unlock(&g_apk_lock);
    asset_lookup_cache_clear();
}

static bool asset_apk_build_index_locked(void) {
    if (g_apk_index_ready)
        return true;
    if (g_apk_index_failed)
        return false;
    if (g_apk_path.empty()) {
        g_apk_index_failed = true;
        return false;
    }

    FILE *apk = asset_try_fopen(g_apk_path.c_str());
    if (!apk) {
        DLA_DEBUG_PRINTF("[asset][WARN] failed to open APK \"%s\"\n", g_apk_path.c_str());
        g_apk_index_failed = true;
        return false;
    }

    const long apk_size = asset_file_size(apk);
    if (apk_size < 22) {
        DLA_DEBUG_PRINTF("[asset][WARN] APK \"%s\" is too small to be a ZIP archive\n", g_apk_path.c_str());
        asset_fclose(apk);
        g_apk_index_failed = true;
        return false;
    }

    const size_t tail_size = (size_t)std::min<long>(apk_size, 0x10000L + 22L);
    std::vector<uint8_t> tail(tail_size);
    if (asset_fseek(apk, apk_size - (long)tail_size, SEEK_SET) != 0 || !asset_read_exact(apk, tail.data(), tail.size())) {
        DLA_DEBUG_PRINTF("[asset][WARN] failed reading ZIP tail from \"%s\"\n", g_apk_path.c_str());
        asset_fclose(apk);
        g_apk_index_failed = true;
        return false;
    }

    ssize_t eocd_pos = -1;
    for (ssize_t i = (ssize_t)tail.size() - 22; i >= 0; --i) {
        if (tail[(size_t)i + 0] == 0x50 &&
            tail[(size_t)i + 1] == 0x4b &&
            tail[(size_t)i + 2] == 0x05 &&
            tail[(size_t)i + 3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }

    if (eocd_pos < 0) {
        DLA_DEBUG_PRINTF("[asset][WARN] ZIP EOCD not found in \"%s\"\n", g_apk_path.c_str());
        asset_fclose(apk);
        g_apk_index_failed = true;
        return false;
    }

    const uint8_t *eocd = tail.data() + eocd_pos;
    const uint16_t entry_count = asset_u16le(eocd + 10);
    const uint32_t central_dir_size = asset_u32le(eocd + 12);
    const uint32_t central_dir_offset = asset_u32le(eocd + 16);
    (void)central_dir_size;

    if (asset_fseek(apk, (long)central_dir_offset, SEEK_SET) != 0) {
        DLA_DEBUG_PRINTF("[asset][WARN] failed seeking to ZIP central directory in \"%s\"\n", g_apk_path.c_str());
        asset_fclose(apk);
        g_apk_index_failed = true;
        return false;
    }

    g_apk_entries.clear();
    g_apk_lower_names.clear();
    for (uint16_t i = 0; i < entry_count; ++i) {
        uint8_t hdr[46];
        if (!asset_read_exact(apk, hdr, sizeof(hdr))) {
            DLA_DEBUG_PRINTF("[asset][WARN] truncated ZIP central directory in \"%s\"\n", g_apk_path.c_str());
            asset_fclose(apk);
            g_apk_index_failed = true;
            return false;
        }
        if (asset_u32le(hdr + 0) != 0x02014b50u) {
            DLA_DEBUG_PRINTF("[asset][WARN] invalid ZIP central header in \"%s\"\n", g_apk_path.c_str());
            asset_fclose(apk);
            g_apk_index_failed = true;
            return false;
        }

        const uint16_t compression_method = asset_u16le(hdr + 10);
        const uint32_t compressed_size = asset_u32le(hdr + 20);
        const uint32_t uncompressed_size = asset_u32le(hdr + 24);
        const uint16_t name_len = asset_u16le(hdr + 28);
        const uint16_t extra_len = asset_u16le(hdr + 30);
        const uint16_t comment_len = asset_u16le(hdr + 32);
        const uint32_t local_header_offset = asset_u32le(hdr + 42);

        std::vector<char> name(name_len + 1, '\0');
        if (name_len > 0 && !asset_read_exact(apk, name.data(), name_len)) {
            DLA_DEBUG_PRINTF("[asset][WARN] truncated ZIP filename in \"%s\"\n", g_apk_path.c_str());
            asset_fclose(apk);
            g_apk_index_failed = true;
            return false;
        }

        if ((extra_len > 0 && asset_fseek(apk, extra_len, SEEK_CUR) != 0) ||
            (comment_len > 0 && asset_fseek(apk, comment_len, SEEK_CUR) != 0)) {
            DLA_DEBUG_PRINTF("[asset][WARN] failed skipping ZIP metadata in \"%s\"\n", g_apk_path.c_str());
            asset_fclose(apk);
            g_apk_index_failed = true;
            return false;
        }

        if (name_len > 0) {
            apkEntry entry = {
                local_header_offset,
                compressed_size,
                uncompressed_size,
                compression_method,
            };
            const std::string entry_name(name.data(), name_len);
            g_apk_entries[entry_name] = entry;
            const std::string lower_name = asset_lower_path(entry_name);
            if (g_apk_lower_names.find(lower_name) == g_apk_lower_names.end())
                g_apk_lower_names[lower_name] = entry_name;
        }
    }

    asset_fclose(apk);
    g_apk_index_ready = true;
    DLA_DEBUG_PRINTF("[asset] indexed %u APK entries from %s\n", (unsigned)g_apk_entries.size(), g_apk_path.c_str());
    return true;
}

static bool asset_apk_find_entry(const char *filename, apkEntry *entry_out, std::string *resolved_name_out) {
    if (!filename || !entry_out)
        return false;

    pthread_mutex_lock(&g_apk_lock);
    const bool index_ok = asset_apk_build_index_locked();
    if (!index_ok) {
        pthread_mutex_unlock(&g_apk_lock);
        return false;
    }

    const std::vector<std::string> candidates = asset_build_apk_candidates(filename);
    for (const std::string &candidate : candidates) {
        const auto it = g_apk_entries.find(candidate);
        if (it != g_apk_entries.end()) {
            *entry_out = it->second;
            if (resolved_name_out)
                *resolved_name_out = candidate;
            pthread_mutex_unlock(&g_apk_lock);
            return true;
        }
    }

    for (const std::string &candidate : candidates) {
        const std::string lower_candidate = asset_lower_path(candidate);
        const auto lower_it = g_apk_lower_names.find(lower_candidate);
        if (lower_it != g_apk_lower_names.end()) {
            const auto entry_it = g_apk_entries.find(lower_it->second);
            if (entry_it == g_apk_entries.end())
                continue;
            *entry_out = entry_it->second;
            if (resolved_name_out)
                *resolved_name_out = entry_it->first;
            pthread_mutex_unlock(&g_apk_lock);
            return true;
        }
    }

    pthread_mutex_unlock(&g_apk_lock);
    return false;
}

static bool asset_apk_try_load(const char *filename, std::string *resolved_name_out, std::vector<uint8_t> *out) {
    if (!out)
        return false;

    apkEntry entry = {};
    std::string resolved_name;
    if (!asset_apk_find_entry(filename, &entry, &resolved_name))
        return false;

    if (entry.uncompressedSize > (64u * 1024u * 1024u) || entry.compressedSize > (64u * 1024u * 1024u)) {
        DLA_DEBUG_PRINTF("[asset][WARN] refusing oversized APK asset \"%s\" (c=%u u=%u)\n",
                      resolved_name.c_str(), entry.compressedSize, entry.uncompressedSize);
        return false;
    }

    FILE *apk = asset_try_fopen(g_apk_path.c_str());
    if (!apk)
        return false;

    uint8_t local_hdr[30];
    if (asset_fseek(apk, (long)entry.localHeaderOffset, SEEK_SET) != 0 ||
        !asset_read_exact(apk, local_hdr, sizeof(local_hdr)) ||
        asset_u32le(local_hdr + 0) != 0x04034b50u) {
        asset_fclose(apk);
        return false;
    }

    const uint16_t name_len = asset_u16le(local_hdr + 26);
    const uint16_t extra_len = asset_u16le(local_hdr + 28);
    const long data_offset = (long)entry.localHeaderOffset + (long)sizeof(local_hdr) + (long)name_len + (long)extra_len;
    if (asset_fseek(apk, data_offset, SEEK_SET) != 0) {
        asset_fclose(apk);
        return false;
    }

    out->clear();
    out->resize(entry.uncompressedSize);

    bool ok = false;
    if (entry.compressionMethod == 0) {
        ok = asset_read_exact(apk, out->data(), out->size());
    } else if (entry.compressionMethod == 8) {
        std::vector<uint8_t> compressed(entry.compressedSize);
        if (asset_read_exact(apk, compressed.data(), compressed.size())) {
            z_stream zs = {};
            zs.next_in = compressed.empty() ? Z_NULL : compressed.data();
            zs.avail_in = (uInt)compressed.size();
            zs.next_out = out->empty() ? Z_NULL : out->data();
            zs.avail_out = (uInt)out->size();

            if (inflateInit2_(&zs, -MAX_WBITS, zlibVersion(), (int)sizeof(zs)) == Z_OK) {
                int rc;
                do {
                    rc = inflate(&zs, Z_NO_FLUSH);
                } while (rc == Z_OK);
                ok = (rc == Z_STREAM_END && zs.total_out == out->size());
                inflateEnd(&zs);
            }
        }
    } else {
        DLA_DEBUG_PRINTF("[asset][WARN] unsupported ZIP compression=%u for \"%s\"\n",
                      entry.compressionMethod, resolved_name.c_str());
    }

    asset_fclose(apk);
    if (!ok) {
        out->clear();
        return false;
    }

    if (resolved_name_out)
        *resolved_name_out = resolved_name;
    return true;
}

AAssetManager * AAssetManager_create() {
    if (g_AAssetManager) return g_AAssetManager;

    assetManager am;

    pthread_mutex_init(&am.mLock, nullptr);

    g_AAssetManager = (AAssetManager *) malloc(sizeof(assetManager));
    memcpy(g_AAssetManager, &am, sizeof(assetManager));

    return g_AAssetManager;
}

AAssetManager * AAssetManager_fromJava(void *env, void *assetManager) {
    (void)env;
    (void)assetManager;
    return AAssetManager_create();
}

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    std::vector<std::string> candidates;
    std::string realp;
    FILE *opened = nullptr;
    bool cached_negative = false;
    std::string cached_path;

    if (asset_lookup_cache_get(filename, &cached_path, &cached_negative)) {
        if (cached_negative)
            return nullptr;
        opened = asset_try_fopen(cached_path.c_str());
        if (opened) {
            realp = cached_path;
        } else {
            asset_lookup_cache_forget(filename);
        }
    }

    if (!opened) {
        candidates = asset_build_candidates(filename);
        for (const std::string &candidate : candidates) {
            opened = asset_try_fopen(candidate.c_str());
            if (opened) {
                realp = candidate;
                asset_lookup_cache_store_file(filename, realp);
                break;
            }
        }
    }

    if (!opened) {
        std::string indexed_path;
        if (asset_loose_index_find_file(filename, &indexed_path)) {
            opened = asset_try_fopen(indexed_path.c_str());
            if (opened) {
                realp = indexed_path;
                asset_lookup_cache_store_file(filename, realp);
                static int loose_index_hit_log_count = 0;
                if (DLA_DEBUG_LOGS && loose_index_hit_log_count < 32) {
                    loose_index_hit_log_count++;
                    DLA_DEBUG_PRINTF("[asset] loose index \"%s\" -> %s\n", filename ? filename : "<null>", realp.c_str());
                }
            }
        }
    }

    if (!opened) {
        std::vector<uint8_t> apk_data;
        std::string apk_name;
        if (asset_apk_try_load(filename, &apk_name, &apk_data)) {
            auto *a = new aAsset;
            a->filename = (char *)malloc(apk_name.length() + 1);
            strcpy(a->filename, apk_name.c_str());
            a->fileSize = apk_data.size();
            a->bytesRead = 0;
            a->memoryBacked = true;
            a->opened = true;
            if (!apk_data.empty()) {
                a->buffer = (uint8_t *)malloc(apk_data.size());
                if (!a->buffer) {
                    free(a->filename);
                    delete a;
                    return nullptr;
                }
                memcpy(a->buffer, apk_data.data(), apk_data.size());
            }
            l_debug("AAssetManager_open<%p>(%p, %s, %i): APK %p", __builtin_return_address(0), mgr, apk_name.c_str(), mode, a);
            return (AAsset *)a;
        }
    }

    if (!opened) {
        static int warn_count = 0;
        if (DLA_DEBUG_LOGS && warn_count < 24) {
            warn_count++;
            DLA_DEBUG_PRINTF("[asset][WARN] open failed for \"%s\"\n", filename ? filename : "<null>");
            for (size_t i = 0; i < candidates.size() && i < 8; i++) {
                DLA_DEBUG_PRINTF("[asset][WARN]   candidate[%u]=%s\n", (unsigned)i, candidates[i].c_str());
            }
            const std::vector<std::string> apk_candidates = asset_build_apk_candidates(filename);
            for (size_t i = 0; i < apk_candidates.size() && i < 4; i++) {
                DLA_DEBUG_PRINTF("[asset][WARN]   apk_candidate[%u]=%s\n", (unsigned)i, apk_candidates[i].c_str());
            }
        }
        asset_lookup_cache_store_negative(filename);
        return nullptr;
    }

    auto * a = new aAsset;
    a->filename = (char *) malloc(realp.length() + 1);
    strcpy(a->filename, realp.c_str());
    a->bytesRead = 0;
    a->f = opened;

    if (!a->f) {
        free(a->filename);
        delete a;
        a = nullptr;
    } else {
        asset_fseek(a->f, 0, SEEK_END);
        a->fileSize = asset_ftell(a->f);
        asset_fseek(a->f, 0, SEEK_SET);
        a->opened = true;
    }

    l_debug("AAssetManager_open<%p>(%p, %s, %i): %p", __builtin_return_address(0), mgr, realp.c_str(), mode, a);
    return (AAsset *) a;
}

void AAsset_close(AAsset* asset) {
    l_debug("AAsset_close<%p>(%p)", __builtin_return_address(0), asset);

    if (asset) {
        auto * a = (aAsset *) asset;
        free(a->filename);
        if (a->memoryBacked) {
            free(a->buffer);
        } else if (a->opened) {
            asset_fclose(a->f);
        }
        delete a;
    }
}

int AAsset_read(AAsset* asset, void* buf, size_t count) {
    l_debug("AAsset_read<%p>(%p, %p, %i)", __builtin_return_address(0), asset, buf, count);

    if (!asset) {
        return -1;
    }

    auto * a = (aAsset *) asset;

    if (a->memoryBacked) {
        const size_t remaining = (a->bytesRead < a->fileSize) ? (a->fileSize - a->bytesRead) : 0;
        const size_t to_copy = std::min(count, remaining);
        if (to_copy == 0)
            return 0;
        memcpy(buf, a->buffer + a->bytesRead, to_copy);
        a->bytesRead += to_copy;
        return (int)to_copy;
    }

    if (!a->opened || !a->f) {
        return -1;
    }

    size_t ret = asset_fread(buf, 1, count, a->f);

    if (ret > 0) {
        a->bytesRead += ret;
        return (int) ret;
    } else {
        if (asset_feof(a->f)) {
            return 0;
        } else {
            return -1;
        }
    }
}

off_t AAsset_seek(AAsset* asset, off_t offset, int whence) {
    l_debug("AAsset_seek(%p, %d, %i)", asset, offset, whence);

    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    if (a->memoryBacked) {
        int64_t base = 0;
        if (whence == SEEK_CUR) {
            base = (int64_t)a->bytesRead;
        } else if (whence == SEEK_END) {
            base = (int64_t)a->fileSize;
        }
        int64_t next = base + (int64_t)offset;
        if (next < 0)
            next = 0;
        if ((size_t)next > a->fileSize)
            next = (int64_t)a->fileSize;
        a->bytesRead = (size_t)next;
        return (off_t)a->bytesRead;
    }

    if (!a->opened || !a->f) {
        return -1;
    }

    if (asset_fseek(a->f, offset, whence) != 0)
        return -1;
    const long pos = asset_ftell(a->f);
    if (pos < 0)
        return -1;
    a->bytesRead = (size_t)pos;
    return (off_t)pos;
}

off_t AAsset_getRemainingLength(AAsset* asset) {
    l_debug("AAsset_getRemainingLength");
    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    if (!a->opened) {
        return -1;
    }

    return (off_t)(a->fileSize - a->bytesRead);
}

off_t AAsset_getLength(AAsset* asset) {
    l_debug("AAsset_getLength");
    if (!asset) {
        return (off_t) -1;
    }

    auto * a = (aAsset *) asset;

    return (off_t)a->fileSize;
}

AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* dirName) {
    (void)mgr;

    std::set<std::string> names;
    asset_collect_loose_dir_entries(dirName, &names);

    pthread_mutex_lock(&g_apk_lock);
    const bool index_ok = asset_apk_build_index_locked();
    if (index_ok) {
        std::vector<std::string> prefixes = asset_build_apk_candidates(dirName && dirName[0] ? dirName : "");
        if (!dirName || !dirName[0]) {
            prefixes.clear();
            prefixes.emplace_back("assets/");
            prefixes.emplace_back("");
        }

        for (std::string prefix : prefixes) {
            if (!prefix.empty() && prefix.back() != '/')
                prefix.push_back('/');

            for (const auto &it : g_apk_entries) {
                const std::string &entry_name = it.first;
                if (!prefix.empty()) {
                    if (entry_name.rfind(prefix, 0) != 0)
                        continue;
                }

                const std::string rest = prefix.empty() ? entry_name : entry_name.substr(prefix.size());
                if (rest.empty())
                    continue;
                const size_t slash = rest.find('/');
                if (slash == std::string::npos)
                    names.insert(rest);
                else if (slash > 0)
                    names.insert(rest.substr(0, slash));
            }
        }
    }
    pthread_mutex_unlock(&g_apk_lock);

    if (names.empty())
        return nullptr;

    auto *dir = new assetDirState;
    dir->entries.assign(names.begin(), names.end());
    return (AAssetDir *)dir;
}

const char* AAssetDir_getNextFileName(AAssetDir* assetDir) {
    auto *dir = (assetDirState *)assetDir;
    if (!dir || dir->index >= dir->entries.size())
        return nullptr;
    return dir->entries[dir->index++].c_str();
}

void AAssetDir_close(AAssetDir* assetDir) {
    delete (assetDirState *)assetDir;
}

int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength) {
    if (!asset) {
        l_warn("AAsset_openFileDescriptor(%p, %p, %p): asset is null", asset, outStart, outLength);
        return -1;
    }
    auto * a = (aAsset *) asset;
    if (a->memoryBacked) {
        l_warn("AAsset_openFileDescriptor(%p/\"%s\", %p, %p): unsupported for memory-backed APK asset",
               asset, a->filename ? a->filename : "<null>", outStart, outLength);
        return -1;
    }
    if (outStart) *outStart = 0;
    if (outLength) *outLength = a->fileSize;
    if (a->opened && a->f) {
        asset_fclose(a->f);
        a->opened = false;
        a->f = nullptr;
    }
    int ret = open(a->filename, O_RDONLY);
    l_debug("AAsset_openFileDescriptor(%p/\"%s\", %p, %p): ret %i", asset, a->filename, outStart, outLength, ret);
    return ret;
}
