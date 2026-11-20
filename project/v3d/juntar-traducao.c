#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t (*BlockFn)(const void *, void *);
typedef struct { uint32_t key; BlockFn execute; } Block;
enum { capacity = 16384 };
Block rcg_blocks[capacity];
uint64_t rcg_block_count;

static int compare(const void *a, const void *b)
{
    const Block *lhs = a, *rhs = b;
    return (lhs->key > rhs->key) - (lhs->key < rhs->key);
}

typedef struct {
    uint32_t (*interp_one)(void *);
    uint32_t (*read8)(void *, uint32_t);
    uint32_t (*read16)(void *, uint32_t);
    uint32_t (*read32)(void *, uint32_t);
    void (*write8)(void *, uint32_t, uint32_t);
    void (*write16)(void *, uint32_t, uint32_t);
    void (*write32)(void *, uint32_t, uint32_t);
    void (*tick)(void *, uint32_t);
    uint32_t (*guard)(void *, uint32_t, uint32_t, uint64_t);
    uint32_t (*chain_gate)(void *);
} Runtime;

#ifdef VRALLY3_USE_OVERLAYS
typedef struct {
    uint32_t addr, primary_len, alternate_len;
    uint64_t primary_hash, alternate_hash;
} Overlay;
#include "overlays.h"

enum { overlay_start = 0x03007700, overlay_capacity = 0x300 };
static struct {
    const Overlay *spec;
    BlockFn primary, alternate;
} variants[overlay_capacity];

static uint32_t execute_variant(const void *runtime, void *m)
{
    const Runtime *api = runtime;
    const uint32_t addr = ((const uint32_t *)m)[15];
    const uint32_t index = addr - overlay_start;
    if (index >= overlay_capacity || !variants[index].spec)
        return api->interp_one(m);
    const Overlay *o = variants[index].spec;
    if (!api->guard(m, addr, o->primary_len, o->primary_hash) &&
        api->guard(m, addr, o->alternate_len, o->alternate_hash))
        return variants[index].alternate(runtime, m);
    return variants[index].primary(runtime, m);
}

static void install_variants(const Block *secondary, uint64_t secondary_n)
{
    for (size_t n = 0; n < sizeof(overlay_specs) / sizeof(*overlay_specs); ++n) {
        const Overlay *spec = &overlay_specs[n];
        const Block match = {spec->addr, NULL};
        Block *primary = bsearch(&match, rcg_blocks, rcg_block_count, sizeof(Block), compare);
        if (!primary)
            continue;
        for (uint64_t i = 0; i < secondary_n; ++i) {
            if (secondary[i].key == spec->addr) {
                const uint32_t index = spec->addr - overlay_start;
                variants[index].spec = spec;
                variants[index].primary = primary->execute;
                variants[index].alternate = secondary[i].execute;
                primary->execute = execute_variant;
                break;
            }
        }
    }
}
#endif

static Block dispatch_table[capacity];
static const Runtime *runtime_api;
static Runtime traced_api;
static uint64_t interpreter_calls[256];
static uint64_t interpreter_sites[capacity];

static uint32_t measure_interpreter(void *m)
{
    const uint32_t *cpu = m;
    const Block key = {cpu[15] | ((cpu[16] >> 5) & 1u), NULL};
    ++interpreter_calls[cpu[15] >> 24];
    const Block *block = bsearch(&key, dispatch_table, rcg_block_count, sizeof(Block), compare);
    if (block)
        ++interpreter_sites[block - dispatch_table];
    return runtime_api->interp_one(m);
}

static uint32_t measure_block(const void *original, void *m)
{
    if (runtime_api != original) {
        runtime_api = original;
        traced_api = *runtime_api;
        traced_api.interp_one = measure_interpreter;
    }
    const uint32_t *cpu = m;
    const Block key = {cpu[15] | ((cpu[16] >> 5) & 1u), NULL};
    const Block *block = bsearch(&key, dispatch_table, rcg_block_count, sizeof(Block), compare);
    return block ? block->execute(&traced_api, m) : measure_interpreter(m);
}

__attribute__((destructor)) static void print_counts(void)
{
    if (!runtime_api)
        return;
    uint64_t total = 0;
    for (unsigned r = 0; r < 256; ++r) {
        total += interpreter_calls[r];
        if (interpreter_calls[r])
            fprintf(stderr, "interpreter address region %02x: %llu calls\n", r,
                    (unsigned long long)interpreter_calls[r]);
    }
    fprintf(stderr, "interpreter calls from native blocks: %llu\n", (unsigned long long)total);
    for (uint64_t i = 0; i < rcg_block_count; ++i) {
        if (interpreter_sites[i] > 500)
            fprintf(stderr, "interpreter dispatch %08x: %llu calls\n", dispatch_table[i].key,
                    (unsigned long long)interpreter_sites[i]);
    }
}

static int read_library(const char *dir, const char *name, const Block **blocks, uint64_t *count)
{
    char *path = NULL;
    if (asprintf(&path, "%s/%s", dir, name) < 0)
        return 0;
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    free(path);
    if (!library) {
        fprintf(stderr, "%s: %s\n", name, dlerror());
        return 0;
    }
    *blocks = dlsym(library, "rcg_blocks");
    const uint64_t *len = dlsym(library, "rcg_block_count");
    if (!*blocks || !len || *len > capacity)
        return 0;
    *count = *len;
    return 1;
}

__attribute__((constructor)) static void load_blocks(void)
{
    Dl_info where;
    if (!dladdr(&rcg_blocks, &where))
        return;
    char *dir = realpath(where.dli_fname, NULL);
    if (!dir)
        return;
    char *leaf = strrchr(dir, '/');
    if (leaf)
        *leaf = '\0';
    const Block *primary, *secondary;
    uint64_t primary_n, secondary_n;
    if (!read_library(leaf ? dir : ".", "original.so", &primary, &primary_n) ||
        !read_library(leaf ? dir : ".", "additional.so", &secondary, &secondary_n)) {
        free(dir);
        return;
    }
    free(dir);
    memcpy(rcg_blocks, primary, primary_n * sizeof(Block));
    qsort(rcg_blocks, primary_n, sizeof(Block), compare);
    rcg_block_count = primary_n;
    for (uint64_t n = 0; n < secondary_n; ++n) {
        if (rcg_block_count == capacity)
            break;
        if (!bsearch(&secondary[n], rcg_blocks, primary_n, sizeof(Block), compare))
            rcg_blocks[rcg_block_count++] = secondary[n];
    }
    qsort(rcg_blocks, rcg_block_count, sizeof(Block), compare);
#ifdef VRALLY3_USE_OVERLAYS
    install_variants(secondary, secondary_n);
#endif
    if (getenv("VRALLY3_TRACE_INTERPRETER")) {
        memcpy(dispatch_table, rcg_blocks, rcg_block_count * sizeof(Block));
        for (uint64_t i = 0; i < rcg_block_count; ++i)
            rcg_blocks[i].execute = measure_block;
    }

}

