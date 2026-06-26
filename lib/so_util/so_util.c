/* so_util.c -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>

#include <psp2/kernel/clib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/dialog.h"
#include "debug_log.h"
#include "so_util.h"

// -----------------------------------------------------------------------------
// Import resolution diagnostics
// -----------------------------------------------------------------------------
// Set to 1 to dump every resolved symbol (very noisy; only enable when needed).
#ifndef SO_DEBUG_IMPORTS
#define SO_DEBUG_IMPORTS 0
#endif

#ifndef SO_DEBUG_INIT
#define SO_DEBUG_INIT DLA_DEBUG_LOGS
#endif

#ifndef SO_FIXUP_ANDROID_PTRS
#define SO_FIXUP_ANDROID_PTRS 1
#endif

#ifndef SO_FIXUP_ANDROID_PTRS_TEXT
#define SO_FIXUP_ANDROID_PTRS_TEXT 0
#endif

#ifndef SO_SKIP_KNOWN_BAD_INIT_CTORS
#define SO_SKIP_KNOWN_BAD_INIT_CTORS 1
#endif

static inline int so_ptr_looks_like_android_addr(uintptr_t p) {
    // Heuristic: Android userspace for 32-bit ARM frequently uses low virtual
    // ranges (e.g. 0x1xxxxxxx-0x7xxxxxxx). If such values survive into function
    // pointers on Vita, they will usually crash with Prefetch abort.
    return (p >= 0x10000000u) && (p < 0x80000000u);
}

static inline uintptr_t so_abs_to_vaddr(so_module *mod, uintptr_t abs_no_thumb) {
    return (abs_no_thumb - (uintptr_t)mod->text_base) + (uintptr_t)mod->text_start;
}

static inline int so_should_skip_init_ctor(so_module *mod, int idx, uintptr_t f_no_thumb) {
#if SO_SKIP_KNOWN_BAD_INIT_CTORS
    // Dark Lands libcocos2dcpp.so:
    // init_array[490] (vaddr 0x00f9507d, Thumb) crashes with Undefined instruction
    // inside a locale/iostream initialization path on Vita. Skipping this one
    // constructor allows boot progression while preserving the rest of init.
    const uintptr_t vaddr = so_abs_to_vaddr(mod, f_no_thumb);
    if (idx == 490 && (vaddr == 0x00f9507cu || vaddr == 0x00f9507du)) {
        DLA_DEBUG_PRINTF("[so_initialize][WARN] skipping known-bad init_array[%d]=%p (vaddr=0x%08x)\n",
                     idx, (void *)f_no_thumb, (unsigned)vaddr);
        return 1;
    }
#else
    (void)mod;
    (void)idx;
    (void)f_no_thumb;
#endif
    return 0;
}

// Returns non-zero if p (optionally Thumb-tagged) points inside this module's
// mapped runtime address ranges (text or any data segment).
static inline int so_ptr_in_runtime_range(so_module *mod, uintptr_t p) {
    uintptr_t v = p & ~(uintptr_t)1;
    uintptr_t text_lo = (uintptr_t)mod->text_base;
    uintptr_t text_hi = text_lo + (uintptr_t)mod->text_size;
    if (v >= text_lo && v < text_hi) return 1;
    for (int i = 0; i < mod->n_data; i++) {
        uintptr_t data_lo = (uintptr_t)mod->data_base[i];
        uintptr_t data_hi = data_lo + ((uintptr_t)mod->data_end[i] - (uintptr_t)mod->data_start[i]);
        if (v >= data_lo && v < data_hi) return 1;
    }
    return 0;
}

// Translate a pointer that still appears to be an Android-era absolute address
// (typically based at 0x31000000) into the Vita runtime address.
//
// Important: some builds store *absolute runtime* Android pointers (0x31xxxxxx,
// 0x32xxxxxx, ...) instead of plain ELF VADDRs. In that case, the value will
// never fall inside mod->text_start..text_start+text_size (often 0..size).
// We therefore support both encodings:
//   1) Raw VADDR:            p0 in [seg_start_vaddr, seg_end_vaddr)
//   2) Android absolute:     p0 in [ANDROID_BASE+seg_start_vaddr, ANDROID_BASE+seg_end_vaddr)
//
// Pointers outside these ranges are left unchanged.
static inline uintptr_t so_translate_android_ptr_if_needed(so_module *mod, uintptr_t p) {
    const uintptr_t thumb = p & 1u;
    const uintptr_t p0 = p & ~1u;

    if (!so_ptr_looks_like_android_addr(p0))
        return p;

    // If it already points into our mapped runtime ranges, leave it.
    if (so_ptr_in_runtime_range(mod, p0))
        return p;

    // Android absolute base varies across builds. Dark Lands pointers were
    // observed with both 0x30xxxxxx and 0x31xxxxxx prefixes.
    static const uintptr_t ANDROID_BASES[] = { 0x30000000u, 0x31000000u };

    // --- Android absolute addressing ---
    // Text segment
    for (unsigned b = 0; b < (unsigned)(sizeof(ANDROID_BASES) / sizeof(ANDROID_BASES[0])); b++) {
        const uintptr_t base = ANDROID_BASES[b];
        {
            const uintptr_t abs_lo = base + (uintptr_t)mod->text_start;
            const uintptr_t abs_hi = abs_lo + (uintptr_t)mod->text_size;
            if (p0 >= abs_lo && p0 < abs_hi) {
                const uintptr_t translated = (p0 - abs_lo) + (uintptr_t)mod->text_base;
                return translated | thumb;
            }
        }

        // Data segments
        for (int i = 0; i < mod->n_data; i++) {
            const uintptr_t abs_lo = base + (uintptr_t)mod->data_start[i];
            const uintptr_t abs_hi = base + (uintptr_t)mod->data_end[i];
            if (p0 >= abs_lo && p0 < abs_hi) {
                const uintptr_t translated = (p0 - abs_lo) + (uintptr_t)mod->data_base[i];
                return translated | thumb;
            }
        }
    }

    // --- Raw VADDR fallback ---
    // Text segment
    if (p0 >= mod->text_start && p0 < mod->text_start + mod->text_size) {
        const uintptr_t translated = (p0 - mod->text_start) + mod->text_base;
        return translated | thumb;
    }

    // Data segments
    for (int i = 0; i < mod->n_data; i++) {
        if (p0 >= mod->data_start[i] && p0 < mod->data_end[i]) {
            const uintptr_t translated = (p0 - mod->data_start[i]) + mod->data_base[i];
            return translated | thumb;
        }
    }

    return p;
}



// -----------------------------------------------------------------------------
// Heuristic fixup: translate leftover Android absolute pointers (0x31xxxxxx)
// -----------------------------------------------------------------------------
//
// Some Android builds embed absolute addresses (based at 0x31000000) in literal
// pools, vtables, or hand-written tables without emitting relocation entries.
// When porting with so_loader, those pointers stay as 0x31xxxxxx and will crash
// the first time they are used (typically via an indirect call).
//
// This pass scans the mapped segments and translates any value that:
//   (a) looks like an Android absolute address, and
//   (b) falls within this module's virtual address ranges.
//
// It is intentionally conservative (range-checked) to minimize false positives.

#define SO_ANDROID_PTR_FIXUP_LOG_LIMIT 32

static int so_android_ptr_fixup_log_count = 0;

static inline void so_write_u32_unrestricted(uintptr_t dst, uint32_t v) {
    // Use kubridge memcpy even for RW pages to keep behavior consistent.
    kuKernelCpuUnrestrictedMemcpy((void *)dst, &v, sizeof(v));
}

static size_t so_fixup_android_ptrs_in_range(so_module *mod, uintptr_t base, size_t size, const char *tag) {
    if (base == 0 || size < 4) return 0;

    size_t patched = 0;
    uintptr_t end = base + size;

    // Align to 4 bytes.
    uintptr_t p = (base + 3) & ~((uintptr_t)3);

    for (; p + 4 <= end; p += 4) {
        uint32_t v;
        // Reading from user-mapped memory is fine.
        v = *(uint32_t *)p;

        if (!so_ptr_looks_like_android_addr((uintptr_t)v))
            continue;

        uintptr_t nv = so_translate_android_ptr_if_needed(mod, (uintptr_t)v);
        if (nv == (uintptr_t)v)
            continue;

        so_write_u32_unrestricted(p, (uint32_t)nv);
        patched++;

        if (DLA_DEBUG_LOGS && so_android_ptr_fixup_log_count < SO_ANDROID_PTR_FIXUP_LOG_LIMIT) {
            DLA_DEBUG_PRINTF("[so_fixup] %s @0x%08lx: 0x%08x -> 0x%08lx\n", tag, (unsigned long)p, (unsigned)v, (unsigned long)nv);
            so_android_ptr_fixup_log_count++;
        }
    }

    if (patched > 0) {
        DLA_DEBUG_PRINTF("[so_fixup] patched %u Android pointers in %s\n", (unsigned)patched, tag);
    }

    return patched;
}

static void so_fixup_android_ptrs(so_module *mod) {
    size_t total = 0;

    // Scanning executable text can corrupt valid Thumb/ARM instruction words
    // that coincidentally look like Android-era addresses. Keep this off by
    // default and only patch data segments.
#if SO_FIXUP_ANDROID_PTRS_TEXT
    DLA_DEBUG_PRINTF("[so_fixup] text scan ENABLED\n");
    total += so_fixup_android_ptrs_in_range(mod, mod->text_base, mod->text_size, "text");
#else
    DLA_DEBUG_PRINTF("[so_fixup] text scan DISABLED (data-only fixup)\n");
#endif

    for (int i = 0; i < mod->n_data; i++) {
        char tag[32];
        sceClibSnprintf(tag, sizeof(tag), "data[%d]", i);
        total += so_fixup_android_ptrs_in_range(mod, mod->data_base[i], mod->data_size[i], tag);
    }

    if (total > 0) {
        // Ensure CPU sees patched code / literals.
        so_flush_caches(mod);
    }
}
static inline void *so_map_vaddr(so_module *mod, uintptr_t vaddr);

typedef struct {
    int total_relocs;
    int resolved_from_deps;
    int resolved_from_default;
    int unresolved_total;
    int unresolved_plt;
    int unresolved_data;
} so_resolve_stats;

static so_resolve_stats g_so_resolve_stats;

#ifndef SO_IMPORT_DIAG_MAX
#define SO_IMPORT_DIAG_MAX 64
#endif

// Set to 1 to stop immediately after resolution when import diagnostics detect
// unresolved or broken function pointers. Useful for debugging sessions.
#ifndef SO_IMPORT_DIAG_FAIL_FAST
#define SO_IMPORT_DIAG_FAIL_FAST 1
#endif

typedef struct {
    char symbol[96];
    uint32_t r_off;
    uint8_t type;
    uint8_t kind; // 0=unresolved, 1=broken_ptr
    uintptr_t value;
} so_import_diag_entry;

static so_import_diag_entry g_import_diag[SO_IMPORT_DIAG_MAX];
static int g_import_diag_count = 0;

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX                 (0x0C20D050)
#endif

typedef struct b_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm24: 24;
            unsigned int l: 1; // Branch with Link flag
            unsigned int enc: 3; // 0b101
            unsigned int cond: 4; // 0b1110
        } bits;
        uint32_t raw;
    };
} b_enc;

typedef struct ldst_enc {
    union {
        struct __attribute__((__packed__)) {
            int imm12: 12;
            unsigned int rt: 4; // Source/Destination register
            unsigned int rn: 4; // Base register
            unsigned int bit20_1: 1; // 0: store to memory, 1: load from memory
            unsigned int w: 1; // 0: no write-back, 1: write address into base
            unsigned int b: 1; // 0: word, 1: byte
            unsigned int u: 1; // 0: subtract offset from base, 1: add to base
            unsigned int p: 1; // 0: post indexing, 1: pre indexing
            unsigned int enc: 3;
            unsigned int cond: 4;
        } bits;
        uint32_t raw;
    };
} ldst_enc;

#define B_RANGE ((1 << 24) - 1)
#define B_OFFSET(x) (x + 8) // branch jumps into addr - 8, so range is biased forward
#define B(PC, DEST) ((b_enc){.bits = {.cond = 0b1110, .enc = 0b101, .l = 0, .imm24 = (((intptr_t)DEST-(intptr_t)PC) / 4) - 2}})
#define LDR_OFFS(RT, RN, IMM) ((ldst_enc){.bits = {.cond = 0b1110, .enc = 0b010, .p = 1, .u = (IMM >= 0), .b = 0, .w = 0, .bit20_1 = 1, .rn = RN, .rt = RT, .imm12 = (IMM >= 0) ? IMM : -IMM}})

#define PATCH_SZ 0x10000 //64 KB-ish arenas
static so_module *head = NULL, *tail = NULL;

static inline int so_ptr_in_any_loaded_text(uintptr_t p_no_thumb) {
    for (so_module *m = head; m; m = m->next) {
        uintptr_t lo = (uintptr_t)m->text_base;
        uintptr_t hi = lo + (uintptr_t)m->text_size;
        if (p_no_thumb >= lo && p_no_thumb < hi)
            return 1;
    }
    return 0;
}

static inline int so_ptr_is_plausible_code(uintptr_t p) {
    const uintptr_t v = p & ~(uintptr_t)1u;

    if (so_ptr_in_any_loaded_text(v))
        return 1;

    // Main eboot / user RX mappings and kernel module mappings.
    if ((v >= 0x81000000u && v < 0x82000000u) ||
        (v >= 0x98000000u && v < 0x9A000000u) ||
        (v >= 0xE0000000u && v < 0xF0000000u))
        return 1;

    return 0;
}

static void so_import_diag_reset(void) {
    g_import_diag_count = 0;
}

static void so_import_diag_add(uint8_t kind, const char *symbol, uint32_t r_off, uint8_t type, uintptr_t value) {
    if (g_import_diag_count >= SO_IMPORT_DIAG_MAX)
        return;
    so_import_diag_entry *e = &g_import_diag[g_import_diag_count++];
    sceClibSnprintf(e->symbol, sizeof(e->symbol), "%s", symbol ? symbol : "?");
    e->r_off = r_off;
    e->type = type;
    e->kind = kind;
    e->value = value;
}

so_hook hook_thumb(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    DLA_DEBUG_PRINTF("THUMB HOOK\n");
    if (addr == 0)
        return h;
    h.thumb_addr = addr;
    addr &= ~1;
    if (addr & 2) {
        uint16_t nop = 0xbf00;
        kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, sizeof(nop));
        addr += 2;
        DLA_DEBUG_PRINTF("THUMB UNALIGNED\n");
    }

    h.addr = addr;
    h.patch_instr[0] = 0xf000f8df; // LDR PC, [PC]
    h.patch_instr[1] = dst;
    kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_arm(uintptr_t addr, uintptr_t dst) {
    so_hook h;
    DLA_DEBUG_PRINTF("ARM HOOK\n");
    if (addr == 0)
        return h;
    uint32_t hook[2];
    h.thumb_addr = 0;
    h.addr = addr;
    h.patch_instr[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
    h.patch_instr[1] = dst;
    kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
    kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

    return h;
}

so_hook hook_addr(uintptr_t addr, uintptr_t dst) {
    if (addr == 0) {
        so_hook h;
        return h;
    }

    if (addr & 1)
        return hook_thumb(addr, dst);
    else
        return hook_arm(addr, dst);
}

void so_flush_caches(so_module *mod) {
    kuKernelFlushCaches((void *)mod->text_base, mod->text_size);
}

int _so_load(so_module *mod, SceUID so_blockid, void *so_data, uintptr_t load_addr) {
    int res = 0;
    uintptr_t data_addr = 0;

    if (memcmp(so_data, ELFMAG, SELFMAG) != 0) {
        res = -1;
        goto err_free_so;
    }

    mod->ehdr = (Elf32_Ehdr *)so_data;
    mod->phdr = (Elf32_Phdr *)((uintptr_t)so_data + mod->ehdr->e_phoff);
    mod->shdr = (Elf32_Shdr *)((uintptr_t)so_data + mod->ehdr->e_shoff);

    mod->shstr = (char *)((uintptr_t)so_data + mod->shdr[mod->ehdr->e_shstrndx].sh_offset);

    for (int i = 0; i < mod->ehdr->e_phnum; i++) {
        if (mod->phdr[i].p_type == PT_LOAD) {
            void *prog_data;
            size_t prog_size;

            if ((mod->phdr[i].p_flags & PF_X) == PF_X) {
                // Preserve original (ELF) VADDR of the executable PT_LOAD.
                // This is required to correctly map relocations and other
                // offsets that are expressed in the ELF address space.
                mod->text_start = (uintptr_t)mod->phdr[i].p_vaddr;

                // Allocate arena for code patches, trampolines, etc
                // Sits exactly under the desired allocation space
                mod->patch_size = ALIGN_MEM(PATCH_SZ, mod->phdr[i].p_align);
                SceKernelAllocMemBlockKernelOpt opt;
                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)load_addr - mod->patch_size;
                res = mod->patch_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, mod->patch_size, &opt);
                if (res < 0)
                    goto err_free_so;

                sceKernelGetMemBlockBase(mod->patch_blockid, (void **) &mod->patch_base);
                mod->patch_head = mod->patch_base;

                prog_size = ALIGN_MEM(mod->phdr[i].p_memsz, mod->phdr[i].p_align);
                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)load_addr;
                res = mod->text_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, prog_size, &opt);
                if (res < 0)
                    goto err_free_so;

                sceKernelGetMemBlockBase(mod->text_blockid, &prog_data);

                mod->phdr[i].p_vaddr += (Elf32_Addr)prog_data;

                mod->text_base = mod->phdr[i].p_vaddr;
                mod->text_size = mod->phdr[i].p_memsz;

                // Use the .text segment padding as a code cave
                // Word-align it to make it simpler for instruction arena allocation
                mod->cave_size = ALIGN_MEM(prog_size - mod->phdr[i].p_memsz, 0x4);
                mod->cave_base = mod->cave_head = (uintptr_t) prog_data + mod->phdr[i].p_memsz;
                mod->cave_base = ALIGN_MEM(mod->cave_base, 0x4);
                mod->cave_head = mod->cave_base;
                DLA_DEBUG_PRINTF("code cave: %d bytes (@0x%08X).\n", mod->cave_size, mod->cave_base);

                data_addr = (uintptr_t)prog_data + prog_size;
            } else {
                if (data_addr == 0)
                    goto err_free_so;

                if (mod->n_data >= MAX_DATA_SEG)
                    goto err_free_data;

                // Preserve original (ELF) VADDR range for this data PT_LOAD
                // before we transform p_vaddr into a runtime address.
                uintptr_t seg_vaddr = (uintptr_t)mod->phdr[i].p_vaddr;
                size_t seg_memsz = (size_t)mod->phdr[i].p_memsz;

                mod->data_start[mod->n_data] = seg_vaddr;
                mod->data_end[mod->n_data] = seg_vaddr + seg_memsz;

                prog_size = ALIGN_MEM(mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - (data_addr - mod->text_base), mod->phdr[i].p_align);

                SceKernelAllocMemBlockKernelOpt opt;
                memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
                opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
                opt.attr = 0x1;
                opt.field_C = (SceUInt32)data_addr;
                res = mod->data_blockid[mod->n_data] = kuKernelAllocMemBlock("rw_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, prog_size, &opt);
                if (res < 0)
                    goto err_free_text;

                sceKernelGetMemBlockBase(mod->data_blockid[mod->n_data], &prog_data);
                data_addr = (uintptr_t)prog_data + prog_size;

                mod->phdr[i].p_vaddr += (Elf32_Addr)mod->text_base;

                mod->data_base[mod->n_data] = mod->phdr[i].p_vaddr;
                mod->data_size[mod->n_data] = mod->phdr[i].p_memsz;
                mod->n_data++;
            }

            char *zero = malloc(prog_size - mod->phdr[i].p_filesz);
            memset(zero, 0, prog_size - mod->phdr[i].p_filesz);
            kuKernelCpuUnrestrictedMemcpy(prog_data + mod->phdr[i].p_filesz, zero, prog_size - mod->phdr[i].p_filesz);
            free(zero);

            kuKernelCpuUnrestrictedMemcpy((void *)mod->phdr[i].p_vaddr, (void *)((uintptr_t)so_data + mod->phdr[i].p_offset), mod->phdr[i].p_filesz);
        }
    }

    for (int i = 0; i < mod->ehdr->e_shnum; i++) {
        char *sh_name = mod->shstr + mod->shdr[i].sh_name;
        uintptr_t sh_addr = mod->text_base + mod->shdr[i].sh_addr;
        size_t sh_size = mod->shdr[i].sh_size;
        if (strcmp(sh_name, ".dynamic") == 0) {
            mod->dynamic = (Elf32_Dyn *)sh_addr;
            mod->num_dynamic = sh_size / sizeof(Elf32_Dyn);
        } else if (strcmp(sh_name, ".dynstr") == 0) {
            mod->dynstr = (char *)sh_addr;
        } else if (strcmp(sh_name, ".dynsym") == 0) {
            mod->dynsym = (Elf32_Sym *)sh_addr;
            mod->num_dynsym = sh_size / sizeof(Elf32_Sym);
        } else if (strcmp(sh_name, ".rel.dyn") == 0) {
            mod->reldyn = (Elf32_Rel *)sh_addr;
            mod->num_reldyn = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".rel.plt") == 0) {
            mod->relplt = (Elf32_Rel *)sh_addr;
            mod->num_relplt = sh_size / sizeof(Elf32_Rel);
        } else if (strcmp(sh_name, ".init_array") == 0) {
            mod->init_array = (void *)sh_addr;
            mod->num_init_array = sh_size / sizeof(void *);
        } else if (strcmp(sh_name, ".hash") == 0) {
            mod->hash = (void *)sh_addr;
        }
    }

    if (mod->dynamic == NULL ||
        mod->dynstr == NULL ||
        mod->dynsym == NULL ||
        mod->reldyn == NULL ||
        mod->relplt == NULL) {
        res = -2;
        goto err_free_data;
    }

    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_SONAME:
                mod->soname = mod->dynstr + mod->dynamic[i].d_un.d_ptr;
                break;
            default:
                break;
        }
    }

    sceKernelFreeMemBlock(so_blockid);

    if (!head && !tail) {
        head = mod;
        tail = mod;
    } else {
        tail->next = mod;
        tail = mod;
    }

	if (SO_DEBUG_INIT) {
		DLA_DEBUG_PRINTF("[so_load] text_base=%p text_size=0x%08x text_start=0x%08x n_data=%d\n",
		             (void *)mod->text_base, (unsigned)mod->text_size, (unsigned)mod->text_start, mod->n_data);
		for (int i = 0; i < mod->n_data; i++) {
			DLA_DEBUG_PRINTF("[so_load] data[%d] base=%p vaddr=0x%08x..0x%08x\n",
			             i, (void *)mod->data_base[i], (unsigned)mod->data_start[i], (unsigned)mod->data_end[i]);
		}
	}

    return 0;

    err_free_data:
    for (int i = 0; i < mod->n_data; i++)
        sceKernelFreeMemBlock(mod->data_blockid[i]);
    err_free_text:
    sceKernelFreeMemBlock(mod->text_blockid);
    err_free_so:
    sceKernelFreeMemBlock(so_blockid);

    return res;
}

int so_mem_load(so_module *mod, void *buffer, size_t so_size, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    if (so_blockid < 0)
        return so_blockid;

    sceKernelGetMemBlockBase(so_blockid, &so_data);
    sceClibMemcpy(so_data, buffer, so_size);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr) {
    SceUID so_blockid;
    void *so_data;

    memset(mod, 0, sizeof(so_module));

    SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0)
        return fd;

    size_t so_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
    if (so_blockid < 0)
        return so_blockid;

    sceKernelGetMemBlockBase(so_blockid, &so_data);

    sceIoRead(fd, so_data, so_size);
    sceIoClose(fd);

    return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_relocate(so_module *mod) {
    // Load bias converts ELF VADDRs (p_vaddr space) into runtime addresses.
    // This matters for non-zero p_vaddr (prelinked/non-PIE) objects.
    const uintptr_t load_bias = (uintptr_t)mod->text_base - (uintptr_t)mod->text_start;

    // Minimal histogram to validate what we actually see in the wild.
    unsigned type_rel = 0, type_abs32 = 0, type_got = 0;
    unsigned rel_addend_android = 0, rel_addend_already_rt = 0;

    uintptr_t val;
    int suspicious_prints = 0;

    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];

        uintptr_t *ptr = (uintptr_t *)so_map_vaddr(mod, rel->r_offset);
        if (!ptr) {
            DLA_DEBUG_PRINTF("[so_relocate][WARN] r_offset=0x%08x not mapped (text_size=0x%08x, n_data=%d). Falling back to text_base mapping.\n",
                         (unsigned)rel->r_offset, (unsigned)mod->text_size, mod->n_data);
            ptr = (uintptr_t *)(mod->text_base + rel->r_offset);
        }

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32: {
                type_abs32++;
                // ABS32 uses addend stored at *ptr (ELF32 REL).
                if (sym->st_shndx != SHN_UNDEF) {
                    void *sym_rt = so_map_vaddr(mod, sym->st_value);
                    if (sym_rt) {
                        val = *ptr + (uintptr_t)sym_rt;
                        kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                    }
                }
                break;
            }

            case R_ARM_RELATIVE: {
                type_rel++;

                // RELATIVE should be applied exactly once. If the addend already
                // looks like a runtime pointer inside this module, do nothing.
                if (so_ptr_in_runtime_range(mod, *ptr)) {
                    rel_addend_already_rt++;
                    val = *ptr;
                    if (DLA_DEBUG_LOGS && suspicious_prints < 32) {
                        DLA_DEBUG_PRINTF("[so_relocate][WARN] RELATIVE addend already runtime: *%p=%p (r_off=0x%08x)\n",
                                     (void *)ptr, (void *)(uintptr_t)*ptr, (unsigned)rel->r_offset);
                        suspicious_prints++;
                    }
                } else if (so_ptr_looks_like_android_addr(*ptr)) {
                    // Some Android-built objects appear to keep absolute Android-era
                    // addresses as addends. Translate if they are in-module vaddr.
                    rel_addend_android++;
                    val = so_translate_android_ptr_if_needed(mod, *ptr);
                    if (DLA_DEBUG_LOGS && suspicious_prints < 64) {
                        DLA_DEBUG_PRINTF("[so_relocate] RELATIVE(android): addend=%p -> %p (text_elf=%p text_base=%p bias=%p)\n",
                                     (void *)(uintptr_t)*ptr, (void *)(uintptr_t)val,
                                     (void *)(uintptr_t)mod->text_start, (void *)(uintptr_t)mod->text_base, (void *)(uintptr_t)load_bias);
                        suspicious_prints++;
                    }
                } else {
                    // Normal ET_DYN case: add load bias.
                    val = *ptr + load_bias;
                }

                kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                break;
            }

            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT: {
                type_got++;
                if (sym->st_shndx != SHN_UNDEF) {
                    void *sym_rt = so_map_vaddr(mod, sym->st_value);
                    if (sym_rt) {
                        val = (uintptr_t)sym_rt;
                        kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                    }
                }
                break;
            }

            default:
                fatal_error("Error unknown relocation type %x\n", type);
                break;
        }
    }

    DLA_DEBUG_PRINTF("[so_relocate] done. rel=%u (android_addend=%u already_rt=%u) abs32=%u got=%u bias=%p\n",
                 type_rel, rel_addend_android, rel_addend_already_rt, type_abs32, type_got, (void *)(uintptr_t)load_bias);

    return 0;
}

// Translate an ELF VADDR to a runtime pointer.
//
// Important: this loader may place non-executable PT_LOAD segments (RW/RO)
// into separate memory blocks (mod->data_base[]). Relocations (r_offset) and
// other metadata are expressed as VADDRs, so we must map them to the correct
// runtime address based on which segment contains the VADDR.
static inline void *so_map_vaddr(so_module *mod, uintptr_t vaddr) {
    const uintptr_t text_start = (uintptr_t)mod->text_start;
    const uintptr_t text_end = text_start + (uintptr_t)mod->text_size;

    if (vaddr >= text_start && vaddr < text_end) {
        return (void *)((uintptr_t)mod->text_base + (vaddr - text_start));
    }

    for (int i = 0; i < mod->n_data; i++) {
        if (vaddr >= (uintptr_t)mod->data_start[i] && vaddr < (uintptr_t)mod->data_end[i]) {
            return (void *)((uintptr_t)mod->data_base[i] + (vaddr - (uintptr_t)mod->data_start[i]));
        }
    }

    return NULL;
}

uintptr_t so_resolve_link(so_module *mod, const char *symbol) {
    for (int i = 0; i < mod->num_dynamic; i++) {
        switch (mod->dynamic[i].d_tag) {
            case DT_NEEDED:
            {
                so_module *curr = head;
                while (curr) {
                    if (curr != mod && strcmp(curr->soname, mod->dynstr + mod->dynamic[i].d_un.d_ptr) == 0) {
                        uintptr_t link = so_symbol(curr, symbol);
                        if (link)
                            return link;
                    }
                    curr = curr->next;
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void reloc_err(uintptr_t got0)
{
    // Find to which module this missing symbol belongs
    int found = 0;
    so_module *curr = head;
    while (curr && !found) {
        for (int i = 0; i < curr->n_data; i++)
            if ((got0 >= curr->data_base[i]) && (got0 <= (uintptr_t)(curr->data_base[i] + curr->data_size)))
                found = 1;

        if (!found)
            curr = curr->next;
    }

    if (curr) {
        // Attempt to find symbol name and then display error
        for (int i = 0; i < curr->num_reldyn + curr->num_relplt; i++) {
            Elf32_Rel *rel = i < curr->num_reldyn ? &curr->reldyn[i] : &curr->relplt[i - curr->num_reldyn];
            Elf32_Sym *sym = &curr->dynsym[ELF32_R_SYM(rel->r_info)];
            uintptr_t *ptr = (uintptr_t *)(curr->text_base + rel->r_offset);

            int type = ELF32_R_TYPE(rel->r_info);
            switch (type) {
                case R_ARM_JUMP_SLOT:
                {
                    if (got0 == (uintptr_t)ptr) {
                        fatal_error("Unknown symbol \"%s\" (%p).\n", curr->dynstr + sym->st_name, (void*)got0);
                    }
                    break;
                }
            }
        }
    }

    // Ooops, this shouldn't have happened.
    fatal_error("Unknown symbol \"???\" (%p).\n", (void*)got0);
}

__attribute__((naked)) void plt0_stub()
{
    register uintptr_t got0 asm("r12");
    reloc_err(got0);
}

static const char *so_reloc_type_name(int type) {
    switch (type) {
        case R_ARM_ABS32: return "R_ARM_ABS32";
        case R_ARM_GLOB_DAT: return "R_ARM_GLOB_DAT";
        case R_ARM_JUMP_SLOT: return "R_ARM_JUMP_SLOT";
        default: return "R_ARM_<other>";
    }
}

static void so_import_diag_report(void) {
    if (g_import_diag_count == 0)
        return;

    DLA_DEBUG_PRINTF("[so_resolve][DIAG] import issues=%d (showing up to %d)\n",
                 g_import_diag_count, SO_IMPORT_DIAG_MAX);

    for (int i = 0; i < g_import_diag_count; i++) {
        const so_import_diag_entry *e = &g_import_diag[i];
        const char *kind = (e->kind == 0) ? "UNRESOLVED" : "BROKEN_PTR";
        DLA_DEBUG_PRINTF("[so_resolve][DIAG][%d] %s sym=%s type=%s r_off=0x%08x val=0x%08lx\n",
                     i,
                     kind,
                     e->symbol,
                     so_reloc_type_name(e->type),
                     e->r_off,
                     (unsigned long)e->value);
    }

#if SO_IMPORT_DIAG_FAIL_FAST
    const so_import_diag_entry *e = &g_import_diag[0];
    fatal_error("Import diagnostic failure: %s (%s) r_off=0x%08x val=0x%08lx\n",
               e->symbol,
               (e->kind == 0) ? "UNRESOLVED" : "BROKEN_PTR",
               e->r_off,
               (unsigned long)e->value);
#endif
}

int so_resolve(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    const int total = mod->num_reldyn + mod->num_relplt;
    const int default_count = size_default_dynlib / (int)sizeof(so_default_dynlib);

    // Reset stats for this resolution pass.
    g_so_resolve_stats.total_relocs = total;
    g_so_resolve_stats.resolved_from_deps = 0;
    g_so_resolve_stats.resolved_from_default = 0;
    g_so_resolve_stats.unresolved_total = 0;
    g_so_resolve_stats.unresolved_plt = 0;
    g_so_resolve_stats.unresolved_data = 0;
    so_import_diag_reset();

    uintptr_t val;

    for (int i = 0; i < total; i++) {
        Elf32_Rel *rel = (i < mod->num_reldyn) ? (mod->reldyn + i) : (mod->relplt + (i - mod->num_reldyn));

        Elf32_Sym *sym = mod->dynsym + ELF32_R_SYM(rel->r_info);
        void *ptr = so_map_vaddr(mod, rel->r_offset);
        if (!ptr) {
            const char *sname = (mod->dynstr && sym && sym->st_name) ? (mod->dynstr + sym->st_name) : "?";
            DLA_DEBUG_PRINTF("[so_resolve][WARN] r_offset=0x%08x not mapped (type=%d, sym=%s). Falling back to text_base mapping.\n",
                         (unsigned)rel->r_offset, ELF32_R_TYPE(rel->r_info), sname);
            ptr = (void *)(mod->text_base + rel->r_offset);
        }
        int type = ELF32_R_TYPE(rel->r_info);

        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT: {
                // The target memory can be protected, so we patch through kernel memcpy.

                // Local symbols are fully handled by so_relocate(). Re-applying
                // them here corrupts ABS32 slots (notably C++ RTTI/vtables)
                // because the relocation addend already contains the relocated
                // runtime pointer after the first pass.
                if (sym->st_shndx != SHN_UNDEF) {
                    if (type == R_ARM_JUMP_SLOT) {
                        uintptr_t cur = *(uintptr_t *)ptr;
                        if (!so_ptr_is_plausible_code(cur)) {
                            DLA_DEBUG_PRINTF("[so_resolve][BROKEN] self %s -> non-code ptr=0x%08lx\n",
                                         mod->dynstr + sym->st_name, (unsigned long)cur);
                            so_import_diag_add(1, mod->dynstr + sym->st_name, rel->r_offset, (uint8_t)type, cur);
                        }
                    }
                    if (SO_DEBUG_IMPORTS) {
                        DLA_DEBUG_PRINTF(
                            "[so_resolve][SKIP:self] %s (%s)\n",
                            mod->dynstr + sym->st_name,
                            so_reloc_type_name(type));
                    }
                    break;
                }

                const char *name = mod->dynstr + sym->st_name;
                int resolved = 0;

                // 1) Search dependency chain first (unless explicitly disabled).
                if (!default_dynlib_only) {
                    for (so_module *link = mod->next; link; link = link->next) {
                        val = so_symbol(link, name);
                        if (val) {
                            if (type == R_ARM_ABS32) {
                                uintptr_t addend = (uintptr_t)(*(uintptr_t *)ptr);
                                if (so_ptr_looks_like_android_addr(addend)) {
                                    DLA_DEBUG_PRINTF(
                                        "[so_resolve][WARN] ABS32 addend looks like an Android addr: %s addend=%p\n",
                                        name,
                                        (void *)addend);
                                    addend = 0;
                                }
                                val = addend + val;
                            }
                            kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                            if (type == R_ARM_JUMP_SLOT && !so_ptr_is_plausible_code(val)) {
                                DLA_DEBUG_PRINTF("[so_resolve][BROKEN] dep %s -> non-code ptr=0x%08lx\n",
                                             name, (unsigned long)val);
                                so_import_diag_add(1, name, rel->r_offset, (uint8_t)type, val);
                            }
                            g_so_resolve_stats.resolved_from_deps++;
                            if (SO_DEBUG_IMPORTS) {
                                DLA_DEBUG_PRINTF(
                                    "[so_resolve][RESOLVED:dep] %s -> %p (%s)\n",
                                    name,
                                    (void *)val,
                                    so_reloc_type_name(type));
                            }
                            resolved = 1;
                            break;
                        }
                    }
                }

                // 2) Built-in symbol map.
                if (!resolved) {
                    for (int j = 0; j < default_count; j++) {
                        if (strcmp(name, default_dynlib[j].symbol) == 0) {
                            val = (uintptr_t)default_dynlib[j].func;
							if (type == R_ARM_ABS32) {
								uintptr_t addend = (uintptr_t)(*(uintptr_t *)ptr);
								if (so_ptr_looks_like_android_addr(addend)) {
									DLA_DEBUG_PRINTF(
										"[so_resolve][WARN] ABS32 addend looks like Android VA (0x%08lx) for %s; ignoring addend\n",
										(unsigned long)addend,
										name);
									addend = 0;
								}
								val = addend + val;
							}
                            kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                            if (type == R_ARM_JUMP_SLOT && !so_ptr_is_plausible_code(val)) {
                                DLA_DEBUG_PRINTF("[so_resolve][BROKEN] def %s -> non-code ptr=0x%08lx\n",
                                             name, (unsigned long)val);
                                so_import_diag_add(1, name, rel->r_offset, (uint8_t)type, val);
                            }
                            g_so_resolve_stats.resolved_from_default++;
                            if (SO_DEBUG_IMPORTS) {
                                DLA_DEBUG_PRINTF(
                                    "[so_resolve][RESOLVED:def] %s -> %p (%s)\n",
                                    name,
                                    (void *)val,
                                    so_reloc_type_name(type));
                            }
                            resolved = 1;
                            break;
                        }
                    }
                }

                // 3) Unresolved: patch to a safe sentinel and print details.
                if (!resolved) {
                    g_so_resolve_stats.unresolved_total++;

                    // Be explicit about relocation details; this helps correlate with readelf output.
                    DLA_DEBUG_PRINTF(
                        "[so_resolve][UNRESOLVED] %s (%s) r_off=0x%08lx ptr=%p cur=0x%08lx\n",
                        name,
                        so_reloc_type_name(type),
                        (unsigned long)rel->r_offset,
                        (void *)ptr,
                        (unsigned long)(*(uintptr_t *)ptr));
                    so_import_diag_add(0, name, rel->r_offset, (uint8_t)type, (uintptr_t)(*(uintptr_t *)ptr));

                    if (type == R_ARM_JUMP_SLOT) {
                        g_so_resolve_stats.unresolved_plt++;
                        val = (uintptr_t)&plt0_stub;
                        kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                    } else {
                        // ABS32/GLOB_DAT usually represent data pointers; a NULL sentinel produces a
                        // deterministic crash (or a graceful NULL-check) while still being obvious.
                        g_so_resolve_stats.unresolved_data++;
                        val = 0;
                        kuKernelCpuUnrestrictedMemcpy(ptr, &val, sizeof(uintptr_t));
                    }
                }

	                // If the relocation slot still contains a value that looks like an
	                // Android userspace VA, log it. This frequently indicates that the
	                // relocation target address mapping is wrong or that a relocation
	                // was skipped.
	                {
	                    uintptr_t cur = *(uintptr_t *)ptr;
	                    if (so_ptr_looks_like_android_addr(cur & ~1u)) {
	                        DLA_DEBUG_PRINTF(
	                            "[so_resolve][SUSPICIOUS] %s (%s) r_off=0x%08lx ptr=%p -> 0x%08lx\n",
	                            name,
	                            so_reloc_type_name(type),
	                            (unsigned long)rel->r_offset,
	                            (void *)ptr,
	                            (unsigned long)cur);
	                    }
	                }

                break;
            }
            default:
                break;
        }
    }

    // Always print a compact summary. This is the quickest sanity check.
    DLA_DEBUG_PRINTF(
        "[so_resolve] relocs=%d dep=%d def=%d unresolved=%d (plt=%d data=%d)\n",
        g_so_resolve_stats.total_relocs,
        g_so_resolve_stats.resolved_from_deps,
        g_so_resolve_stats.resolved_from_default,
        g_so_resolve_stats.unresolved_total,
        g_so_resolve_stats.unresolved_plt,
        g_so_resolve_stats.unresolved_data);
    so_import_diag_report();

    return 0;
}

int __ret0() {
    return 0;
}

int so_resolve_with_dummy(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

        int type = ELF32_R_TYPE(rel->r_info);
        switch (type) {
            case R_ARM_ABS32:
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            {
                if (sym->st_shndx == SHN_UNDEF) {
                    for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
                        if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
                            *ptr = (uintptr_t) &__ret0;
                            break;
                        }
                    }
                }

                break;
            }
            default:
                break;
        }
    }

    return 0;
}

void so_initialize(so_module *mod) {
    if (SO_DEBUG_INIT) {
        DLA_DEBUG_PRINTF("[so_initialize] init_array=%p num_init_array=%d\n", (void*)mod->init_array, mod->num_init_array);
    }

#if SO_FIXUP_ANDROID_PTRS
    so_fixup_android_ptrs(mod);
#endif

    for (int i = 0; i < mod->num_init_array; i++) {
        if (!mod->init_array[i] || (int)mod->init_array[i] == -1)
            continue;

        uintptr_t f = (uintptr_t)mod->init_array[i];
        uintptr_t f_no_thumb = f & ~((uintptr_t)1);

        // If this looks like an Android-era absolute VADDR, try to translate it
        // into the Vita runtime address space instead of blindly skipping it.
        const uintptr_t f_translated = so_translate_android_ptr_if_needed(mod, f);
        if (f_translated != f) {
            DLA_DEBUG_PRINTF("[so_initialize] translated init_array[%d] %p -> %p\n", i, (void*)f, (void*)f_translated);
            f = f_translated;
            f_no_thumb = f & ~((uintptr_t)1);
        }

        if (SO_DEBUG_INIT) {
            DLA_DEBUG_PRINTF("[so_initialize] calling init_array[%d]=%p\n", i, (void*)f);
        }

        // If we ever see low Android-like addresses here, calling them will
        // immediately Prefetch-abort. Log and skip to keep the process alive
        // long enough to gather more import/relocation diagnostics.
        if (so_ptr_looks_like_android_addr(f_no_thumb)) {
            DLA_DEBUG_PRINTF("[so_initialize][WARN] skipping suspicious init_array[%d]=%p (still looks Android-ish)\n", i, (void*)f);
            continue;
        }

        if (so_should_skip_init_ctor(mod, i, f_no_thumb)) {
            continue;
        }

        // Android's linker may call constructors with argc/argv/envp.
        // Passing explicit zero/NULL avoids leaking garbage register values.
        ((void (*)(int, char **, char **))f)(0, NULL, NULL);

        if (SO_DEBUG_INIT) {
            DLA_DEBUG_PRINTF("[so_initialize] returned init_array[%d]\n", i);
        }
    }
}

uint32_t so_hash(const uint8_t *name) {
    uint64_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        if ((g = (h & 0xf0000000)) != 0)
            h ^= g >> 24;
        h &= 0x0fffffff;
    }
    return h;
}

static int so_symbol_index(so_module *mod, const char *symbol)
{
    if (mod->hash) {
        uint32_t hash = so_hash((const uint8_t *)symbol);
        uint32_t nbucket = mod->hash[0];
        uint32_t *bucket = &mod->hash[2];
        uint32_t *chain = &bucket[nbucket];
        for (int i = bucket[hash % nbucket]; i; i = chain[i]) {
            if (mod->dynsym[i].st_shndx == SHN_UNDEF)
                continue;
            if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
                return i;
        }
    }

    for (int i = 0; i < mod->num_dynsym; i++) {
        if (mod->dynsym[i].st_shndx == SHN_UNDEF)
            continue;
        if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
            return i;
    }

    return -1;
}

/*
 * alloc_arena: allocates space on either patch or cave arenas,
 * range: maximum range from allocation to dst (ignored if NULL)
 * dst: destination address
*/
uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz) {
    // Is address in range?
#define inrange(lsr, gtr, range) \
		(((uintptr_t)(range) == (uintptr_t)NULL) || ((uintptr_t)(range) >= ((uintptr_t)(gtr) - (uintptr_t)(lsr))))
    // Space left on block
#define blkavail(type) (so->type##_size - (so->type##_head - so->type##_base))

    // keep allocations 4-byte aligned for simplicity
    sz = ALIGN_MEM(sz, 4);

    if (sz <= (blkavail(patch)) && inrange(so->patch_base, dst, range)) {
        so->patch_head += sz;
        return (so->patch_head - sz);
    } else if (sz <= (blkavail(cave)) && inrange(dst, so->cave_base, range)) {
        so->cave_head += sz;
        return (so->cave_head - sz);
    }

    return (uintptr_t)NULL;
}

static void trampoline_ldm(so_module *mod, uint32_t *dst) {
    uint32_t trampoline[1];
    uint32_t funct[20] = {0xFAFAFAFA};
    uint32_t *ptr = funct;

    int cur = 0;
    int baseReg = ((*dst) >> 16) & 0xF;
    int bitMask = (*dst) & 0xFFFF;

    uint32_t stored = (uint32_t) NULL;
    for (int i = 0; i < 16; i++) {
        if (bitMask & (1 << i)) {
            // If the register we're reading the offset from is the same as the one we're writing,
            // delay it to the very end so that the base pointer ins't clobbered
            if (baseReg == i)
                stored = LDR_OFFS(i, baseReg, cur).raw;
            else
                *ptr++ = LDR_OFFS(i, baseReg, cur).raw;
            cur += 4;
        }
    }

    // Perform the delayed load if needed
    if (stored) {
        *ptr++ = stored;
    }

    *ptr++ = (uint32_t) 0xe51ff004; // LDR PC, [PC, -0x4] ; jmp to [dst+0x4]
    *ptr++ = (uint32_t) dst+1; // .dword <...>	; [dst+0x4]

    size_t trampoline_sz =	((uintptr_t)ptr - (uintptr_t)&funct[0]);
    uintptr_t patch_addr = so_alloc_arena(mod, B_RANGE, (uintptr_t) B_OFFSET(dst), trampoline_sz);

    if (!patch_addr) {
        fatal_error("Failed to patch LDMIA at 0x%08X, unable to allocate space.\n", dst);
    }

    // Create sign extended relative address rel_addr
    trampoline[0] = B(dst, patch_addr).raw;

    kuKernelCpuUnrestrictedMemcpy((void*)patch_addr, funct, trampoline_sz);
    kuKernelCpuUnrestrictedMemcpy(dst, trampoline, sizeof(trampoline));
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
    int index = so_symbol_index(mod, symbol);
    if (index == -1)
        return (uintptr_t) NULL;

    return mod->text_base + mod->dynsym[index].st_value;
}

void so_symbol_fix_ldmia(so_module *mod, const char *symbol) {
    // This is meant to work around crashes due to unaligned accesses (SIGBUS :/) due to certain
    // kernels not having the fault trap enabled, e.g. certain RK3326 Odroid Go Advance clone distros.
    // TODO:: Maybe enable this only with a config flag? maybe with a list of known broken functions?
    // Known to trigger on GM:S's "_Z11Shader_LoadPhjS_" - if it starts happening on other places,
    // might be worth enabling it globally.

    int idx = so_symbol_index(mod, symbol);
    if (idx == -1)
        return;

    uintptr_t st_addr = mod->text_base + mod->dynsym[idx].st_value;
    for (uintptr_t addr = st_addr; addr < st_addr + mod->dynsym[idx].st_size; addr+=4) {
        uint32_t inst = *(uint32_t*)(addr);

        //Is this an LDMIA instruction with a R0-R12 base register?
        if (((inst & 0xFFF00000) == 0xE8900000) && (((inst >> 16) & 0xF) < 13) ) {
            DLA_DEBUG_PRINTF("Found possibly misaligned LDMIA on 0x%08X, trying to fix it... (instr: 0x%08X, to 0x%08X)\n", addr, *(uint32_t*)addr, mod->patch_head);
            trampoline_ldm(mod, (uint32_t *) addr);
        }
    }
}
