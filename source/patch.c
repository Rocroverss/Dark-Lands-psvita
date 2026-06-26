#include <psp2/kernel/clib.h>
#include "debug_log.h"
#include <psp2/io/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynlib.h"
#include "game_prefs.h"
#include "SharedPreferences.h"
#include "so_util/so_util.h"

extern so_module so_mod;
extern void *operator_new_guarded(unsigned int size);
static so_hook g_bt_voronoi_update_hook;
static so_hook g_scheduler_schedule_per_frame_hook;
static so_hook g_scheduler_schedule_hook;
static so_hook g_scheduler_schedule_ex_hook;
static so_hook g_cpp_operator_new_hook;
static so_hook g_cpp_get_new_handler_hook;
static so_hook g_cpp_set_new_handler_hook;
static so_hook g_bt_convex_find_edge_hook;
static so_hook g_texture_manager_onaddresource_iter_hook;
static so_hook g_value_hash_table_find_hook;
static so_hook g_persister_load_hook;
static so_hook g_persister_save_hook;
static so_hook g_data_storage_save_local_hook;
static so_hook g_game_progress_save_level_hook;
static uintptr_t g_persister_tree_find;
static so_hook g_node_addchild_hook;
static so_hook g_node_addchild_z_hook;
static so_hook g_node_addchild_tag_hook;
static so_hook g_node_addchild_name_hook;
static so_hook g_node_insertchild_hook;
static so_hook g_node_addchildhelper_hook;
static so_hook g_screenutils_nodesetpos_hook;
static so_hook g_screenutils_nodesetpos_simple_hook;
static so_hook g_loader_showgamelogo_hook;
static so_hook g_ftcparser_readint_hook;
static so_hook g_ftcparser_readfloat_hook;
static so_hook g_cocos_ref_retain_hook;
static so_hook g_cocos_ref_release_hook;
static so_hook g_cocos_ref_autorelease_hook;
static so_hook g_csloader_nodewithflatbuffersfile_hook;
static so_hook g_csloader_nodewithflatbuffersfile_cb_hook;
static so_hook g_csloader_createnodewithflatbuffersfile_hook;
static so_hook g_csloader_createnodewithflatbuffersfile_cb_hook;
static so_hook g_csloader_createnode_hook;
static so_hook g_csloader_createnode_cb_hook;
static so_hook g_csloader_createtimeline_hook;
static so_hook g_actiontimelinecache_load_flatbuffers_hook;
static so_hook g_actiontimelinecache_load_file_hook;
static so_hook g_actiontimelinecache_create_flatbuffers_hook;
static so_hook g_actiontimelinecache_create_action_hook;
static so_hook g_spriteutils_load_hook;
static so_hook g_shopscene_partialload_hook;
static so_hook g_shopscene_updatecharacter_hook;
static so_hook g_shopmenubase_createbuttons_hook;
static so_hook g_shopmenubase_createlabels_hook;
static so_hook g_inventorymenubase_setupcharacterinfo_hook;
static so_hook g_inventorymenubase_updatecharacterinfo_hook;
static so_hook g_inventorymenubase_showbottomline_hook;
static so_hook g_inventorymenubase_createlist_hook;
static so_hook g_inventorymenu_setupsubcategorybuttons_hook;
static so_hook g_upgrademenu_setupsubcategorybuttons_hook;
static so_hook g_labelsprite_updatevalue_hook;
static so_hook g_c_uxelement_setlabel_plain_hook;
static so_hook g_c_uxelement_setlabel_three_hook;
static so_hook g_c_uxelement_setlabel_text_hook;
static so_hook g_c_uxelement_setlabel_typed_hook;
static so_hook g_ftccharacter_playanimation_hook;
static so_hook g_ftccharacter_update_hook;
static so_hook g_ftccharacter_updateoverlays_hook;
static so_hook g_ftccharacter_playframe_hook;
static so_hook g_sprite_create_with_sprite_frame_hook;
static so_hook g_sprite_set_sprite_frame_hook;
static so_hook g_node_convert_to_worldspace_hook;
static so_hook g_listview_addeventlistener_hook;

typedef char (*bt_voronoi_update_fn)(void *self);
typedef void (*scheduler_schedule_per_frame_fn)(void *scheduler, const void *callback_fn, void *target, int priority, int paused);
typedef void (*scheduler_schedule_fn)(void *scheduler, const void *callback_fn, void *target, float interval, int paused, const void *key);
typedef void (*scheduler_schedule_ex_fn)(void *scheduler, const void *callback_fn, void *target, unsigned repeat, float interval, int paused, const void *key);
typedef void *(*value_hash_table_find_fn)(void *self, const void *key);
typedef void (*persister_load_fn)(void *self);
typedef void (*persister_save_fn)(void *self);
typedef void (*data_storage_save_local_fn)(void *self, int force);
typedef void (*game_progress_save_level_fn)(void *self, int level, const void *progress);
typedef void *(*persister_tree_find_fn)(void *tree, const void *key);
typedef void (*persistent_data_load_fn)(void *self, const char *data, unsigned *offset);
typedef void (*node_addchild_fn)(void *self, void *child);
typedef void (*node_addchild_z_fn)(void *self, void *child, int local_z_order);
typedef void (*node_addchild_tag_fn)(void *self, void *child, int local_z_order, int tag);
typedef void (*node_addchild_name_fn)(void *self, void *child, int local_z_order, const void *name);
typedef void (*node_insertchild_fn)(void *self, void *child, int local_z_order);
typedef void (*node_addchildhelper_fn)(void *self, void *child, int local_z_order, int tag, const void *name, int force_tag);
typedef void (*screenutils_nodesetpos_fn)(void *node, int z_order, const void *anchor, int node_mode, const void *offset, const void *size, int keep_ratio);
typedef void (*screenutils_nodesetpos_simple_fn)(void *node, int z_order, const void *anchor, int node_mode, int keep_ratio);
typedef void (*loader_showgamelogo_fn)(void *self, float fade);
typedef const unsigned char *(*cocos_data_getbytes_fn)(const void *self);
typedef size_t (*cocos_data_getsize_fn)(const void *self);
typedef int (*ftcparser_readint_fn)(const void *data, int *offset);
typedef float (*ftcparser_readfloat_fn)(const void *data, int *offset);
typedef void (*cocos_ref_retain_fn)(void *self);
typedef void (*cocos_ref_release_fn)(void *self);
typedef void *(*cocos_ref_autorelease_fn)(void *self);
// These CSLoader file overloads are instance methods; keep the hidden this
// pointer or the callback argument shifts into garbage inside nodeWithFlatBuffers().
typedef void *(*csloader_nodewithflatbuffersfile_fn)(void *self, const void *path);
typedef void *(*csloader_nodewithflatbuffersfile_cb_fn)(void *self, const void *path, const void *callback);
typedef void *(*csloader_createnodewithflatbuffersfile_fn)(const void *path);
typedef void *(*csloader_createnodewithflatbuffersfile_cb_fn)(const void *path, const void *callback);
typedef void *(*csloader_createnode_fn)(const void *path);
typedef void *(*csloader_createnode_cb_fn)(const void *path, const void *callback);
typedef void *(*csloader_createtimeline_fn)(const void *path);
typedef void *(*actiontimelinecache_load_flatbuffers_fn)(void *self, const void *path);
typedef void *(*actiontimelinecache_load_file_fn)(void *self, const void *path);
typedef void *(*actiontimelinecache_create_flatbuffers_fn)(void *self, const void *path);
typedef void *(*actiontimelinecache_create_action_fn)(void *self, const void *path);
typedef void *(*spriteutils_load_fn)(const void *path, void **action_out);
typedef void (*shopscene_partialload_fn)(void *self, int stage);
typedef void (*shopscene_updatecharacter_fn)(void *self, float dt);
typedef void (*shopmenubase_createbuttons_fn)(void *self);
typedef void (*shopmenubase_createlabels_fn)(void *self, float ui_scale);
typedef void (*inventorymenubase_setupcharacterinfo_fn)(void *self);
typedef void (*inventorymenubase_updatecharacterinfo_fn)(void *self);
typedef void (*inventorymenubase_showbottomline_fn)(void *self, int visible);
typedef void (*inventorymenubase_createlist_fn)(void *self);
typedef void (*inventorymenu_setupsubcategorybuttons_fn)(void *self);
typedef void (*upgrademenu_setupsubcategorybuttons_fn)(void *self);
typedef void (*labelsprite_updatevalue_fn)(void *self);
typedef void (*c_uxelement_setlabel_plain_fn)(void *self, const void *text);
typedef void (*c_uxelement_setlabel_three_fn)(void *self, const void *text, const void *prefix, const void *suffix);
typedef void (*c_uxelement_setlabel_text_fn)(void *self, int text_type, int text_id);
typedef void (*c_uxelement_setlabel_typed_fn)(void *self, const void *prefix, int text_type, int text_id, const void *suffix);
typedef void (*sprite_set_sprite_frame_fn)(void *self, void *sprite_frame);
typedef void (*node_convert_space_sret_fn)(void *out, const void *self, const void *point);
typedef void (*listview_addeventlistener_fn)(void *self, const void *callback);
typedef struct vec2_arg {
    float x;
    float y;
} vec2_arg;
typedef int (*ftccharacter_playanimation_fn)(void *self, const void *anim_name, int loop_mode, float speed, vec2_arg blend_pos, int flags);
typedef void (*ftccharacter_update_fn)(void *self, float dt);
typedef void (*ftccharacter_updateoverlays_fn)(void *self);
typedef void (*ftccharacter_playframe_fn)(void *self);
typedef void *(*sprite_create_fn)(void);
typedef void *(*sprite_create_with_sprite_frame_fn)(void *sprite_frame);

#ifndef SO_PATCH_BT_VORONOI_SAFE_STUB
#define SO_PATCH_BT_VORONOI_SAFE_STUB 1
#endif

#ifndef SO_PATCH_SCHEDULER_DROP_SUSPICIOUS
#define SO_PATCH_SCHEDULER_DROP_SUSPICIOUS 0
#endif

#ifndef SO_PATCH_BT_CONVEX_FIND_EDGE_SAFE_STUB
#define SO_PATCH_BT_CONVEX_FIND_EDGE_SAFE_STUB 1
#endif

#ifndef SO_PATCH_TEXTUREMANAGER_ONADDRESOURCE_NULL_GUARD
#define SO_PATCH_TEXTUREMANAGER_ONADDRESOURCE_NULL_GUARD 1
#endif

#ifndef SO_PATCH_VALUE_HASH_TABLE_FIND_NULL_GUARD
#define SO_PATCH_VALUE_HASH_TABLE_FIND_NULL_GUARD 1
#endif

#ifndef SO_PATCH_NODE_ADDCHILD_NULL_GUARD
#define SO_PATCH_NODE_ADDCHILD_NULL_GUARD 1
#endif

#ifndef SO_PATCH_NODE_ADDCHILDHELPER_NULL_GUARD
#define SO_PATCH_NODE_ADDCHILDHELPER_NULL_GUARD 1
#endif

#ifndef SO_PATCH_SCREENUTILS_NODESETPOS_NULL_GUARD
#define SO_PATCH_SCREENUTILS_NODESETPOS_NULL_GUARD 1
#endif

#ifndef SO_PATCH_LOADER_SHOWGAMELOGO_SAFE_STUB
#define SO_PATCH_LOADER_SHOWGAMELOGO_SAFE_STUB 1
#endif

#ifndef SO_PATCH_FTCPARSER_READ_GUARD
#define SO_PATCH_FTCPARSER_READ_GUARD 1
#endif

#ifndef SO_PATCH_COCOS_REF_NULL_GUARD
#define SO_PATCH_COCOS_REF_NULL_GUARD 1
#endif

#ifndef SO_PATCH_SHOPSCENE_PLAYANIMATION_STUB
#define SO_PATCH_SHOPSCENE_PLAYANIMATION_STUB 0
#endif

#ifndef SO_PATCH_SHOPSCENE_PREVIEW_FALLBACK
#define SO_PATCH_SHOPSCENE_PREVIEW_FALLBACK 0
#endif

#ifndef SO_PATCH_SHOPMENUBASE_CREATELABELS_SAFE_STUB
#define SO_PATCH_SHOPMENUBASE_CREATELABELS_SAFE_STUB 0
#endif

#ifndef SO_PATCH_C_UXELEMENT_LABEL_SAFE_STUB
#define SO_PATCH_C_UXELEMENT_LABEL_SAFE_STUB 0
#endif

#ifndef SO_PATCH_SPRITE_CREATE_FRAME_FALLBACK
#define SO_PATCH_SPRITE_CREATE_FRAME_FALLBACK 0
#endif

#ifndef SO_PATCH_GAMEPLAY_FALLBACK_GUARDS
#define SO_PATCH_GAMEPLAY_FALLBACK_GUARDS 0
#endif

static void *g_cpp_new_handler = NULL;
static volatile uintptr_t g_texture_manager_onaddresource_iter_continue __attribute__((used)) = 0;
static cocos_data_getbytes_fn g_cocos_data_getbytes = NULL;
static cocos_data_getsize_fn g_cocos_data_getsize = NULL;
static void *(*g_cocos_node_create)(void) = NULL;
static sprite_create_fn g_cocos_sprite_create_raw = NULL;
static void *(*g_cocostudio_actiontimeline_create)(void) = NULL;
// Raw retain/release pointers for use in fallback stubs that need to keep
// objects alive past the autorelease pool (e.g. dummy ActionTimeline returned
// from csloader_fallback_timeline must be retained before addChildHelper gets it).
static void (*g_cocos_ref_retain_raw)(void *self) = NULL;
static void (*g_cocos_ref_release_raw)(void *self) = NULL;
static int g_shopscene_preview_context = 0;
static int g_shopscene_preview_broken = 0;
static void *g_shopscene_preview_bad_ftc[16];

static inline uintptr_t so_vaddr_to_abs(const so_module *m, uintptr_t vaddr);
static int shopscene_preview_skip_active(uintptr_t lr);
static void shopscene_preview_mark_bad_ftc(void *self);
static int shopscene_preview_is_bad_ftc(void *self);
static int ftccharacter_playframe_dispatch_suspicious(void *self, uintptr_t *dispatch_out);
static int caller_in_inventorymenu_createlist_range(uintptr_t lr);
static int caller_in_inventorymenu_setupsubcategorybuttons_range(uintptr_t lr);
static int caller_in_upgrademenu_setupsubcategorybuttons_range(uintptr_t lr);

static char btVoronoi_updateClosest_guard(void *self) {
    const uintptr_t p = (uintptr_t)self;
    if (p < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] btVoronoiSimplexSolver::updateClosestVectorAndPoints invalid this=%p; returning false\n", self);
        }
        return 0;
    }
#if SO_PATCH_BT_VORONOI_SAFE_STUB
    // This function still crashes in this build (prefetch abort around 0x98fe328d).
    // Keep solver state in a conservative "no valid closest point" form and return false.
    {
        uint8_t *s = (uint8_t *)self;
        s[0x4e] = 0; // m_cachedValidClosest
        s[0x53] = 0; // m_usedVertices bitfield
        s[0x58] = 0; // m_degenerate
        s[0x59] = 0; // m_needsUpdate
        static int warned_stub = 0;
        if (!warned_stub) {
            warned_stub = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] btVoronoiSimplexSolver::updateClosestVectorAndPoints safe-stub active\n");
        }
    }
    return 0;
#else
    return SO_CONTINUE_TYPED(char, bt_voronoi_update_fn, g_bt_voronoi_update_hook, self);
#endif
}

static void cocos_console_create_command_noop(void *self) {
    (void)self;
}

static int cocos_console_ret0(void) {
    return 0;
}

static int ptr_is_plausible_exec(uintptr_t p) {
    uintptr_t v = p & ~(uintptr_t)1u;
    // Only treat addresses in mapped text as executable for this SO.
    if (v >= (uintptr_t)so_mod.text_base && v < (uintptr_t)so_mod.text_base + (uintptr_t)so_mod.text_size)
        return 1;
    // Main executable text.
    if ((v >= 0x81000000u && v < 0x82000000u) ||
        // Kernel module text.
        (v >= 0xE0000000u && v < 0xF0000000u))
        return 1;
    return 0;
}

static void *cpp_get_new_handler_guard(void) {
    if (!ptr_is_plausible_exec((uintptr_t)g_cpp_new_handler))
        return NULL;
    return g_cpp_new_handler;
}

static void *cpp_set_new_handler_guard(void *handler) {
    void *prev = g_cpp_new_handler;
    if (ptr_is_plausible_exec((uintptr_t)handler)) {
        g_cpp_new_handler = handler;
    } else {
        if (handler) {
            static int warned = 0;
            if (!warned) {
                warned = 1;
                DLA_DEBUG_PRINTF("[so_patch][WARN] set_new_handler received invalid ptr=%p; forcing NULL\n", handler);
            }
        }
        g_cpp_new_handler = NULL;
    }
    return prev;
}

static void *btConvexHullInternal_findEdgeForCoplanarFaces_guard(
    void *self,
    void *v1,
    void *v2,
    void **edge_a_ref,
    void **edge_b_ref,
    void *v5,
    void *v6
) {
    (void)self;
    (void)v1;
    (void)v2;
    (void)v5;
    (void)v6;
#if SO_PATCH_BT_CONVEX_FIND_EDGE_SAFE_STUB
    if (edge_a_ref)
        *edge_a_ref = NULL;
    if (edge_b_ref)
        *edge_b_ref = NULL;
    static int warned = 0;
    if (!warned) {
        warned = 1;
        DLA_DEBUG_PRINTF("[so_patch][WARN] btConvexHullInternal::findEdgeForCoplanarFaces safe-stub active\n");
    }
    return NULL;
#else
    return SO_CONTINUE_TYPED(void *, void *(*)(void *, void *, void *, void **, void **, void *, void *),
                            g_bt_convex_find_edge_hook, self, v1, v2, edge_a_ref, edge_b_ref, v5, v6);
#endif
}

__attribute__((naked)) static void texture_manager_onaddresource_iter_guard(void) {
    __asm__ volatile(
        "adds r6, #16\n"
        "add r4, sp, #84\n"
        "add.w r10, sp, #72\n"
        // The original code expects r8 to be a valid in-memory pointer.
        // When an internal lookup returns NULL, it becomes 0x8 here and the
        // next dereference aborts. Force the existing null/empty path instead.
        "lsrs r0, r8, #28\n"
        "cmp r0, #8\n"
        "blo 1f\n"
        "ldr.w r8, [r8]\n"
        "b 2f\n"
        "1:\n"
        "mov.w r8, #0\n"
        "2:\n"
        "movw r0, #:lower16:g_texture_manager_onaddresource_iter_continue\n"
        "movt r0, #:upper16:g_texture_manager_onaddresource_iter_continue\n"
        "ldr r0, [r0]\n"
        "bx r0\n"
    );
}

static void *value_hash_table_find_guard(void *self, const void *key) {
    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] unordered_map<string, cocos2d::Value>::find received invalid this=%p key=%p; returning end()\n",
                         self, key);
        }
        return NULL;
    }

    return SO_CONTINUE_TYPED(void *, value_hash_table_find_fn, g_value_hash_table_find_hook, self, key);
}

// libc++/NDK std::string is 12 bytes on this 32-bit Android .so.  Persister
// stores the save-file name at self+8 and the save slot at self+4.  Decoding it
// here lets the Vita shim check the native .cloud file before calling the
// original loader; otherwise an empty/missing file makes tellg() return -1 and
// the game tries operator new[](0xffffffff).
#define VITA_PERSISTER_MAX_FILE_SIZE (8u * 1024u * 1024u)

static int ndk_string_copy(char *out, size_t out_size, const void *str_obj) {
    if (!out || out_size == 0 || !str_obj) {
        return 0;
    }

    out[0] = '\0';

    const unsigned char *raw = (const unsigned char *)str_obj;
    const char *src = NULL;
    uint32_t len = 0;

    if (raw[0] & 1u) {
        uint32_t ptr32 = 0;
        memcpy(&len, raw + 4, sizeof(len));
        memcpy(&ptr32, raw + 8, sizeof(ptr32));
        src = (const char *)(uintptr_t)ptr32;
    } else {
        len = (uint32_t)(raw[0] >> 1);
        src = (const char *)(raw + 1);
    }

    if (!src || len == 0 || len >= 240u) {
        return 0;
    }

    size_t copy_len = len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }

    memcpy(out, src, copy_len);
    out[copy_len] = '\0';
    return 1;
}

static int persister_build_vita_path(void *self, char *out, size_t out_size) {
    if (!self || !out || out_size == 0) {
        return 0;
    }

    char name[128];
    if (!ndk_string_copy(name, sizeof(name), (const unsigned char *)self + 8)) {
        snprintf(out, out_size, "%s<unknown>", DATA_PATH);
        return 0;
    }

    int slot = *(const int *)((const unsigned char *)self + 4);
    if (slot >= 1 && slot <= 99) {
        char slot_text[8];
        snprintf(slot_text, sizeof(slot_text), "%d", slot);

        char slotted[128];
        const char *dot = strchr(name, '.');
        if (dot && dot != name) {
            size_t prefix_len = (size_t)(dot - name);
            if (prefix_len >= sizeof(slotted)) {
                prefix_len = sizeof(slotted) - 1;
            }

            memcpy(slotted, name, prefix_len);
            slotted[prefix_len] = '\0';
            strncat(slotted, slot_text, sizeof(slotted) - strlen(slotted) - 1);
            strncat(slotted, dot, sizeof(slotted) - strlen(slotted) - 1);
            snprintf(name, sizeof(name), "%s", slotted);
        } else {
            snprintf(slotted, sizeof(slotted), "%s%s", slot_text, name);
            snprintf(name, sizeof(name), "%s", slotted);
        }
    }

    snprintf(out, out_size, "%s%s", DATA_PATH, name);
    return 1;
}

static int vita_file_size(const char *path, unsigned *size_out) {
    if (size_out) {
        *size_out = 0;
    }
    if (!path || !path[0]) {
        return -1;
    }

    SceIoStat st;
    memset(&st, 0, sizeof(st));
    int rc = sceIoGetstat(path, &st);
    if (rc < 0) {
        return rc;
    }

    if (size_out) {
        *size_out = st.st_size > 0xffffffffu ? 0xffffffffu : (unsigned)st.st_size;
    }
    return 0;
}

static unsigned read_u32_unaligned(const void *ptr) {
    unsigned value = 0;
    memcpy(&value, ptr, sizeof(value));
    return value;
}

static void make_ndk_string_view(unsigned char out[12], const char *str, unsigned len) {
    memset(out, 0, 12);

    // libc++ short-string stores (len << 1) in byte 0 and chars at +1.
    // Long-string stores capacity|1, size, then char pointer.
    if (len <= 10u) {
        out[0] = (unsigned char)(len << 1);
        memcpy(out + 1, str, len);
        out[1 + len] = '\0';
    } else {
        unsigned capacity = (len + 1u) | 1u;
        unsigned ptr32 = (unsigned)(uintptr_t)str;
        memcpy(out, &capacity, sizeof(capacity));
        memcpy(out + 4, &len, sizeof(len));
        memcpy(out + 8, &ptr32, sizeof(ptr32));
    }
}

static void *persister_find_property(void *self, const char *key, unsigned key_len) {
    if (!self || !key || !g_persister_tree_find) {
        return NULL;
    }

    unsigned char key_string[12];
    make_ndk_string_view(key_string, key, key_len);

    void *tree = (unsigned char *)self + 0x14;
    void *end = (unsigned char *)self + 0x18;
    void *node = ((persister_tree_find_fn)g_persister_tree_find)(tree, key_string);
    if (node == end) {
        return NULL;
    }

    return node;
}

static int persister_safe_load_file(void *self, const char *path, unsigned expected_size) {
    if (!self || !path || !g_persister_tree_find || expected_size < 4u ||
        expected_size > VITA_PERSISTER_MAX_FILE_SIZE) {
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        DLA_DEBUG_PRINTF("[SAVE][LOAD][SKIP] safe fopen failed self=%p path=\"%s\"\n", self, path);
        return 0;
    }

    char *data = (char *)malloc(expected_size);
    if (!data) {
        fclose(f);
        DLA_DEBUG_PRINTF("[SAVE][LOAD][SKIP] safe malloc failed self=%p path=\"%s\" size=%u\n",
                     self, path, expected_size);
        return 0;
    }

    size_t got = fread(data, 1, expected_size, f);
    fclose(f);
    if (got != expected_size) {
        DLA_DEBUG_PRINTF("[SAVE][LOAD][SKIP] safe fread short self=%p path=\"%s\" got=%u expected=%u\n",
                     self, path, (unsigned)got, expected_size);
        free(data);
        return 0;
    }

    unsigned count = read_u32_unaligned(data);
    unsigned offset = 4;
    unsigned loaded = 0;
    unsigned missing = 0;

    if (count > 256u) {
        DLA_DEBUG_PRINTF("[SAVE][LOAD][SKIP] bad property count self=%p path=\"%s\" count=%u size=%u\n",
                     self, path, count, expected_size);
        free(data);
        return 0;
    }

    for (unsigned i = 0; i < count; ++i) {
        if (offset + 4u > expected_size) {
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] truncated key header path=\"%s\" index=%u offset=%u size=%u\n",
                         path, i, offset, expected_size);
            break;
        }

        unsigned key_len = read_u32_unaligned(data + offset);
        offset += 4u;
        if (key_len >= 240u || offset + key_len >= expected_size || data[offset + key_len] != '\0') {
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] invalid key path=\"%s\" index=%u key_len=%u offset=%u size=%u\n",
                         path, i, key_len, offset, expected_size);
            break;
        }

        const char *key = data + offset;
        offset += key_len + 1u;

        void *node = persister_find_property(self, key, key_len);
        if (!node) {
            missing++;
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] property not registered path=\"%s\" key=\"%s\"\n", path, key);
            continue;
        }

        void *property = *(void **)((unsigned char *)node + 0x1c);
        if (!property) {
            missing++;
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] property null path=\"%s\" key=\"%s\"\n", path, key);
            continue;
        }

        uintptr_t vtable = *(uintptr_t *)property;
        uintptr_t load_addr = *(uintptr_t *)(vtable + 0x0c);
        if (!load_addr) {
            missing++;
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] property load missing path=\"%s\" key=\"%s\"\n", path, key);
            continue;
        }

        unsigned before = offset;
        ((persistent_data_load_fn)load_addr)(property, data, &offset);
        if (offset < before || offset > expected_size) {
            DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] property load overran path=\"%s\" key=\"%s\" before=%u after=%u size=%u\n",
                         path, key, before, offset, expected_size);
            break;
        }

        loaded++;
    }

    DLA_DEBUG_PRINTF("[SAVE][LOAD] safe loaded self=%p path=\"%s\" count=%u loaded=%u missing=%u final_offset=%u size=%u\n",
                 self, path, count, loaded, missing, offset, expected_size);
    free(data);
    return loaded > 0 || count == 0;
}

static void persister_load_guard(void *self) {
    char path[256];
    int have_path = persister_build_vita_path(self, path, sizeof(path));
    unsigned size = 0;
    int stat_rc = have_path ? vita_file_size(path, &size) : -1;

    if (!have_path || stat_rc < 0 || size == 0 || size > VITA_PERSISTER_MAX_FILE_SIZE) {
        static int warn_count = 0;
        if (warn_count < 24) {
            DLA_DEBUG_PRINTF("[SAVE][LOAD][SKIP] file not ready self=%p path=\"%s\" stat=%d size=%u\n",
                         self, have_path ? path : "<unknown>", stat_rc, size);
            warn_count++;
        }
        return;
    }

    DLA_DEBUG_PRINTF("[SAVE][LOAD] safe game::Persister::Load begin self=%p path=\"%s\" size=%u\n",
                 self, path, size);
    if (!persister_safe_load_file(self, path, size)) {
        DLA_DEBUG_PRINTF("[SAVE][LOAD][WARN] safe load produced no data self=%p path=\"%s\" size=%u\n",
                     self, path, size);
    }
    unsigned after_size = 0;
    int after_rc = vita_file_size(path, &after_size);
    DLA_DEBUG_PRINTF("[SAVE][LOAD] safe game::Persister::Load end self=%p path=\"%s\" stat=%d size=%u\n",
                 self, path, after_rc, after_size);
}

static void save_observer_flush(const char *reason) {
    DLA_DEBUG_PRINTF("[SAVE] flushing Vita save files reason=\"%s\"\n", reason ? reason : "<unknown>");
    prefs_flush_with_reason(reason ? reason : "native save observer");
    game_prefs_flush(reason ? reason : "native save observer");
}

static void persister_save_guard(void *self) {
    char path[256];
    int have_path = persister_build_vita_path(self, path, sizeof(path));
    unsigned before_size = 0;
    int before_rc = have_path ? vita_file_size(path, &before_size) : -1;

    DLA_DEBUG_PRINTF("[SAVE] game::Persister::Save begin self=%p path=\"%s\" stat=%d size=%u\n",
                 self, have_path ? path : "<unknown>", before_rc, before_size);
    SO_CONTINUE_TYPED_VOID(persister_save_fn, g_persister_save_hook, self);
    unsigned after_size = 0;
    int after_rc = have_path ? vita_file_size(path, &after_size) : -1;
    DLA_DEBUG_PRINTF("[SAVE] game::Persister::Save end self=%p path=\"%s\" stat=%d size=%u\n",
                 self, have_path ? path : "<unknown>", after_rc, after_size);
    save_observer_flush("game::Persister::Save");
}

static void data_storage_save_local_guard(void *self, int force) {
    DLA_DEBUG_PRINTF("[SAVE] game::DataStorage::SaveLocal begin self=%p force=%d\n", self, force);
    SO_CONTINUE_TYPED_VOID(data_storage_save_local_fn, g_data_storage_save_local_hook, self, force);
    DLA_DEBUG_PRINTF("[SAVE] game::DataStorage::SaveLocal end self=%p force=%d\n", self, force);
    save_observer_flush("game::DataStorage::SaveLocal");
}

static void game_progress_save_level_guard(void *self, int level, const void *progress) {
    DLA_DEBUG_PRINTF("[SAVE] game::GameProgress::SaveLevel begin self=%p level=%d progress=%p\n", self, level, progress);
    SO_CONTINUE_TYPED_VOID(game_progress_save_level_fn, g_game_progress_save_level_hook, self, level, progress);
    DLA_DEBUG_PRINTF("[SAVE] game::GameProgress::SaveLevel end self=%p level=%d progress=%p\n", self, level, progress);
    save_observer_flush("game::GameProgress::SaveLevel");
}

static int node_child_call_invalid(const char *which, void *self, void *child, void *lr) {
    if ((uintptr_t)self < 0x10000u || (uintptr_t)child < 0x10000u) {
        static int warn_count = 0;
        if (warn_count < 24) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] cocos2d::Node::%s invalid self=%p child=%p caller=%p; dropping call\n",
                         which, self, child, lr);
            warn_count++;
        }
        return 1;
    }

    return 0;
}

static void node_addchild_guard(void *self, void *child) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (node_child_call_invalid("addChild", self, child, lr))
        return;

    SO_CONTINUE_TYPED_VOID(node_addchild_fn, g_node_addchild_hook, self, child);
}

static void node_addchild_z_guard(void *self, void *child, int local_z_order) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (node_child_call_invalid("addChild(z)", self, child, lr))
        return;

    SO_CONTINUE_TYPED_VOID(node_addchild_z_fn, g_node_addchild_z_hook, self, child, local_z_order);
}

static void node_addchild_tag_guard(void *self, void *child, int local_z_order, int tag) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (node_child_call_invalid("addChild(z,tag)", self, child, lr))
        return;

    SO_CONTINUE_TYPED_VOID(node_addchild_tag_fn, g_node_addchild_tag_hook, self, child, local_z_order, tag);
}

static void node_addchild_name_guard(void *self, void *child, int local_z_order, const void *name) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (node_child_call_invalid("addChild(z,name)", self, child, lr))
        return;

    SO_CONTINUE_TYPED_VOID(node_addchild_name_fn, g_node_addchild_name_hook, self, child, local_z_order, name);
}

static void node_insertchild_guard(void *self, void *child, int local_z_order) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (node_child_call_invalid("insertChild", self, child, lr))
        return;

    SO_CONTINUE_TYPED_VOID(node_insertchild_fn, g_node_insertchild_hook, self, child, local_z_order);
}

static void node_addchildhelper_guard(void *self, void *child, int local_z_order, int tag, const void *name, int force_tag) {
    if ((uintptr_t)self < 0x10000u || (uintptr_t)child < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] cocos2d::Node::addChildHelper invalid self=%p child=%p z=%d tag=%d name=%p forceTag=%d; dropping call\n",
                         self, child, local_z_order, tag, name, force_tag);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(node_addchildhelper_fn, g_node_addchildhelper_hook,
                           self, child, local_z_order, tag, name, force_tag);
}

static void screenutils_nodesetpos_guard(void *node, int z_order, const void *anchor, int node_mode, const void *offset, const void *size, int keep_ratio) {
    if ((uintptr_t)node < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ScreenUtils::NodeSetPos(full) invalid node=%p z=%d anchor=%p mode=%d offset=%p size=%p keep=%d; dropping call\n",
                         node, z_order, anchor, node_mode, offset, size, keep_ratio);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(screenutils_nodesetpos_fn, g_screenutils_nodesetpos_hook,
                           node, z_order, anchor, node_mode, offset, size, keep_ratio);
}

static void screenutils_nodesetpos_simple_guard(void *node, int z_order, const void *anchor, int node_mode, int keep_ratio) {
    if ((uintptr_t)node < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ScreenUtils::NodeSetPos(simple) invalid node=%p z=%d anchor=%p mode=%d keep=%d; dropping call\n",
                         node, z_order, anchor, node_mode, keep_ratio);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(screenutils_nodesetpos_simple_fn, g_screenutils_nodesetpos_simple_hook,
                           node, z_order, anchor, node_mode, keep_ratio);
}

static void loader_showgamelogo_guard(void *self, float fade) {
    (void)self;
    (void)fade;
#if SO_PATCH_LOADER_SHOWGAMELOGO_SAFE_STUB
    static int warned = 0;
    if (!warned) {
        warned = 1;
        DLA_DEBUG_PRINTF("[so_patch][WARN] game::Loader::ShowGameLogo safe-stub active\n");
    }
    return;
#else
    SO_CONTINUE_TYPED_VOID(loader_showgamelogo_fn, g_loader_showgamelogo_hook, self, fade);
#endif
}

static void shopscene_partialload_guard(void *self, int stage) {
    const int prev_context = g_shopscene_preview_context;

    g_shopscene_preview_context = prev_context + 1;

#if SO_PATCH_SHOPSCENE_PREVIEW_FALLBACK
    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopScene::PartialLoad preview fallback active self=%p stage=%d; skipping preview load\n",
                         self, stage);
        }
        g_shopscene_preview_context = prev_context;
        return;
    }
#endif

    SO_CONTINUE_TYPED_VOID(shopscene_partialload_fn, g_shopscene_partialload_hook, self, stage);
    g_shopscene_preview_context = prev_context;
}

static void shopscene_updatecharacter_guard(void *self, float dt) {
    const int prev_context = g_shopscene_preview_context;

    g_shopscene_preview_context = prev_context + 1;

#if SO_PATCH_SHOPSCENE_PREVIEW_FALLBACK
    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopScene::UpdateCharacter preview fallback active self=%p; skipping preview tick\n",
                         self);
        }
        g_shopscene_preview_context = prev_context;
        (void)dt;
        return;
    }
#endif

    SO_CONTINUE_TYPED_VOID(shopscene_updatecharacter_fn, g_shopscene_updatecharacter_hook, self, dt);
    g_shopscene_preview_context = prev_context;
}

static int ftcparser_data_read_valid(const void *data, int *offset, size_t need, const unsigned char **bytes_out, size_t *size_out, int *pos_out) {
    const unsigned char *bytes = NULL;
    size_t size = 0;
    int pos = offset ? *offset : 0;

    if (data && g_cocos_data_getbytes)
        bytes = g_cocos_data_getbytes(data);
    if (data && g_cocos_data_getsize)
        size = g_cocos_data_getsize(data);

    if (bytes_out)
        *bytes_out = bytes;
    if (size_out)
        *size_out = size;
    if (pos_out)
        *pos_out = pos;

    if (!data || !offset || !bytes || pos < 0)
        return 0;
    if ((size_t)pos > size)
        return 0;
    if (size - (size_t)pos < need)
        return 0;
    return 1;
}

static int ftcparser_readint_guard(const void *data, int *offset) {
    const unsigned char *bytes = NULL;
    size_t size = 0;
    int pos = 0;

    if (!ftcparser_data_read_valid(data, offset, sizeof(int), &bytes, &size, &pos)) {
        if (g_shopscene_preview_context > 0)
            g_shopscene_preview_broken = 1;
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCParser::ReadInt invalid data=%p bytes=%p size=%u offset=%d; returning 0\n",
                         data, bytes, (unsigned)size, pos);
        }
        if (offset) {
            if (pos < 0)
                pos = 0;
            *offset = pos + (int)sizeof(int);
        }
        return 0;
    }

    return SO_CONTINUE_TYPED(int, ftcparser_readint_fn, g_ftcparser_readint_hook, data, offset);
}

static float ftcparser_readfloat_guard(const void *data, int *offset) {
    const unsigned char *bytes = NULL;
    size_t size = 0;
    int pos = 0;

    if (!ftcparser_data_read_valid(data, offset, sizeof(float), &bytes, &size, &pos)) {
        if (g_shopscene_preview_context > 0)
            g_shopscene_preview_broken = 1;
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCParser::ReadFloat invalid data=%p bytes=%p size=%u offset=%d; returning 0.0\n",
                         data, bytes, (unsigned)size, pos);
        }
        if (offset) {
            if (pos < 0)
                pos = 0;
            *offset = pos + (int)sizeof(float);
        }
        return 0.0f;
    }

    return SO_CONTINUE_TYPED(float, ftcparser_readfloat_fn, g_ftcparser_readfloat_hook, data, offset);
}

static void cocos_ref_log_invalid_call(const char *which, void *self) {
    static int warn_count = 0;
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (warn_count < 12) {
        DLA_DEBUG_PRINTF("[so_patch][WARN] cocos2d::Ref::%s invalid self=%p caller=%p; ignoring\n",
                     which, self, lr);
        warn_count++;
    }
}

static void cocos_ref_retain_guard(void *self) {
    if ((uintptr_t)self < 0x10000u) {
        cocos_ref_log_invalid_call("retain", self);
        return;
    }

    SO_CONTINUE_TYPED_VOID(cocos_ref_retain_fn, g_cocos_ref_retain_hook, self);
}

static void cocos_ref_release_guard(void *self) {
    if ((uintptr_t)self < 0x10000u) {
        cocos_ref_log_invalid_call("release", self);
        return;
    }

    SO_CONTINUE_TYPED_VOID(cocos_ref_release_fn, g_cocos_ref_release_hook, self);
}

static void *cocos_ref_autorelease_guard(void *self) {
    if ((uintptr_t)self < 0x10000u) {
        cocos_ref_log_invalid_call("autorelease", self);
        return NULL;
    }

    return SO_CONTINUE_TYPED(void *, cocos_ref_autorelease_fn, g_cocos_ref_autorelease_hook, self);
}

static int csloader_preview_ptr_plausible(uintptr_t p) {
    return ((p >= 0x81000000u && p < 0x84000000u) ||
            (p >= 0x84000000u && p < 0x88000000u) ||
            (p >= 0x98000000u && p < 0x9c000000u));
}

static const char *csloader_path_preview(const void *path) {
    static char preview[96];
    const unsigned char *raw = (const unsigned char *)path;
    size_t best_len = 0;
    const unsigned char *best = NULL;

    if ((uintptr_t)path < 0x10000u) {
        sceClibSnprintf(preview, sizeof(preview), "<invalid:%p>", path);
        return preview;
    }

    for (int off = 0; off <= 12; off += 4) {
        uintptr_t candidate = *(const uintptr_t *)(raw + off);
        if (!csloader_preview_ptr_plausible(candidate))
            continue;

        const unsigned char *s = (const unsigned char *)candidate;
        size_t len = 0;
        while (len < sizeof(preview) - 1) {
            unsigned char c = s[len];
            if (c == 0)
                break;
            if (c < 0x20 || c > 0x7e) {
                len = 0;
                break;
            }
            len++;
        }
        if (len >= 4) {
            sceClibMemcpy(preview, s, len);
            preview[len] = 0;
            return preview;
        }
    }

    for (int off = 0; off < 20; off++) {
        size_t len = 0;
        while (off + (int)len < 32 && len < sizeof(preview) - 1) {
            unsigned char c = raw[off + len];
            if (c == 0)
                break;
            if (c < 0x20 || c > 0x7e) {
                len = 0;
                break;
            }
            len++;
        }
        if (len > best_len) {
            best_len = len;
            best = raw + off;
        }
    }

    if (best && best_len >= 4) {
        sceClibMemcpy(preview, best, best_len);
        preview[best_len] = 0;
        return preview;
    }

    sceClibSnprintf(preview, sizeof(preview), "<opaque:%p>", path);
    return preview;
}

static void *csloader_fallback_node(const char *which, const void *path, void *node) {
    static int warn_count = 0;

    if (warn_count < 24) {
        DLA_DEBUG_PRINTF("[so_patch][WARN] %s fallback path=%s raw=%p node=%p\n",
                     which, csloader_path_preview(path), path, node);
        warn_count++;
    }

    return g_cocos_node_create ? g_cocos_node_create() : NULL;
}

static void *csloader_fallback_timeline(const char *which, const void *path, void *timeline) {
    static int warn_count = 0;

    if (warn_count < 24) {
        DLA_DEBUG_PRINTF("[so_patch][WARN] %s fallback path=%s raw=%p timeline=%p\n",
                     which, csloader_path_preview(path), path, timeline);
        warn_count++;
    }

    // ActionTimeline::create() returns an autoreleased object (refcount 1, on
    // the autorelease pool).  addChildHelper calls retain() on it, but if the
    // autorelease pool drains first the object is already freed, causing the
    // data abort at 0x98922cf4.  Retain it here so the caller can safely pass
    // it to addChild / addChildHelper without racing the pool.
    void *tl = g_cocostudio_actiontimeline_create ? g_cocostudio_actiontimeline_create() : NULL;
    if (tl && g_cocos_ref_retain_raw)
        g_cocos_ref_retain_raw(tl);
    return tl;
}

static void *csloader_nodewithflatbuffersfile_guard(void *self, const void *path) {
    if ((uintptr_t)self < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile invalid self", path, self);
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_nodewithflatbuffersfile_fn,
                                   g_csloader_nodewithflatbuffersfile_hook, self, path);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile returned invalid node", path, node);

    return node;
}

static void *csloader_nodewithflatbuffersfile_cb_guard(void *self, const void *path, const void *callback) {
    if ((uintptr_t)self < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile(cb) invalid self", path, self);
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile(cb) invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_nodewithflatbuffersfile_cb_fn,
                                   g_csloader_nodewithflatbuffersfile_cb_hook, self, path, callback);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::nodeWithFlatBuffersFile(cb) returned invalid node", path, node);

    return node;
}

static void *csloader_createnodewithflatbuffersfile_guard(const void *path) {
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::createNodeWithFlatBuffersFile invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_createnodewithflatbuffersfile_fn,
                                   g_csloader_createnodewithflatbuffersfile_hook, path);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::createNodeWithFlatBuffersFile returned invalid node", path, node);

    return node;
}

static void *csloader_createnodewithflatbuffersfile_cb_guard(const void *path, const void *callback) {
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::createNodeWithFlatBuffersFile(cb) invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_createnodewithflatbuffersfile_cb_fn,
                                   g_csloader_createnodewithflatbuffersfile_cb_hook, path, callback);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::createNodeWithFlatBuffersFile(cb) returned invalid node", path, node);

    return node;
}

static void *csloader_createnode_guard(const void *path) {
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::createNode invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_createnode_fn,
                                   g_csloader_createnode_hook, path);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::createNode returned invalid node", path, node);

    return node;
}

static void *csloader_createnode_cb_guard(const void *path, const void *callback) {
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_node("CSLoader::createNode(cb) invalid path", path, NULL);

    void *node = SO_CONTINUE_TYPED(void *, csloader_createnode_cb_fn,
                                   g_csloader_createnode_cb_hook, path, callback);
    if ((uintptr_t)node < 0x10000u)
        return csloader_fallback_node("CSLoader::createNode(cb) returned invalid node", path, node);

    return node;
}

static void *csloader_createtimeline_guard(const void *path) {
    if ((uintptr_t)path < 0x10000u)
        return csloader_fallback_timeline("CSLoader::createTimeline invalid path", path, NULL);

    void *timeline = SO_CONTINUE_TYPED(void *, csloader_createtimeline_fn,
                                       g_csloader_createtimeline_hook, path);
    if ((uintptr_t)timeline < 0x10000u)
        return csloader_fallback_timeline("CSLoader::createTimeline returned invalid timeline", path, timeline);

    return timeline;
}

static void *spriteutils_load_guard(const void *path, void **action_out) {
    void *fallback_action = NULL;
    void **safe_action_out = action_out;
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)path < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] SpriteUtils::Load invalid path=%p; returning empty node\n", path);
        }
        return g_cocos_node_create ? g_cocos_node_create() : NULL;
    }

    if ((uintptr_t)action_out < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] SpriteUtils::Load invalid action_out=%p; using temporary storage\n",
                         action_out);
        }
        safe_action_out = &fallback_action;
    }

    if (shopscene_preview_skip_active((uintptr_t)lr)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] SpriteUtils::Load preview fallback active path=%s raw=%p caller=%p; returning empty node\n",
                         csloader_path_preview(path), path, lr);
        }
        if (safe_action_out)
            *safe_action_out = NULL;
        return g_cocos_node_create ? g_cocos_node_create() : NULL;
    }

    void *node = SO_CONTINUE_TYPED(void *, spriteutils_load_fn, g_spriteutils_load_hook, path, safe_action_out);
    if ((uintptr_t)node < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] SpriteUtils::Load returned invalid node=%p; using Node::create fallback\n",
                         node);
        }
        if (safe_action_out)
            *safe_action_out = NULL;
        return g_cocos_node_create ? g_cocos_node_create() : NULL;
    }

    return node;
}

static int caller_in_shopscene_load_range(uintptr_t lr) {
    const uintptr_t caller = lr & ~(uintptr_t)1u;
    const uintptr_t partialload_lo = so_vaddr_to_abs(&so_mod, 0x0071384du);
    const uintptr_t partialload_hi = so_vaddr_to_abs(&so_mod, 0x00713c79u);
    const uintptr_t updatechar_lo = so_vaddr_to_abs(&so_mod, 0x00713ca1u);
    const uintptr_t updatechar_hi = so_vaddr_to_abs(&so_mod, 0x00713e1du);
    const uintptr_t setup_lo = so_vaddr_to_abs(&so_mod, 0x00713ee5u);
    const uintptr_t setup_hi = so_vaddr_to_abs(&so_mod, 0x007140b1u);

    if (caller >= partialload_lo && caller < partialload_hi)
        return 1;
    if (caller >= updatechar_lo && caller < updatechar_hi)
        return 1;
    if (caller >= setup_lo && caller < setup_hi)
        return 1;
    return 0;
}

static int caller_in_inventorymenu_info_range(uintptr_t lr) {
    const uintptr_t caller = lr & ~(uintptr_t)1u;
    const uintptr_t setup_info_lo = so_vaddr_to_abs(&so_mod, 0x00710e3du);
    const uintptr_t setup_info_hi = so_vaddr_to_abs(&so_mod, 0x0071123du);
    const uintptr_t update_info_lo = so_vaddr_to_abs(&so_mod, 0x0071133du);
    const uintptr_t update_info_hi = so_vaddr_to_abs(&so_mod, 0x00711381u);
    const uintptr_t show_bottom_lo = so_vaddr_to_abs(&so_mod, 0x00711381u);
    const uintptr_t show_bottom_hi = so_vaddr_to_abs(&so_mod, 0x007113fdu);

    if (caller >= setup_info_lo && caller < setup_info_hi)
        return 1;
    if (caller >= update_info_lo && caller < update_info_hi)
        return 1;
    if (caller >= show_bottom_lo && caller < show_bottom_hi)
        return 1;
    return 0;
}

static int caller_in_inventorymenu_createlist_range(uintptr_t lr) {
    const uintptr_t caller = lr & ~(uintptr_t)1u;
    const uintptr_t create_list_lo = so_vaddr_to_abs(&so_mod, 0x007114f5u);
    const uintptr_t create_list_hi = so_vaddr_to_abs(&so_mod, 0x00711641u);

    return caller >= create_list_lo && caller < create_list_hi;
}

static int caller_in_inventorymenu_setupsubcategorybuttons_range(uintptr_t lr) {
    const uintptr_t caller = lr & ~(uintptr_t)1u;
    const uintptr_t setup_lo = so_vaddr_to_abs(&so_mod, 0x0070e279u);
    const uintptr_t setup_hi = so_vaddr_to_abs(&so_mod, 0x0070e59du);

    return caller >= setup_lo && caller < setup_hi;
}

static int caller_in_upgrademenu_setupsubcategorybuttons_range(uintptr_t lr) {
    const uintptr_t caller = lr & ~(uintptr_t)1u;
    const uintptr_t setup_lo = so_vaddr_to_abs(&so_mod, 0x0070ad89u);
    const uintptr_t setup_hi = so_vaddr_to_abs(&so_mod, 0x0070b0c9u);

    return caller >= setup_lo && caller < setup_hi;
}

static int shopscene_preview_skip_active(uintptr_t lr) {
    if (!g_shopscene_preview_broken)
        return 0;
    if (g_shopscene_preview_context > 0)
        return 1;
    if (caller_in_shopscene_load_range(lr))
        return 1;
    return 0;
}

static void shopscene_preview_mark_bad_ftc(void *self) {
    if ((uintptr_t)self < 0x10000u)
        return;

    for (unsigned i = 0; i < sizeof(g_shopscene_preview_bad_ftc) / sizeof(g_shopscene_preview_bad_ftc[0]); i++) {
        if (g_shopscene_preview_bad_ftc[i] == self)
            return;
    }

    for (unsigned i = 0; i < sizeof(g_shopscene_preview_bad_ftc) / sizeof(g_shopscene_preview_bad_ftc[0]); i++) {
        if (!g_shopscene_preview_bad_ftc[i]) {
            g_shopscene_preview_bad_ftc[i] = self;
            return;
        }
    }
}

static int shopscene_preview_is_bad_ftc(void *self) {
    if ((uintptr_t)self < 0x10000u)
        return 0;

    for (unsigned i = 0; i < sizeof(g_shopscene_preview_bad_ftc) / sizeof(g_shopscene_preview_bad_ftc[0]); i++) {
        if (g_shopscene_preview_bad_ftc[i] == self)
            return 1;
    }

    return 0;
}

static int ftccharacter_playframe_dispatch_suspicious(void *self, uintptr_t *dispatch_out) {
    uintptr_t dispatch = 0;

    if ((uintptr_t)self < 0x10000u)
        return 1;

    dispatch = *(const uintptr_t *)((const uint8_t *)self + 0x4c4u);
    if (dispatch_out)
        *dispatch_out = dispatch;

    if (!dispatch)
        return 1;
    if (!ptr_is_plausible_exec(dispatch))
        return 1;

    {
        const uintptr_t fn = dispatch & ~(uintptr_t)1u;
        const uintptr_t ftc_core_lo = so_vaddr_to_abs(&so_mod, 0x005f8760u);
        const uintptr_t ftc_core_hi = so_vaddr_to_abs(&so_mod, 0x005f943du);

        // A frame event dispatching back into FTCCharacter core methods is the
        // crash pattern seen at LR=FTCCharacter::PlayFrame+0x9a. Real gameplay
        // handlers live elsewhere, e.g. Character::AttackAnimation_AnimationFrame.
        if (fn >= ftc_core_lo && fn < ftc_core_hi)
            return 1;
    }

    return 0;
}

static void *actiontimelinecache_fallback_timeline(const char *which, void *self, const void *path, void *lr, void *timeline) {
    static int warn_count = 0;

    if (g_shopscene_preview_context > 0)
        g_shopscene_preview_broken = 1;

    if (warn_count < 24) {
        DLA_DEBUG_PRINTF("[so_patch][WARN] %s fallback self=%p path=%s raw=%p caller=%p timeline=%p\n",
                     which, self, csloader_path_preview(path), path, lr, timeline);
        warn_count++;
    }

    // Same autorelease-pool race fix as csloader_fallback_timeline: retain the
    // dummy object so callers can safely add it to a scene without it being
    // freed under them before they call retain() themselves.
    void *tl = g_cocostudio_actiontimeline_create ? g_cocostudio_actiontimeline_create() : NULL;
    if (tl && g_cocos_ref_retain_raw)
        g_cocos_ref_retain_raw(tl);
    return tl;
}

static void *actiontimelinecache_load_flatbuffers_guard(void *self, const void *path) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFlatBuffersFile invalid self", self, path, lr, NULL);
    if ((uintptr_t)path < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFlatBuffersFile invalid path", self, path, lr, NULL);
    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFlatBuffersFile preview fallback active", self, path, lr, NULL);

    void *timeline = SO_CONTINUE_TYPED(void *, actiontimelinecache_load_flatbuffers_fn,
                                       g_actiontimelinecache_load_flatbuffers_hook, self, path);
    if ((uintptr_t)timeline < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFlatBuffersFile returned invalid timeline", self, path, lr, timeline);

    return timeline;
}

static void *actiontimelinecache_load_file_guard(void *self, const void *path) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFile invalid self", self, path, lr, NULL);
    if ((uintptr_t)path < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFile invalid path", self, path, lr, NULL);
    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFile preview fallback active", self, path, lr, NULL);

    void *timeline = SO_CONTINUE_TYPED(void *, actiontimelinecache_load_file_fn,
                                       g_actiontimelinecache_load_file_hook, self, path);
    if ((uintptr_t)timeline < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::loadAnimationActionWithFile returned invalid timeline", self, path, lr, timeline);

    return timeline;
}

static void *actiontimelinecache_create_flatbuffers_guard(void *self, const void *path) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createActionWithFlatBuffersFile invalid self", self, path, lr, NULL);
    if ((uintptr_t)path < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createActionWithFlatBuffersFile invalid path", self, path, lr, NULL);
    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createActionWithFlatBuffersFile preview fallback active", self, path, lr, NULL);

    void *timeline = SO_CONTINUE_TYPED(void *, actiontimelinecache_create_flatbuffers_fn,
                                       g_actiontimelinecache_create_flatbuffers_hook, self, path);
    if ((uintptr_t)timeline < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createActionWithFlatBuffersFile returned invalid timeline", self, path, lr, timeline);

    return timeline;
}

static void *actiontimelinecache_create_action_guard(void *self, const void *path) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createAction invalid self", self, path, lr, NULL);
    if ((uintptr_t)path < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createAction invalid path", self, path, lr, NULL);
    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createAction preview fallback active", self, path, lr, NULL);

    void *timeline = SO_CONTINUE_TYPED(void *, actiontimelinecache_create_action_fn,
                                       g_actiontimelinecache_create_action_hook, self, path);
    if ((uintptr_t)timeline < 0x10000u)
        return actiontimelinecache_fallback_timeline("ActionTimelineCache::createAction returned invalid timeline", self, path, lr, timeline);

    return timeline;
}

static int ftccharacter_playanimation_guard(void *self, const void *anim_name, int loop_mode, float speed, vec2_arg blend_pos, int flags) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayAnimation invalid self=%p caller=%p; ignoring\n",
                         self, lr);
        }
        return 0;
    }

#if SO_PATCH_SHOPSCENE_PLAYANIMATION_STUB
    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0) {
        static int warned_context = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned_context) {
            warned_context = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayAnimation preview fallback active self=%p caller=%p\n",
                         self, lr);
        }
        (void)anim_name;
        (void)speed;
        (void)blend_pos;
        return 0;
    }

    if (caller_in_shopscene_load_range((uintptr_t)lr)) {
        static int warned = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayAnimation shop-load stub active self=%p caller=%p mode=%d flags=%d\n",
                         self, lr, loop_mode, flags);
        }
        (void)anim_name;
        (void)speed;
        (void)blend_pos;
        return 0;
    }
#endif

    return SO_CONTINUE_TYPED(int, ftccharacter_playanimation_fn, g_ftccharacter_playanimation_hook,
                             self, anim_name, loop_mode, speed, blend_pos, flags);
}

static void ftccharacter_update_guard(void *self, float dt) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::Update invalid self=%p caller=%p; ignoring\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0) {
        static int warned_context = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned_context) {
            warned_context = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::Update preview fallback active self=%p caller=%p\n",
                         self, lr);
        }
        (void)dt;
        return;
    }

    if (caller_in_shopscene_load_range((uintptr_t)lr)) {
        static int warned = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::Update shop-load stub active self=%p caller=%p\n",
                         self, lr);
        }
        (void)dt;
        return;
    }

    SO_CONTINUE_TYPED_VOID(ftccharacter_update_fn, g_ftccharacter_update_hook, self, dt);
}

static void ftccharacter_updateoverlays_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::UpdateOverlays invalid self=%p caller=%p; ignoring\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken && g_shopscene_preview_context > 0) {
        static int warned_context = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned_context) {
            warned_context = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::UpdateOverlays preview fallback active self=%p caller=%p\n",
                         self, lr);
        }
        return;
    }

    if (caller_in_shopscene_load_range((uintptr_t)lr)) {
        static int warned = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::UpdateOverlays shop-load stub active self=%p caller=%p\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(ftccharacter_updateoverlays_fn, g_ftccharacter_updateoverlays_hook, self);
}

static void ftccharacter_playframe_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayFrame invalid self=%p caller=%p; ignoring\n",
                         self, lr);
        }
        return;
    }

    {
        uintptr_t dispatch = 0;
        if (ftccharacter_playframe_dispatch_suspicious(self, &dispatch)) {
            static int warned_dispatch = 0;
            if (warned_dispatch < 16) {
                DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayFrame suspicious dispatch self=%p dispatch=%p caller=%p; skipping frame\n",
                             self, (void *)dispatch, lr);
                warned_dispatch++;
            }
            return;
        }
    }

    if (shopscene_preview_is_bad_ftc(self)) {
        static int warned_broken = 0;
        if (warned_broken < 8) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayFrame shop preview character self=%p caller=%p; skipping frame dispatch\n",
                         self, lr);
            warned_broken++;
        }
        return;
    }

    if (caller_in_shopscene_load_range((uintptr_t)lr)) {
        static int warned = 0;
        shopscene_preview_mark_bad_ftc(self);
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] FTCCharacter::PlayFrame shop-load stub active self=%p caller=%p\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(ftccharacter_playframe_fn, g_ftccharacter_playframe_hook, self);
}

static void shopmenubase_createbuttons_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopMenuBase::CreateButtons invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (shopscene_preview_skip_active((uintptr_t)lr)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopMenuBase::CreateButtons preview fallback active self=%p caller=%p; skipping button build\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(shopmenubase_createbuttons_fn, g_shopmenubase_createbuttons_hook, self);
}

static void shopmenubase_createlabels_guard(void *self, float ui_scale) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopMenuBase::CreateLabels invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

#if SO_PATCH_SHOPMENUBASE_CREATELABELS_SAFE_STUB
    {
        static int warned = 0;
        g_shopscene_preview_broken = 1;
        if (!warned) {
            union {
                float f;
                uint32_t u;
            } scale_bits;
            scale_bits.f = ui_scale;
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopMenuBase::CreateLabels safe-stub active self=%p caller=%p scale=0x%08x; skipping label build\n",
                         self, lr, (unsigned)scale_bits.u);
        }
        return;
    }
#endif

    if (shopscene_preview_skip_active((uintptr_t)lr)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ShopMenuBase::CreateLabels preview fallback active self=%p caller=%p; skipping label build\n",
                         self, lr);
        }
        (void)ui_scale;
        return;
    }

    SO_CONTINUE_TYPED_VOID(shopmenubase_createlabels_fn, g_shopmenubase_createlabels_hook, self, ui_scale);
}

static void inventorymenubase_setupcharacterinfo_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::SetupCharacterInfo invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::SetupCharacterInfo preview fallback active self=%p caller=%p; skipping info setup\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(inventorymenubase_setupcharacterinfo_fn, g_inventorymenubase_setupcharacterinfo_hook, self);
}

static void inventorymenubase_updatecharacterinfo_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::UpdateCharacterInfo invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::UpdateCharacterInfo preview fallback active self=%p caller=%p; skipping info refresh\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(inventorymenubase_updatecharacterinfo_fn, g_inventorymenubase_updatecharacterinfo_hook, self);
}

static void inventorymenubase_showbottomline_guard(void *self, int visible) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::ShowBottomLine invalid self=%p caller=%p visible=%d; skipping\n",
                         self, lr, visible);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::ShowBottomLine preview fallback active self=%p caller=%p; skipping bottom line\n",
                         self, lr);
        }
        (void)visible;
        return;
    }

    SO_CONTINUE_TYPED_VOID(inventorymenubase_showbottomline_fn, g_inventorymenubase_showbottomline_hook, self, visible);
}

static void inventorymenubase_createlist_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::CreateList invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenuBase::CreateList preview fallback active self=%p caller=%p; skipping list build\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(inventorymenubase_createlist_fn, g_inventorymenubase_createlist_hook, self);
}

static void inventorymenu_setupsubcategorybuttons_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenu::SetupSubcategoryButtons invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] InventoryMenu::SetupSubcategoryButtons preview fallback active self=%p caller=%p; skipping subcategory button setup\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(inventorymenu_setupsubcategorybuttons_fn, g_inventorymenu_setupsubcategorybuttons_hook, self);
}

static void upgrademenu_setupsubcategorybuttons_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] UpgradeMenu::SetupSubcategoryButtons invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] UpgradeMenu::SetupSubcategoryButtons preview fallback active self=%p caller=%p; skipping subcategory button setup\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(upgrademenu_setupsubcategorybuttons_fn, g_upgrademenu_setupsubcategorybuttons_hook, self);
}

static void labelsprite_updatevalue_guard(void *self) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] LabelSprite::UpdateValue invalid self=%p caller=%p; skipping\n",
                         self, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken && caller_in_inventorymenu_info_range((uintptr_t)lr)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] LabelSprite::UpdateValue preview fallback active self=%p caller=%p; skipping label refresh\n",
                         self, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(labelsprite_updatevalue_fn, g_labelsprite_updatevalue_hook, self);
}

static int c_uxelement_skip_label_update(const char *which, void *self, void *lr) {
    if ((uintptr_t)self < 0x10000u) {
        static int invalid_warn_count = 0;
        if (invalid_warn_count < 8) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] %s invalid self=%p caller=%p; skipping label update\n",
                         which, self, lr);
            invalid_warn_count++;
        }
        return 1;
    }

#if SO_PATCH_C_UXELEMENT_LABEL_SAFE_STUB
    if (shopscene_preview_skip_active((uintptr_t)lr)) {
        static int fallback_warn_count = 0;
        if (fallback_warn_count < 16) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] %s shop fallback active self=%p caller=%p; skipping label update\n",
                         which, self, lr);
            fallback_warn_count++;
        }
        return 1;
    }
#endif

    return 0;
}

static void c_uxelement_setlabel_plain_guard(void *self, const void *text) {
    void *lr = NULL;
    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (c_uxelement_skip_label_update("C_UXElement::SetLabelString(text)", self, lr))
        return;

    SO_CONTINUE_TYPED_VOID(c_uxelement_setlabel_plain_fn, g_c_uxelement_setlabel_plain_hook, self, text);
}

static void c_uxelement_setlabel_three_guard(void *self, const void *text, const void *prefix, const void *suffix) {
    void *lr = NULL;
    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (c_uxelement_skip_label_update("C_UXElement::SetLabelString(text,prefix,suffix)", self, lr))
        return;

    SO_CONTINUE_TYPED_VOID(c_uxelement_setlabel_three_fn, g_c_uxelement_setlabel_three_hook, self, text, prefix, suffix);
}

static void c_uxelement_setlabel_text_guard(void *self, int text_type, int text_id) {
    void *lr = NULL;
    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (c_uxelement_skip_label_update("C_UXElement::SetLabelString(TextType,int)", self, lr))
        return;

    SO_CONTINUE_TYPED_VOID(c_uxelement_setlabel_text_fn, g_c_uxelement_setlabel_text_hook, self, text_type, text_id);
}

static void c_uxelement_setlabel_typed_guard(void *self, const void *prefix, int text_type, int text_id, const void *suffix) {
    void *lr = NULL;
    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if (c_uxelement_skip_label_update("C_UXElement::SetLabelString(prefix,TextType,int,suffix)", self, lr))
        return;

    SO_CONTINUE_TYPED_VOID(c_uxelement_setlabel_typed_fn, g_c_uxelement_setlabel_typed_hook,
                           self, prefix, text_type, text_id, suffix);
}

static void *sprite_create_empty_fallback(const char *which, void *sprite_frame, void *lr, void *result) {
    static int warn_count = 0;
    void *sprite = g_cocos_sprite_create_raw ? g_cocos_sprite_create_raw() : NULL;

    if (warn_count < 16) {
        DLA_DEBUG_PRINTF("[so_patch][WARN] %s fallback frame=%p caller=%p result=%p empty=%p\n",
                     which, sprite_frame, lr, result, sprite);
        warn_count++;
    }

    return sprite;
}

static void *sprite_create_with_sprite_frame_guard(void *sprite_frame) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)sprite_frame < 0x10000u)
        return sprite_create_empty_fallback("Sprite::createWithSpriteFrame invalid frame", sprite_frame, lr, NULL);

    void *sprite = SO_CONTINUE_TYPED(void *, sprite_create_with_sprite_frame_fn,
                                     g_sprite_create_with_sprite_frame_hook, sprite_frame);
    if ((uintptr_t)sprite < 0x10000u)
        return sprite_create_empty_fallback("Sprite::createWithSpriteFrame returned invalid sprite", sprite_frame, lr, sprite);

    return sprite;
}

static void sprite_set_sprite_frame_guard(void *self, void *sprite_frame) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u || (uintptr_t)sprite_frame < 0x10000u) {
        static int warn_count = 0;
        if (warn_count < 16) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] Sprite::setSpriteFrame invalid self=%p frame=%p caller=%p; skipping\n",
                         self, sprite_frame, lr);
            warn_count++;
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(sprite_set_sprite_frame_fn, g_sprite_set_sprite_frame_hook, self, sprite_frame);
}

static void vec2_copy_or_zero(void *out, const void *point) {
    if ((uintptr_t)out < 0x10000u)
        return;

    if ((uintptr_t)point >= 0x10000u) {
        ((uint32_t *)out)[0] = ((const uint32_t *)point)[0];
        ((uint32_t *)out)[1] = ((const uint32_t *)point)[1];
    } else {
        ((uint32_t *)out)[0] = 0;
        ((uint32_t *)out)[1] = 0;
    }
}

static void node_convert_to_worldspace_guard(void *out, const void *self, const void *point) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u) {
        static int warn_count = 0;
        if (caller_in_inventorymenu_createlist_range((uintptr_t)lr))
            g_shopscene_preview_broken = 1;
        if (warn_count < 8) {
            DLA_DEBUG_PRINTF("[so_patch][WARN] cocos2d::Node::convertToWorldSpace null self=%p point=%p out=%p caller=%p; returning input point\n",
                         self, point, out, lr);
            warn_count++;
        }
        vec2_copy_or_zero(out, point);
        return;
    }

    SO_CONTINUE_TYPED_VOID(node_convert_space_sret_fn, g_node_convert_to_worldspace_hook, out, self, point);
}

static void listview_addeventlistener_guard(void *self, const void *callback) {
    void *lr = NULL;

    __asm__ volatile ("mov %0, lr" : "=r"(lr));

    if ((uintptr_t)self < 0x10000u || (uintptr_t)callback < 0x10000u) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ListView::addEventListener invalid self=%p callback=%p caller=%p; skipping\n",
                         self, callback, lr);
        }
        return;
    }

    if (g_shopscene_preview_broken &&
        (caller_in_inventorymenu_setupsubcategorybuttons_range((uintptr_t)lr) ||
         caller_in_upgrademenu_setupsubcategorybuttons_range((uintptr_t)lr))) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            DLA_DEBUG_PRINTF("[so_patch][WARN] ListView::addEventListener shop fallback active self=%p callback=%p caller=%p; skipping listener\n",
                         self, callback, lr);
        }
        return;
    }

    SO_CONTINUE_TYPED_VOID(listview_addeventlistener_fn, g_listview_addeventlistener_hook, self, callback);
}

static int ptr_is_plausible_data(uintptr_t p) {
    if ((p >= 0x81000000u && p < 0x84000000u) ||
        (p >= 0x84000000u && p < 0x88000000u) ||
        (p >= 0x98000000u && p < 0x9C000000u))
        return 1;
    return 0;
}

static int ptr_is_in_so_data(uintptr_t p) {
    uintptr_t v = p & ~(uintptr_t)1u;
    for (int i = 0; i < so_mod.n_data; i++) {
        uintptr_t lo = (uintptr_t)so_mod.data_base[i];
        uintptr_t hi = lo + (uintptr_t)so_mod.data_size[i];
        if (v >= lo && v < hi)
            return 1;
    }
    return 0;
}

static int ptr_is_so_text_vaddr(uintptr_t p) {
    uintptr_t v = p & ~(uintptr_t)1u;
    uintptr_t lo = (uintptr_t)so_mod.text_start;
    uintptr_t hi = lo + (uintptr_t)so_mod.text_size;
    return (v >= lo && v < hi);
}

static int ptr_is_android_so_text(uintptr_t p) {
    uintptr_t v = p & ~(uintptr_t)1u;
    static const uintptr_t android_bases[] = { 0x30000000u, 0x31000000u };
    for (unsigned i = 0; i < (unsigned)(sizeof(android_bases) / sizeof(android_bases[0])); i++) {
        uintptr_t lo = android_bases[i] + (uintptr_t)so_mod.text_start;
        uintptr_t hi = lo + (uintptr_t)so_mod.text_size;
        if (v >= lo && v < hi)
            return 1;
    }
    return 0;
}

static int ptr_looks_like_so_code(uintptr_t p) {
    return ptr_is_plausible_exec(p) || ptr_is_so_text_vaddr(p) || ptr_is_android_so_text(p);
}

static int scheduler_callback_descriptor_has_exec(const uintptr_t *w) {
    if (!ptr_is_in_so_data(w[0]))
        return 0;

    const uintptr_t *desc = (const uintptr_t *)(w[0] & ~(uintptr_t)1u);
    if (!ptr_is_plausible_data((uintptr_t)desc))
        return 0;

    for (int i = 0; i < 8; i++) {
        if (ptr_looks_like_so_code(desc[i]))
            return 1;
    }

    return 0;
}

static int scheduler_callback_suspicious(const void *callback_fn, void *target) {
    if (!target)
        return 1;
    if (!callback_fn)
        return 1;
    if (!ptr_is_plausible_data((uintptr_t)callback_fn))
        return 1;

    const uintptr_t *w = (const uintptr_t *)callback_fn;
    const uintptr_t *desc = NULL;
    int has_exec = 0;
    int has_heapish = 0;
    for (int i = 0; i < 4; i++) {
        uintptr_t p = w[i];
        if (!p)
            continue;
        if (ptr_looks_like_so_code(p))
            has_exec = 1;
        if (p >= 0x82000000u && p < 0x88000000u)
            has_heapish = 1;
    }

    // libc++ std::function closures often store a descriptor in SO data and
    // the bound target object in the next word. Recognize that indirect shape
    // so we don't reject valid schedule callbacks.
    if (ptr_is_in_so_data(w[0])) {
        desc = (const uintptr_t *)(w[0] & ~(uintptr_t)1u);
        if (!has_exec && scheduler_callback_descriptor_has_exec(w))
            has_exec = 1;
    }

    // Dark Lands uses schedulePerFrame during nativeInit with a std::function
    // closure that points at a descriptor in SO data and stores the target in
    // the next word. Treat that as valid even if we couldn't immediately find
    // the eventual code pointer in the descriptor snapshot.
    if (!has_exec && desc && ptr_is_plausible_data((uintptr_t)desc) &&
        w[1] == (uintptr_t)target && target) {
        return 0;
    }

    // Never accept callback payloads that directly alias target in the
    // descriptor slot. The target slot itself is normal for bound callbacks;
    // only treat it as suspicious when we still failed to find any code path.
    if (w[0] == (uintptr_t)target)
        return 1;
    if (w[1] == (uintptr_t)target && !has_exec)
        return 1;

    // No code pointer found in first slots but heap-ish payload exists.
    return (!has_exec && has_heapish);
}

static void scheduler_log_call(const char *which, void *scheduler, const void *callback_fn, void *target, const void *key) {
    static int log_count = 0;
    if (log_count >= 24)
        return;
    log_count++;

    if (ptr_is_plausible_data((uintptr_t)callback_fn)) {
        const uintptr_t *w = (const uintptr_t *)callback_fn;
        DLA_DEBUG_PRINTF("[so_patch][DBG] %s sched=%p cb=%p target=%p key=%p cbw=[%08x %08x %08x %08x]\n",
                     which, scheduler, callback_fn, target, key,
                     (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
    } else {
        DLA_DEBUG_PRINTF("[so_patch][DBG] %s sched=%p cb=%p target=%p key=%p (cb ptr not plausible data)\n",
                     which, scheduler, callback_fn, target, key);
    }
}

static void scheduler_log_descriptor(const char *which, const void *callback_fn) {
    if (!ptr_is_plausible_data((uintptr_t)callback_fn))
        return;

    const uintptr_t *w = (const uintptr_t *)callback_fn;
    if (!ptr_is_in_so_data(w[0]))
        return;

    const uintptr_t *desc = (const uintptr_t *)(w[0] & ~(uintptr_t)1u);
    if (!ptr_is_plausible_data((uintptr_t)desc))
        return;

    DLA_DEBUG_PRINTF("[so_patch][DBG] %s desc=%p dw=[%08x %08x %08x %08x %08x %08x]\n",
                 which, desc,
                 (unsigned)desc[0], (unsigned)desc[1], (unsigned)desc[2],
                 (unsigned)desc[3], (unsigned)desc[4], (unsigned)desc[5]);
}

static void cocos2d_scheduler_schedule_per_frame_guard(
    void *scheduler,
    const void *callback_fn,
    void *target,
    int priority,
    int paused
) {
    scheduler_log_call("schedulePerFrame", scheduler, callback_fn, target, NULL);

    if (scheduler_callback_suspicious(callback_fn, target)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            if (ptr_is_plausible_data((uintptr_t)callback_fn)) {
                const uintptr_t *w = (const uintptr_t *)callback_fn;
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedulePerFrame suspicious callback target=%p fn=[%08x %08x %08x %08x]\n",
                             target, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
                scheduler_log_descriptor("schedulePerFrame", callback_fn);
            } else {
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedulePerFrame suspicious callback target=%p fn=%p (unreadable)\n",
                             target, callback_fn);
            }
        }
#if SO_PATCH_SCHEDULER_DROP_SUSPICIOUS
        return;
#endif
    }

    SO_CONTINUE_TYPED_VOID(scheduler_schedule_per_frame_fn, g_scheduler_schedule_per_frame_hook, scheduler, callback_fn, target, priority, paused);
}

static void cocos2d_scheduler_schedule_guard(
    void *scheduler,
    const void *callback_fn,
    void *target,
    float interval,
    int paused,
    const void *key
) {
    (void)interval;
    scheduler_log_call("schedule", scheduler, callback_fn, target, key);
    if (scheduler_callback_suspicious(callback_fn, target)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            if (ptr_is_plausible_data((uintptr_t)callback_fn)) {
                const uintptr_t *w = (const uintptr_t *)callback_fn;
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedule suspicious callback target=%p fn=[%08x %08x %08x %08x]\n",
                             target, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
                scheduler_log_descriptor("schedule", callback_fn);
            } else {
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedule suspicious callback target=%p fn=%p (unreadable)\n",
                             target, callback_fn);
            }
        }
#if SO_PATCH_SCHEDULER_DROP_SUSPICIOUS
        return;
#endif
    }
    SO_CONTINUE_TYPED_VOID(scheduler_schedule_fn, g_scheduler_schedule_hook, scheduler, callback_fn, target, interval, paused, key);
}

static void cocos2d_scheduler_schedule_ex_guard(
    void *scheduler,
    const void *callback_fn,
    void *target,
    unsigned repeat,
    float interval,
    int paused,
    const void *key
) {
    (void)repeat;
    (void)interval;
    scheduler_log_call("scheduleEx", scheduler, callback_fn, target, key);
    if (scheduler_callback_suspicious(callback_fn, target)) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            if (ptr_is_plausible_data((uintptr_t)callback_fn)) {
                const uintptr_t *w = (const uintptr_t *)callback_fn;
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedule(ex) suspicious callback target=%p fn=[%08x %08x %08x %08x]\n",
                             target, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
                scheduler_log_descriptor("scheduleEx", callback_fn);
            } else {
                DLA_DEBUG_PRINTF("[so_patch][WARN] dropping Scheduler::schedule(ex) suspicious callback target=%p fn=%p (unreadable)\n",
                             target, callback_fn);
            }
        }
#if SO_PATCH_SCHEDULER_DROP_SUSPICIOUS
        return;
#endif
    }
    SO_CONTINUE_TYPED_VOID(scheduler_schedule_ex_fn, g_scheduler_schedule_ex_hook, scheduler, callback_fn, target, repeat, interval, paused, key);
}

static inline uintptr_t so_vaddr_to_abs(const so_module *m, uintptr_t vaddr) {
    return (uintptr_t)m->text_base + (vaddr - (uintptr_t)m->text_start);
}

void so_patch(void) {
    DLA_DEBUG_PRINTF("so_patch: start\n");

#if SO_PATCH_FTCPARSER_READ_GUARD
    g_cocos_data_getbytes = (cocos_data_getbytes_fn)(uintptr_t)so_symbol(&so_mod, "_ZNK7cocos2d4Data8getBytesEv");
    g_cocos_data_getsize = (cocos_data_getsize_fn)(uintptr_t)so_symbol(&so_mod, "_ZNK7cocos2d4Data7getSizeEv");
    DLA_DEBUG_PRINTF("[so_patch] cocos2d::Data helpers getBytes=%p getSize=%p\n",
                 g_cocos_data_getbytes, g_cocos_data_getsize);
#endif

    uintptr_t sym = (uintptr_t)so_symbol(&so_mod, "__emutls_get_address");

    if (!sym) {
        // If the Android .so does not export the symbol in .dynsym, so_symbol() will fail.
        // In that case, fall back to known vaddrs for this libcocos2dcpp build.
        // Keep mode bit in the candidate when known.
        const uintptr_t candidates[] = { 0x00FF45CD, 0x00FF0360 };
        for (unsigned i = 0; i < (unsigned)(sizeof(candidates) / sizeof(candidates[0])); i++) {
            uintptr_t abs = so_vaddr_to_abs(&so_mod, candidates[i] & ~(uintptr_t)1u) | (candidates[i] & 1u);
            uintptr_t abs_no_thumb = abs & ~(uintptr_t)1u;
            if (abs_no_thumb >= (uintptr_t)so_mod.text_base &&
                abs_no_thumb < (uintptr_t)so_mod.text_base + (uintptr_t)so_mod.text_size) {
                sym = abs;
                DLA_DEBUG_PRINTF("[so_patch] __emutls_get_address not in dynsym; using fallback vaddr=0x%08x abs=0x%08x\n",
                             (unsigned)(candidates[i] & ~(uintptr_t)1u), (unsigned)abs);
                break;
            }
        }
    }

    if (sym) {
        // Preserve the source mode (ARM/Thumb). Forcing Thumb on ARM symbols
        // corrupts the target function and can crash during constructors.
        hook_addr(sym, (uintptr_t)&__emutls_get_address);
        DLA_DEBUG_PRINTF("[so_patch] hooked __emutls_get_address @0x%08x (%s) -> 0x%08x\n",
                     (unsigned)(sym & ~(uintptr_t)1u), (sym & 1u) ? "Thumb" : "ARM",
                     (unsigned)(uintptr_t)&__emutls_get_address);
    } else {
        DLA_DEBUG_PRINTF("[so_patch] __emutls_get_address not found; no hook applied\n");
    }

    uintptr_t voronoi = (uintptr_t)so_symbol(&so_mod, "_ZN22btVoronoiSimplexSolver28updateClosestVectorAndPointsEv");
    if (voronoi) {
        g_bt_voronoi_update_hook = hook_addr(voronoi, (uintptr_t)&btVoronoi_updateClosest_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked btVoronoiSimplexSolver::updateClosestVectorAndPoints @0x%08x (%s)\n",
                     (unsigned)(voronoi & ~(uintptr_t)1u), (voronoi & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] btVoronoiSimplexSolver::updateClosestVectorAndPoints not found; no hook applied\n");
    }

    // Disable Cocos2d debug console command registration on Vita.
    // The command-building path (std::function/std::bind heavy) crashes in
    // Game_nativeInit for this binary and is not required for gameplay.
    static const char *console_create_syms[] = {
        "_ZN7cocos2d7Console22createCommandAllocatorEv",
        "_ZN7cocos2d7Console19createCommandConfigEv",
        "_ZN7cocos2d7Console21createCommandDebugMsgEv",
        "_ZN7cocos2d7Console21createCommandDirectorEv",
        "_ZN7cocos2d7Console17createCommandExitEv",
        "_ZN7cocos2d7Console22createCommandFileUtilsEv",
        "_ZN7cocos2d7Console16createCommandFpsEv",
        "_ZN7cocos2d7Console17createCommandHelpEv",
        "_ZN7cocos2d7Console23createCommandProjectionEv",
        "_ZN7cocos2d7Console23createCommandResolutionEv",
        "_ZN7cocos2d7Console23createCommandSceneGraphEv",
        "_ZN7cocos2d7Console20createCommandTextureEv",
        "_ZN7cocos2d7Console18createCommandTouchEv",
        "_ZN7cocos2d7Console19createCommandUploadEv",
        "_ZN7cocos2d7Console20createCommandVersionEv",
    };

    int console_hooks = 0;
    for (unsigned i = 0; i < (unsigned)(sizeof(console_create_syms) / sizeof(console_create_syms[0])); i++) {
        uintptr_t addr = (uintptr_t)so_symbol(&so_mod, console_create_syms[i]);
        if (!addr)
            continue;
        hook_addr(addr, (uintptr_t)&cocos_console_create_command_noop);
        console_hooks++;
    }
    DLA_DEBUG_PRINTF("[so_patch] console createCommand hooks applied: %d\n", console_hooks);

    static const char *console_disable_syms[] = {
        "_ZN7cocos2d7Console11listenOnTCPEi",
        "_ZN7cocos2d7Console22listenOnFileDescriptorEi",
        "_ZN7cocos2d7Console4loopEv",
        "_ZN7cocos2d7Console10addCommandERKNS0_7CommandE",
        "_ZN7cocos2d7Console13addSubCommandERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEERKNS0_7CommandE",
        "_ZN7cocos2d7Console13addSubCommandERNS0_7CommandERKS1_",
    };

    int console_disable_hooks = 0;
    for (unsigned i = 0; i < (unsigned)(sizeof(console_disable_syms) / sizeof(console_disable_syms[0])); i++) {
        uintptr_t addr = (uintptr_t)so_symbol(&so_mod, console_disable_syms[i]);
        if (!addr)
            continue;
        hook_addr(addr, (uintptr_t)&cocos_console_ret0);
        console_disable_hooks++;
    }
    DLA_DEBUG_PRINTF("[so_patch] console hard-disable hooks applied: %d\n", console_disable_hooks);

    uintptr_t sched_pf = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d9Scheduler16schedulePerFrameERKNSt6__ndk18functionIFvfEEEPvib");
    if (sched_pf) {
        g_scheduler_schedule_per_frame_hook = hook_addr(sched_pf, (uintptr_t)&cocos2d_scheduler_schedule_per_frame_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked Scheduler::schedulePerFrame @0x%08x (%s)\n",
                     (unsigned)(sched_pf & ~(uintptr_t)1u), (sched_pf & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] Scheduler::schedulePerFrame not found; no hook applied\n");
    }

    uintptr_t sched = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d9Scheduler8scheduleERKNSt6__ndk18functionIFvfEEEPvfbRKNS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
    if (sched) {
        g_scheduler_schedule_hook = hook_addr(sched, (uintptr_t)&cocos2d_scheduler_schedule_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked Scheduler::schedule @0x%08x (%s)\n",
                     (unsigned)(sched & ~(uintptr_t)1u), (sched & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] Scheduler::schedule not found; no hook applied\n");
    }

    uintptr_t sched_ex = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d9Scheduler8scheduleERKNSt6__ndk18functionIFvfEEEPvfjfbRKNS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
    if (sched_ex) {
        g_scheduler_schedule_ex_hook = hook_addr(sched_ex, (uintptr_t)&cocos2d_scheduler_schedule_ex_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked Scheduler::schedule(ex) @0x%08x (%s)\n",
                     (unsigned)(sched_ex & ~(uintptr_t)1u), (sched_ex & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] Scheduler::schedule(ex) not found; no hook applied\n");
    }

    uintptr_t op_new = (uintptr_t)so_symbol(&so_mod, "_Znwj");
    if (op_new) {
        g_cpp_operator_new_hook = hook_addr(op_new, (uintptr_t)&operator_new_guarded);
        DLA_DEBUG_PRINTF("[so_patch] hooked operator new @0x%08x (%s)\n",
                     (unsigned)(op_new & ~(uintptr_t)1u), (op_new & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] operator new not found; no hook applied\n");
    }

    uintptr_t get_nh = (uintptr_t)so_symbol(&so_mod, "_ZSt15get_new_handlerv");
    if (get_nh) {
        g_cpp_get_new_handler_hook = hook_addr(get_nh, (uintptr_t)&cpp_get_new_handler_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked std::get_new_handler @0x%08x (%s)\n",
                     (unsigned)(get_nh & ~(uintptr_t)1u), (get_nh & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] std::get_new_handler not found; no hook applied\n");
    }

    uintptr_t set_nh = (uintptr_t)so_symbol(&so_mod, "_ZSt15set_new_handlerPFvvE");
    if (set_nh) {
        g_cpp_set_new_handler_hook = hook_addr(set_nh, (uintptr_t)&cpp_set_new_handler_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked std::set_new_handler @0x%08x (%s)\n",
                     (unsigned)(set_nh & ~(uintptr_t)1u), (set_nh & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] std::set_new_handler not found; no hook applied\n");
    }

    uintptr_t convex_find_edge = (uintptr_t)so_symbol(&so_mod, "_ZN20btConvexHullInternal24findEdgeForCoplanarFacesEPNS_6VertexES1_RPNS_4EdgeES4_S1_S1_");
    if (convex_find_edge) {
        g_bt_convex_find_edge_hook = hook_addr(convex_find_edge, (uintptr_t)&btConvexHullInternal_findEdgeForCoplanarFaces_guard);
        DLA_DEBUG_PRINTF("[so_patch] hooked btConvexHullInternal::findEdgeForCoplanarFaces @0x%08x (%s)\n",
                     (unsigned)(convex_find_edge & ~(uintptr_t)1u), (convex_find_edge & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] btConvexHullInternal::findEdgeForCoplanarFaces not found; no hook applied\n");
    }

#if SO_PATCH_TEXTUREMANAGER_ONADDRESOURCE_NULL_GUARD
    {
        const uintptr_t iter_guard_vaddr = 0x00737300u;
        const uintptr_t iter_continue_vaddr = 0x0073730cu;
        uintptr_t iter_guard_abs = so_vaddr_to_abs(&so_mod, iter_guard_vaddr) | 1u;
        g_texture_manager_onaddresource_iter_continue = so_vaddr_to_abs(&so_mod, iter_continue_vaddr) | 1u;
        g_texture_manager_onaddresource_iter_hook = hook_addr(iter_guard_abs, (uintptr_t)&texture_manager_onaddresource_iter_guard);
        DLA_DEBUG_PRINTF("[so_patch] guarded TextureManager::OnAddResource null iterator @0x%08x -> 0x%08x\n",
                     (unsigned)(iter_guard_abs & ~(uintptr_t)1u),
                     (unsigned)(g_texture_manager_onaddresource_iter_continue & ~(uintptr_t)1u));
    }
#endif

#if SO_PATCH_VALUE_HASH_TABLE_FIND_NULL_GUARD
    {
        uintptr_t value_find = (uintptr_t)so_symbol(&so_mod, "_ZNSt6__ndk112__hash_tableINS_17__hash_value_typeINS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEN7cocos2d5ValueEEENS_22__unordered_map_hasherIS7_SA_NS_4hashIS7_EELb1EEENS_21__unordered_map_equalIS7_SA_NS_8equal_toIS7_EELb1EEENS5_ISA_EEE4findIS7_EENS_15__hash_iteratorIPNS_11__hash_nodeISA_PvEEEERKT_");
        if (value_find) {
            g_value_hash_table_find_hook = hook_addr(value_find, (uintptr_t)&value_hash_table_find_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded unordered_map<string, cocos2d::Value>::find @0x%08x (%s)\n",
                         (unsigned)(value_find & ~(uintptr_t)1u), (value_find & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] unordered_map<string, cocos2d::Value>::find not found; no hook applied\n");
        }
    }
#endif

    g_persister_tree_find = (uintptr_t)so_symbol(&so_mod, "_ZNSt6__ndk16__treeINS_12__value_typeINS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEENS_10unique_ptrIN4game14PersistentDataENS_14default_deleteISA_EEEEEENS_19__map_value_compareIS7_SE_NS_4lessIS7_EELb1EEENS5_ISE_EEE4findIS7_EENS_15__tree_iteratorISE_PNS_11__tree_nodeISE_PvEEiEERKT_");
    DLA_DEBUG_PRINTF("[so_patch] game::Persister property map find %s @0x%08x\n",
                 g_persister_tree_find ? "resolved" : "missing",
                 (unsigned)(g_persister_tree_find & ~(uintptr_t)1u));

    uintptr_t persister_load = (uintptr_t)so_symbol(&so_mod, "_ZN4game9Persister4LoadEv");
    if (persister_load) {
        g_persister_load_hook = hook_addr(persister_load, (uintptr_t)&persister_load_guard);
        DLA_DEBUG_PRINTF("[so_patch] observed game::Persister::Load @0x%08x (%s)\n",
                     (unsigned)(persister_load & ~(uintptr_t)1u), (persister_load & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] game::Persister::Load not found; no hook applied\n");
    }

    uintptr_t persister_save = (uintptr_t)so_symbol(&so_mod, "_ZN4game9Persister4SaveEv");
    if (persister_save) {
        g_persister_save_hook = hook_addr(persister_save, (uintptr_t)&persister_save_guard);
        DLA_DEBUG_PRINTF("[so_patch] observed game::Persister::Save @0x%08x (%s)\n",
                     (unsigned)(persister_save & ~(uintptr_t)1u), (persister_save & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] game::Persister::Save not found; no hook applied\n");
    }

    uintptr_t data_storage_save_local = (uintptr_t)so_symbol(&so_mod, "_ZN4game11DataStorage9SaveLocalEb");
    if (data_storage_save_local) {
        g_data_storage_save_local_hook = hook_addr(data_storage_save_local, (uintptr_t)&data_storage_save_local_guard);
        DLA_DEBUG_PRINTF("[so_patch] observed game::DataStorage::SaveLocal @0x%08x (%s)\n",
                     (unsigned)(data_storage_save_local & ~(uintptr_t)1u), (data_storage_save_local & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] game::DataStorage::SaveLocal not found; no hook applied\n");
    }

    uintptr_t game_progress_save_level = (uintptr_t)so_symbol(&so_mod, "_ZN4game12GameProgress9SaveLevelEiRKNS_13LevelProgressE");
    if (game_progress_save_level) {
        g_game_progress_save_level_hook = hook_addr(game_progress_save_level, (uintptr_t)&game_progress_save_level_guard);
        DLA_DEBUG_PRINTF("[so_patch] observed game::GameProgress::SaveLevel @0x%08x (%s)\n",
                     (unsigned)(game_progress_save_level & ~(uintptr_t)1u), (game_progress_save_level & 1u) ? "Thumb" : "ARM");
    } else {
        DLA_DEBUG_PRINTF("[so_patch] game::GameProgress::SaveLevel not found; no hook applied\n");
    }

#if SO_PATCH_NODE_ADDCHILD_NULL_GUARD
    {
        uintptr_t node_addchild = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node8addChildEPS0_");
        if (node_addchild) {
            g_node_addchild_hook = hook_addr(node_addchild, (uintptr_t)&node_addchild_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::addChild(Node*) @0x%08x (%s)\n",
                         (unsigned)(node_addchild & ~(uintptr_t)1u), (node_addchild & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::addChild(Node*) not found; no hook applied\n");
        }

        uintptr_t node_addchild_z = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node8addChildEPS0_i");
        if (node_addchild_z) {
            g_node_addchild_z_hook = hook_addr(node_addchild_z, (uintptr_t)&node_addchild_z_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::addChild(Node*,z) @0x%08x (%s)\n",
                         (unsigned)(node_addchild_z & ~(uintptr_t)1u), (node_addchild_z & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::addChild(Node*,z) not found; no hook applied\n");
        }

        uintptr_t node_addchild_tag = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node8addChildEPS0_ii");
        if (node_addchild_tag) {
            g_node_addchild_tag_hook = hook_addr(node_addchild_tag, (uintptr_t)&node_addchild_tag_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::addChild(Node*,z,tag) @0x%08x (%s)\n",
                         (unsigned)(node_addchild_tag & ~(uintptr_t)1u), (node_addchild_tag & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::addChild(Node*,z,tag) not found; no hook applied\n");
        }

        uintptr_t node_addchild_name = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node8addChildEPS0_iRKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
        if (node_addchild_name) {
            g_node_addchild_name_hook = hook_addr(node_addchild_name, (uintptr_t)&node_addchild_name_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::addChild(Node*,z,name) @0x%08x (%s)\n",
                         (unsigned)(node_addchild_name & ~(uintptr_t)1u), (node_addchild_name & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::addChild(Node*,z,name) not found; no hook applied\n");
        }

        uintptr_t node_insertchild = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node11insertChildEPS0_i");
        if (node_insertchild) {
            g_node_insertchild_hook = hook_addr(node_insertchild, (uintptr_t)&node_insertchild_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::insertChild @0x%08x (%s)\n",
                         (unsigned)(node_insertchild & ~(uintptr_t)1u), (node_insertchild & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::insertChild not found; no hook applied\n");
        }
    }
#endif

#if SO_PATCH_NODE_ADDCHILDHELPER_NULL_GUARD
    {
        uintptr_t add_child_helper = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node14addChildHelperEPS0_iiRKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEEb");
        if (add_child_helper) {
            g_node_addchildhelper_hook = hook_addr(add_child_helper, (uintptr_t)&node_addchildhelper_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::addChildHelper @0x%08x (%s)\n",
                         (unsigned)(add_child_helper & ~(uintptr_t)1u), (add_child_helper & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::addChildHelper not found; no hook applied\n");
        }
    }
#endif

#if SO_PATCH_SCREENUTILS_NODESETPOS_NULL_GUARD
    {
        uintptr_t nodesetpos_full = (uintptr_t)so_symbol(&so_mod, "_ZN4game11ScreenUtils10NodeSetPosEPN7cocos2d4NodeEiRKNS1_4Vec2ENS_8NodeModeES6_RKNS1_4SizeEb");
        if (nodesetpos_full) {
            g_screenutils_nodesetpos_hook = hook_addr(nodesetpos_full, (uintptr_t)&screenutils_nodesetpos_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ScreenUtils::NodeSetPos(full) @0x%08x (%s)\n",
                         (unsigned)(nodesetpos_full & ~(uintptr_t)1u), (nodesetpos_full & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ScreenUtils::NodeSetPos(full) not found; no hook applied\n");
        }

        uintptr_t nodesetpos_simple = (uintptr_t)so_symbol(&so_mod, "_ZN4game11ScreenUtils10NodeSetPosEPN7cocos2d4NodeEiRKNS1_4Vec2ENS_8NodeModeEb");
        if (nodesetpos_simple) {
            g_screenutils_nodesetpos_simple_hook = hook_addr(nodesetpos_simple, (uintptr_t)&screenutils_nodesetpos_simple_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ScreenUtils::NodeSetPos(simple) @0x%08x (%s)\n",
                         (unsigned)(nodesetpos_simple & ~(uintptr_t)1u), (nodesetpos_simple & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ScreenUtils::NodeSetPos(simple) not found; no hook applied\n");
        }
    }
#endif

#if SO_PATCH_LOADER_SHOWGAMELOGO_SAFE_STUB
    {
        uintptr_t show_game_logo = (uintptr_t)so_symbol(&so_mod, "_ZN4game6Loader12ShowGameLogoEf");
        if (show_game_logo) {
            g_loader_showgamelogo_hook = hook_addr(show_game_logo, (uintptr_t)&loader_showgamelogo_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::Loader::ShowGameLogo @0x%08x (%s)\n",
                         (unsigned)(show_game_logo & ~(uintptr_t)1u), (show_game_logo & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::Loader::ShowGameLogo not found; no hook applied\n");
        }
    }
#endif

#if SO_PATCH_FTCPARSER_READ_GUARD
    {
        uintptr_t ftc_read_int = (uintptr_t)so_symbol(&so_mod, "_ZN4game9FTCParser7ReadIntERKN7cocos2d4DataERi");
        if (ftc_read_int && g_cocos_data_getbytes && g_cocos_data_getsize) {
            g_ftcparser_readint_hook = hook_addr(ftc_read_int, (uintptr_t)&ftcparser_readint_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCParser::ReadInt @0x%08x (%s)\n",
                         (unsigned)(ftc_read_int & ~(uintptr_t)1u), (ftc_read_int & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCParser::ReadInt not guarded (sym=%p getBytes=%p getSize=%p)\n",
                         (void *)ftc_read_int, g_cocos_data_getbytes, g_cocos_data_getsize);
        }

        uintptr_t ftc_read_float = (uintptr_t)so_symbol(&so_mod, "_ZN4game9FTCParser9ReadFloatERKN7cocos2d4DataERi");
        if (ftc_read_float && g_cocos_data_getbytes && g_cocos_data_getsize) {
            g_ftcparser_readfloat_hook = hook_addr(ftc_read_float, (uintptr_t)&ftcparser_readfloat_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCParser::ReadFloat @0x%08x (%s)\n",
                         (unsigned)(ftc_read_float & ~(uintptr_t)1u), (ftc_read_float & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCParser::ReadFloat not guarded (sym=%p getBytes=%p getSize=%p)\n",
                         (void *)ftc_read_float, g_cocos_data_getbytes, g_cocos_data_getsize);
        }
    }
#endif

#if SO_PATCH_COCOS_REF_NULL_GUARD
    {
        uintptr_t ref_retain = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d3Ref6retainEv");
        if (ref_retain) {
            // Store raw function pointer with Thumb bit preserved so direct
            // calls use the correct instruction set.
            g_cocos_ref_retain_raw = (void (*)(void *))ref_retain;
            g_cocos_ref_retain_hook = hook_addr(ref_retain, (uintptr_t)&cocos_ref_retain_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Ref::retain @0x%08x (%s)\n",
                         (unsigned)(ref_retain & ~(uintptr_t)1u), (ref_retain & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Ref::retain not found; no hook applied\n");
        }

        uintptr_t ref_release = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d3Ref7releaseEv");
        if (ref_release) {
            g_cocos_ref_release_raw = (void (*)(void *))ref_release;
            g_cocos_ref_release_hook = hook_addr(ref_release, (uintptr_t)&cocos_ref_release_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Ref::release @0x%08x (%s)\n",
                         (unsigned)(ref_release & ~(uintptr_t)1u), (ref_release & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Ref::release not found; no hook applied\n");
        }

        uintptr_t ref_autorelease = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d3Ref11autoreleaseEv");
        if (ref_autorelease) {
            g_cocos_ref_autorelease_hook = hook_addr(ref_autorelease, (uintptr_t)&cocos_ref_autorelease_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Ref::autorelease @0x%08x (%s)\n",
                         (unsigned)(ref_autorelease & ~(uintptr_t)1u), (ref_autorelease & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Ref::autorelease not found; no hook applied\n");
        }
    }
#endif

#if SO_PATCH_GAMEPLAY_FALLBACK_GUARDS
    {
        uintptr_t node_create = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d4Node6createEv");
        if (node_create) {
            g_cocos_node_create = (void *(*)(void))node_create;
            DLA_DEBUG_PRINTF("[so_patch] resolved cocos2d::Node::create @0x%08x (%s)\n",
                         (unsigned)(node_create & ~(uintptr_t)1u), (node_create & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::create not found; SpriteUtils fallback will return NULL\n");
        }

        uintptr_t sprite_create = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d6Sprite6createEv");
        if (sprite_create) {
            g_cocos_sprite_create_raw = (sprite_create_fn)sprite_create;
            DLA_DEBUG_PRINTF("[so_patch] resolved cocos2d::Sprite::create @0x%08x (%s)\n",
                         (unsigned)(sprite_create & ~(uintptr_t)1u), (sprite_create & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Sprite::create not found; sprite-frame fallback will return NULL\n");
        }

        uintptr_t node_convert_to_worldspace = (uintptr_t)so_symbol(&so_mod, "_ZNK7cocos2d4Node19convertToWorldSpaceERKNS_4Vec2E");
        if (node_convert_to_worldspace) {
            g_node_convert_to_worldspace_hook = hook_addr(node_convert_to_worldspace, (uintptr_t)&node_convert_to_worldspace_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Node::convertToWorldSpace @0x%08x (%s)\n",
                         (unsigned)(node_convert_to_worldspace & ~(uintptr_t)1u), (node_convert_to_worldspace & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Node::convertToWorldSpace not found; no hook applied\n");
        }

        uintptr_t listview_addeventlistener = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d2ui8ListView16addEventListenerERKNSt6__ndk18functionIFvPNS_3RefENS1_9EventTypeEEEE");
        if (listview_addeventlistener) {
            g_listview_addeventlistener_hook = hook_addr(listview_addeventlistener, (uintptr_t)&listview_addeventlistener_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::ui::ListView::addEventListener @0x%08x (%s)\n",
                         (unsigned)(listview_addeventlistener & ~(uintptr_t)1u), (listview_addeventlistener & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::ui::ListView::addEventListener not found; no hook applied\n");
        }

        uintptr_t actiontimeline_create = (uintptr_t)so_symbol(&so_mod, "_ZN10cocostudio8timeline14ActionTimeline6createEv");
        if (actiontimeline_create) {
            g_cocostudio_actiontimeline_create = (void *(*)(void))actiontimeline_create;
            DLA_DEBUG_PRINTF("[so_patch] resolved cocostudio::timeline::ActionTimeline::create @0x%08x (%s)\n",
                         (unsigned)(actiontimeline_create & ~(uintptr_t)1u), (actiontimeline_create & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] ActionTimeline::create not found; CSLoader timeline fallback will return NULL\n");
        }

        uintptr_t csloader_node_file = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader23nodeWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        if (csloader_node_file) {
            g_csloader_nodewithflatbuffersfile_hook = hook_addr(csloader_node_file, (uintptr_t)&csloader_nodewithflatbuffersfile_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::nodeWithFlatBuffersFile @0x%08x (%s)\n",
                         (unsigned)(csloader_node_file & ~(uintptr_t)1u), (csloader_node_file & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::nodeWithFlatBuffersFile not found; no hook applied\n");
        }

        uintptr_t csloader_node_file_cb = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader23nodeWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEERKNS1_8functionIFvPNS_3RefEEEE");
        if (csloader_node_file_cb) {
            g_csloader_nodewithflatbuffersfile_cb_hook = hook_addr(csloader_node_file_cb, (uintptr_t)&csloader_nodewithflatbuffersfile_cb_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::nodeWithFlatBuffersFile(cb) @0x%08x (%s)\n",
                         (unsigned)(csloader_node_file_cb & ~(uintptr_t)1u), (csloader_node_file_cb & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::nodeWithFlatBuffersFile(cb) not found; no hook applied\n");
        }

        uintptr_t csloader_create_file = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader29createNodeWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        if (csloader_create_file) {
            g_csloader_createnodewithflatbuffersfile_hook = hook_addr(csloader_create_file, (uintptr_t)&csloader_createnodewithflatbuffersfile_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::createNodeWithFlatBuffersFile @0x%08x (%s)\n",
                         (unsigned)(csloader_create_file & ~(uintptr_t)1u), (csloader_create_file & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::createNodeWithFlatBuffersFile not found; no hook applied\n");
        }

        uintptr_t csloader_create_file_cb = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader29createNodeWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEERKNS1_8functionIFvPNS_3RefEEEE");
        if (csloader_create_file_cb) {
            g_csloader_createnodewithflatbuffersfile_cb_hook = hook_addr(csloader_create_file_cb, (uintptr_t)&csloader_createnodewithflatbuffersfile_cb_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::createNodeWithFlatBuffersFile(cb) @0x%08x (%s)\n",
                         (unsigned)(csloader_create_file_cb & ~(uintptr_t)1u), (csloader_create_file_cb & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::createNodeWithFlatBuffersFile(cb) not found; no hook applied\n");
        }

        uintptr_t csloader_create_node = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader10createNodeERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        if (csloader_create_node) {
            g_csloader_createnode_hook = hook_addr(csloader_create_node, (uintptr_t)&csloader_createnode_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::createNode @0x%08x (%s)\n",
                         (unsigned)(csloader_create_node & ~(uintptr_t)1u), (csloader_create_node & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::createNode not found; no hook applied\n");
        }

        uintptr_t csloader_create_node_cb = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader10createNodeERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEERKNS1_8functionIFvPNS_3RefEEEE");
        if (csloader_create_node_cb) {
            g_csloader_createnode_cb_hook = hook_addr(csloader_create_node_cb, (uintptr_t)&csloader_createnode_cb_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::createNode(cb) @0x%08x (%s)\n",
                         (unsigned)(csloader_create_node_cb & ~(uintptr_t)1u), (csloader_create_node_cb & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::createNode(cb) not found; no hook applied\n");
        }

        uintptr_t csloader_create_timeline = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d8CSLoader14createTimelineERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        if (csloader_create_timeline) {
            g_csloader_createtimeline_hook = hook_addr(csloader_create_timeline, (uintptr_t)&csloader_createtimeline_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::CSLoader::createTimeline @0x%08x (%s)\n",
                         (unsigned)(csloader_create_timeline & ~(uintptr_t)1u), (csloader_create_timeline & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::CSLoader::createTimeline not found; no hook applied\n");
        }

        uintptr_t atl_load_flatbuffers = (uintptr_t)so_symbol(&so_mod, "_ZN10cocostudio8timeline19ActionTimelineCache38loadAnimationActionWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
        if (atl_load_flatbuffers) {
            g_actiontimelinecache_load_flatbuffers_hook = hook_addr(atl_load_flatbuffers, (uintptr_t)&actiontimelinecache_load_flatbuffers_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded ActionTimelineCache::loadAnimationActionWithFlatBuffersFile @0x%08x (%s)\n",
                         (unsigned)(atl_load_flatbuffers & ~(uintptr_t)1u), (atl_load_flatbuffers & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] ActionTimelineCache::loadAnimationActionWithFlatBuffersFile not found; no hook applied\n");
        }

        uintptr_t atl_load_file = (uintptr_t)so_symbol(&so_mod, "_ZN10cocostudio8timeline19ActionTimelineCache27loadAnimationActionWithFileERKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
        if (atl_load_file) {
            g_actiontimelinecache_load_file_hook = hook_addr(atl_load_file, (uintptr_t)&actiontimelinecache_load_file_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded ActionTimelineCache::loadAnimationActionWithFile @0x%08x (%s)\n",
                         (unsigned)(atl_load_file & ~(uintptr_t)1u), (atl_load_file & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] ActionTimelineCache::loadAnimationActionWithFile not found; no hook applied\n");
        }

        uintptr_t atl_create_flatbuffers = (uintptr_t)so_symbol(&so_mod, "_ZN10cocostudio8timeline19ActionTimelineCache31createActionWithFlatBuffersFileERKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
        if (atl_create_flatbuffers) {
            g_actiontimelinecache_create_flatbuffers_hook = hook_addr(atl_create_flatbuffers, (uintptr_t)&actiontimelinecache_create_flatbuffers_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded ActionTimelineCache::createActionWithFlatBuffersFile @0x%08x (%s)\n",
                         (unsigned)(atl_create_flatbuffers & ~(uintptr_t)1u), (atl_create_flatbuffers & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] ActionTimelineCache::createActionWithFlatBuffersFile not found; no hook applied\n");
        }

        uintptr_t atl_create_action = (uintptr_t)so_symbol(&so_mod, "_ZN10cocostudio8timeline19ActionTimelineCache12createActionERKNSt6__ndk112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
        if (atl_create_action) {
            g_actiontimelinecache_create_action_hook = hook_addr(atl_create_action, (uintptr_t)&actiontimelinecache_create_action_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded ActionTimelineCache::createAction @0x%08x (%s)\n",
                         (unsigned)(atl_create_action & ~(uintptr_t)1u), (atl_create_action & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] ActionTimelineCache::createAction not found; no hook applied\n");
        }

        uintptr_t spriteutils_load = (uintptr_t)so_symbol(&so_mod, "_ZN4game11SpriteUtils4LoadERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPPN7cocos2d6ActionE");
        if (spriteutils_load) {
            g_spriteutils_load_hook = hook_addr(spriteutils_load, (uintptr_t)&spriteutils_load_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::SpriteUtils::Load @0x%08x (%s)\n",
                         (unsigned)(spriteutils_load & ~(uintptr_t)1u), (spriteutils_load & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::SpriteUtils::Load not found; no hook applied\n");
        }

        uintptr_t partial_load = (uintptr_t)so_symbol(&so_mod, "_ZN4game9ShopScene11PartialLoadEi");
        if (partial_load) {
            g_shopscene_partialload_hook = hook_addr(partial_load, (uintptr_t)&shopscene_partialload_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ShopScene::PartialLoad @0x%08x (%s)\n",
                         (unsigned)(partial_load & ~(uintptr_t)1u), (partial_load & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ShopScene::PartialLoad not found; no hook applied\n");
        }

        uintptr_t update_character = (uintptr_t)so_symbol(&so_mod, "_ZN4game9ShopScene15UpdateCharacterEf");
        if (update_character) {
            g_shopscene_updatecharacter_hook = hook_addr(update_character, (uintptr_t)&shopscene_updatecharacter_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ShopScene::UpdateCharacter @0x%08x (%s)\n",
                         (unsigned)(update_character & ~(uintptr_t)1u), (update_character & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ShopScene::UpdateCharacter not found; no hook applied\n");
        }

        uintptr_t shopmenubase_createbuttons = (uintptr_t)so_symbol(&so_mod, "_ZN4game12ShopMenuBase13CreateButtonsEv");
        if (shopmenubase_createbuttons) {
            g_shopmenubase_createbuttons_hook = hook_addr(shopmenubase_createbuttons, (uintptr_t)&shopmenubase_createbuttons_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ShopMenuBase::CreateButtons @0x%08x (%s)\n",
                         (unsigned)(shopmenubase_createbuttons & ~(uintptr_t)1u), (shopmenubase_createbuttons & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ShopMenuBase::CreateButtons not found; no hook applied\n");
        }

        uintptr_t shopmenubase_createlabels = (uintptr_t)so_symbol(&so_mod, "_ZN4game12ShopMenuBase12CreateLabelsEf");
        if (shopmenubase_createlabels) {
            g_shopmenubase_createlabels_hook = hook_addr(shopmenubase_createlabels, (uintptr_t)&shopmenubase_createlabels_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::ShopMenuBase::CreateLabels @0x%08x (%s)\n",
                         (unsigned)(shopmenubase_createlabels & ~(uintptr_t)1u), (shopmenubase_createlabels & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::ShopMenuBase::CreateLabels not found; no hook applied\n");
        }

        uintptr_t inventorymenubase_setupcharacterinfo = (uintptr_t)so_symbol(&so_mod, "_ZN4game17InventoryMenuBase18SetupCharacterInfoEv");
        if (inventorymenubase_setupcharacterinfo) {
            g_inventorymenubase_setupcharacterinfo_hook = hook_addr(inventorymenubase_setupcharacterinfo, (uintptr_t)&inventorymenubase_setupcharacterinfo_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::InventoryMenuBase::SetupCharacterInfo @0x%08x (%s)\n",
                         (unsigned)(inventorymenubase_setupcharacterinfo & ~(uintptr_t)1u), (inventorymenubase_setupcharacterinfo & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::InventoryMenuBase::SetupCharacterInfo not found; no hook applied\n");
        }

        uintptr_t inventorymenubase_updatecharacterinfo = (uintptr_t)so_symbol(&so_mod, "_ZN4game17InventoryMenuBase19UpdateCharacterInfoEv");
        if (inventorymenubase_updatecharacterinfo) {
            g_inventorymenubase_updatecharacterinfo_hook = hook_addr(inventorymenubase_updatecharacterinfo, (uintptr_t)&inventorymenubase_updatecharacterinfo_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::InventoryMenuBase::UpdateCharacterInfo @0x%08x (%s)\n",
                         (unsigned)(inventorymenubase_updatecharacterinfo & ~(uintptr_t)1u), (inventorymenubase_updatecharacterinfo & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::InventoryMenuBase::UpdateCharacterInfo not found; no hook applied\n");
        }

        uintptr_t inventorymenubase_showbottomline = (uintptr_t)so_symbol(&so_mod, "_ZN4game17InventoryMenuBase14ShowBottomLineEb");
        if (inventorymenubase_showbottomline) {
            g_inventorymenubase_showbottomline_hook = hook_addr(inventorymenubase_showbottomline, (uintptr_t)&inventorymenubase_showbottomline_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::InventoryMenuBase::ShowBottomLine @0x%08x (%s)\n",
                         (unsigned)(inventorymenubase_showbottomline & ~(uintptr_t)1u), (inventorymenubase_showbottomline & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::InventoryMenuBase::ShowBottomLine not found; no hook applied\n");
        }

        uintptr_t inventorymenubase_createlist = (uintptr_t)so_symbol(&so_mod, "_ZN4game17InventoryMenuBase10CreateListEv");
        if (inventorymenubase_createlist) {
            g_inventorymenubase_createlist_hook = hook_addr(inventorymenubase_createlist, (uintptr_t)&inventorymenubase_createlist_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::InventoryMenuBase::CreateList @0x%08x (%s)\n",
                         (unsigned)(inventorymenubase_createlist & ~(uintptr_t)1u), (inventorymenubase_createlist & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::InventoryMenuBase::CreateList not found; no hook applied\n");
        }

        uintptr_t inventorymenu_setupsubcategorybuttons = (uintptr_t)so_symbol(&so_mod, "_ZN4game13InventoryMenu23SetupSubcategoryButtonsEv");
        if (inventorymenu_setupsubcategorybuttons) {
            g_inventorymenu_setupsubcategorybuttons_hook = hook_addr(inventorymenu_setupsubcategorybuttons, (uintptr_t)&inventorymenu_setupsubcategorybuttons_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::InventoryMenu::SetupSubcategoryButtons @0x%08x (%s)\n",
                         (unsigned)(inventorymenu_setupsubcategorybuttons & ~(uintptr_t)1u), (inventorymenu_setupsubcategorybuttons & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::InventoryMenu::SetupSubcategoryButtons not found; no hook applied\n");
        }

        uintptr_t upgrademenu_setupsubcategorybuttons = (uintptr_t)so_symbol(&so_mod, "_ZN4game11UpgradeMenu23SetupSubcategoryButtonsEv");
        if (upgrademenu_setupsubcategorybuttons) {
            g_upgrademenu_setupsubcategorybuttons_hook = hook_addr(upgrademenu_setupsubcategorybuttons, (uintptr_t)&upgrademenu_setupsubcategorybuttons_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::UpgradeMenu::SetupSubcategoryButtons @0x%08x (%s)\n",
                         (unsigned)(upgrademenu_setupsubcategorybuttons & ~(uintptr_t)1u), (upgrademenu_setupsubcategorybuttons & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::UpgradeMenu::SetupSubcategoryButtons not found; no hook applied\n");
        }

        uintptr_t labelsprite_updatevalue = (uintptr_t)so_symbol(&so_mod, "_ZN4game11LabelSprite11UpdateValueEv");
        if (labelsprite_updatevalue) {
            g_labelsprite_updatevalue_hook = hook_addr(labelsprite_updatevalue, (uintptr_t)&labelsprite_updatevalue_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::LabelSprite::UpdateValue @0x%08x (%s)\n",
                         (unsigned)(labelsprite_updatevalue & ~(uintptr_t)1u), (labelsprite_updatevalue & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::LabelSprite::UpdateValue not found; no hook applied\n");
        }

        uintptr_t c_uxelement_setlabel_plain = (uintptr_t)so_symbol(&so_mod, "_ZN4game11C_UXElement14SetLabelStringERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        if (c_uxelement_setlabel_plain) {
            g_c_uxelement_setlabel_plain_hook = hook_addr(c_uxelement_setlabel_plain, (uintptr_t)&c_uxelement_setlabel_plain_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::C_UXElement::SetLabelString(text) @0x%08x (%s)\n",
                         (unsigned)(c_uxelement_setlabel_plain & ~(uintptr_t)1u), (c_uxelement_setlabel_plain & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::C_UXElement::SetLabelString(text) not found; no hook applied\n");
        }

        uintptr_t c_uxelement_setlabel_three = (uintptr_t)so_symbol(&so_mod, "_ZN4game11C_UXElement14SetLabelStringERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_S9_");
        if (c_uxelement_setlabel_three) {
            g_c_uxelement_setlabel_three_hook = hook_addr(c_uxelement_setlabel_three, (uintptr_t)&c_uxelement_setlabel_three_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::C_UXElement::SetLabelString(text,prefix,suffix) @0x%08x (%s)\n",
                         (unsigned)(c_uxelement_setlabel_three & ~(uintptr_t)1u), (c_uxelement_setlabel_three & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::C_UXElement::SetLabelString(text,prefix,suffix) not found; no hook applied\n");
        }

        uintptr_t c_uxelement_setlabel_text = (uintptr_t)so_symbol(&so_mod, "_ZN4game11C_UXElement14SetLabelStringENS_8TextTypeEi");
        if (c_uxelement_setlabel_text) {
            g_c_uxelement_setlabel_text_hook = hook_addr(c_uxelement_setlabel_text, (uintptr_t)&c_uxelement_setlabel_text_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::C_UXElement::SetLabelString(TextType,int) @0x%08x (%s)\n",
                         (unsigned)(c_uxelement_setlabel_text & ~(uintptr_t)1u), (c_uxelement_setlabel_text & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::C_UXElement::SetLabelString(TextType,int) not found; no hook applied\n");
        }

        uintptr_t c_uxelement_setlabel_typed = (uintptr_t)so_symbol(&so_mod, "_ZN4game11C_UXElement14SetLabelStringERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEENS_8TextTypeEiS9_");
        if (c_uxelement_setlabel_typed) {
            g_c_uxelement_setlabel_typed_hook = hook_addr(c_uxelement_setlabel_typed, (uintptr_t)&c_uxelement_setlabel_typed_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::C_UXElement::SetLabelString(prefix,TextType,int,suffix) @0x%08x (%s)\n",
                         (unsigned)(c_uxelement_setlabel_typed & ~(uintptr_t)1u), (c_uxelement_setlabel_typed & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::C_UXElement::SetLabelString(prefix,TextType,int,suffix) not found; no hook applied\n");
        }

#if SO_PATCH_SPRITE_CREATE_FRAME_FALLBACK
        uintptr_t sprite_create_with_frame = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d6Sprite21createWithSpriteFrameEPNS_11SpriteFrameE");
        if (sprite_create_with_frame) {
            g_sprite_create_with_sprite_frame_hook = hook_addr(sprite_create_with_frame, (uintptr_t)&sprite_create_with_sprite_frame_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Sprite::createWithSpriteFrame @0x%08x (%s)\n",
                         (unsigned)(sprite_create_with_frame & ~(uintptr_t)1u), (sprite_create_with_frame & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Sprite::createWithSpriteFrame not found; no hook applied\n");
        }
#endif

        uintptr_t sprite_set_frame = (uintptr_t)so_symbol(&so_mod, "_ZN7cocos2d6Sprite14setSpriteFrameEPNS_11SpriteFrameE");
        if (sprite_set_frame) {
            g_sprite_set_sprite_frame_hook = hook_addr(sprite_set_frame, (uintptr_t)&sprite_set_sprite_frame_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded cocos2d::Sprite::setSpriteFrame(SpriteFrame*) @0x%08x (%s)\n",
                         (unsigned)(sprite_set_frame & ~(uintptr_t)1u), (sprite_set_frame & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] cocos2d::Sprite::setSpriteFrame(SpriteFrame*) not found; no hook applied\n");
        }

        uintptr_t play_animation = (uintptr_t)so_symbol(&so_mod, "_ZN4game12FTCCharacter13PlayAnimationERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEifN7cocos2d4Vec2Ei");
        if (play_animation) {
            g_ftccharacter_playanimation_hook = hook_addr(play_animation, (uintptr_t)&ftccharacter_playanimation_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCCharacter::PlayAnimation @0x%08x (%s)\n",
                         (unsigned)(play_animation & ~(uintptr_t)1u), (play_animation & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCCharacter::PlayAnimation not found; no hook applied\n");
        }

        uintptr_t update = (uintptr_t)so_symbol(&so_mod, "_ZN4game12FTCCharacter6UpdateEf");
        if (update) {
            g_ftccharacter_update_hook = hook_addr(update, (uintptr_t)&ftccharacter_update_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCCharacter::Update @0x%08x (%s)\n",
                         (unsigned)(update & ~(uintptr_t)1u), (update & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCCharacter::Update not found; no hook applied\n");
        }

        uintptr_t update_overlays = (uintptr_t)so_symbol(&so_mod, "_ZN4game12FTCCharacter14UpdateOverlaysEv");
        if (update_overlays) {
            g_ftccharacter_updateoverlays_hook = hook_addr(update_overlays, (uintptr_t)&ftccharacter_updateoverlays_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCCharacter::UpdateOverlays @0x%08x (%s)\n",
                         (unsigned)(update_overlays & ~(uintptr_t)1u), (update_overlays & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCCharacter::UpdateOverlays not found; no hook applied\n");
        }

        uintptr_t play_frame = (uintptr_t)so_symbol(&so_mod, "_ZN4game12FTCCharacter9PlayFrameEv");
        if (play_frame) {
            g_ftccharacter_playframe_hook = hook_addr(play_frame, (uintptr_t)&ftccharacter_playframe_guard);
            DLA_DEBUG_PRINTF("[so_patch] guarded game::FTCCharacter::PlayFrame @0x%08x (%s)\n",
                         (unsigned)(play_frame & ~(uintptr_t)1u), (play_frame & 1u) ? "Thumb" : "ARM");
        } else {
            DLA_DEBUG_PRINTF("[so_patch] game::FTCCharacter::PlayFrame not found; no hook applied\n");
        }
    }
#else
    DLA_DEBUG_PRINTF("[so_patch] gameplay UI/animation fallback guards disabled; using original shop, list, sprite, and FTCCharacter paths\n");
#endif

    DLA_DEBUG_PRINTF("so_patch: end\n");
}
