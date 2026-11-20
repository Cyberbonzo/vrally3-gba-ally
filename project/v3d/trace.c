#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint32_t (*interp_one)(void *);
    uint32_t (*read8)(void *, uint32_t);
    uint32_t (*read16)(void *, uint32_t);
    uint32_t (*read32)(void *, uint32_t);
} V3dTraceApi;

enum { command_capacity = 4096, words = 48 };
typedef struct {
    uint32_t guest_address;
    uint32_t data[words];
} Command;

static Command pass[command_capacity], complete[command_capacity];
static uint32_t count, complete_count, pass_number;
static uint8_t vram[0x18000], ewram[0x40000], iwram[0x8000];
static uint8_t palette[0x400], oam[0x400];
static uint8_t before_page[240 * 160], complete_before[240 * 160];
static uint32_t registers[7];
static int active;
static long selected_pass = -1;

static void trace_copy(const V3dTraceApi *api, void *m, uint8_t *out, uint32_t addr, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        out[i] = api->read8(m, addr + (uint32_t)i);
}

void v3d_trace(const void *runtime, void *m, uint32_t address)
{
    const V3dTraceApi *api = runtime;
    if (address == 0x030033fc) {
        const uint32_t mode = api->read16(m, 0x04000000);
        active = (mode & 7) == 4;
        if (active) {
            ++pass_number;
            count = 0;
            const uint32_t table = api->read32(m, 0x030035f4);
            if (table < 0x02000000 || table > 0x0203fc00) {
                active = 0;
                return;
            }
            for (int i = 255; i >= 0; --i) {
                uint32_t addr = api->read32(m, table + 4 * i);
                while (addr && count < command_capacity) {
                    if (addr < 0x02000000 || addr >= 0x02040000)
                        break;
                    const uint32_t type = api->read32(m, addr + 4);
                    if (type > 4 || type == 0)
                        break;
                    Command *record = &pass[count++];
                    record->guest_address = addr;
                    for (uint32_t j = 0; j < words; ++j)
                        record->data[j] = api->read32(m, addr + 4 * j);
                    const uint32_t next = record->data[0];
                    if (next == addr)
                        break;
                    addr = next;
                }
            }
            const uint32_t target = api->read32(m, 0x03003a84);
            if (target == 0x06000000 || target == 0x0600a000)
                trace_copy(api, m, before_page, target, sizeof(before_page));
        }
    } else if (active && address == 0x0300346c) {
        active = 0;
        if (count < 20)
            return;
        if (selected_pass < 0) {
            const char *select = getenv("V3D_PASS");
            selected_pass = select ? strtol(select, NULL, 10) : 0;
        }
        if (selected_pass > 0 && (uint32_t)selected_pass != pass_number)
            return;
        memcpy(complete, pass, sizeof(Command) * count);
        complete_count = count;
        memcpy(complete_before, before_page, sizeof(before_page));
        registers[0] = pass_number;
        registers[1] = api->read16(m, 0x04000000);
        registers[2] = api->read32(m, 0x03003604);
        registers[3] = api->read32(m, 0x03003608);
        registers[4] = api->read32(m, 0x0300360c);
        registers[5] = api->read32(m, 0x03003600);
        registers[6] = api->read32(m, 0x03003a84);
        trace_copy(api, m, palette, 0x05000000, sizeof(palette));
        trace_copy(api, m, vram, 0x06000000, sizeof(vram));
        trace_copy(api, m, oam, 0x07000000, sizeof(oam));
        trace_copy(api, m, ewram, 0x02000000, sizeof(ewram));
        trace_copy(api, m, iwram, 0x03000000, sizeof(iwram));
    }
}

__attribute__((destructor)) static void write_trace(void)
{
    if (!complete_count)
        return;
    const char *path = getenv("V3D_TRACE_FILE");
    if (!path)
        return;
    FILE *out = fopen(path, "wb");
    if (!out)
        return;
    const uint32_t magic = 0x32523356;
    fwrite(&magic, sizeof(magic), 1, out);
    fwrite(&complete_count, sizeof(complete_count), 1, out);
    fwrite(registers, sizeof(registers), 1, out);
    fwrite(complete, sizeof(Command), complete_count, out);
    fwrite(palette, sizeof(palette), 1, out);
    fwrite(vram, sizeof(vram), 1, out);
    fwrite(oam, sizeof(oam), 1, out);
    fwrite(ewram, sizeof(ewram), 1, out);
    fwrite(iwram, sizeof(iwram), 1, out);
    fwrite(complete_before, sizeof(complete_before), 1, out);
    fclose(out);
    fprintf(stderr, "V3D trace contains %u commands from pass %u\n", complete_count, registers[0]);
}

