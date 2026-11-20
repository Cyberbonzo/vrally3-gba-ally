#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { width = 1920, height = 1080, sw = 240, sh = 160, max_commands = 4096, words = 49 };
typedef struct {
    uint32_t (*interp_one)(void *);
    uint32_t (*read8)(void *, uint32_t);
    uint32_t (*read16)(void *, uint32_t);
    uint32_t (*read32)(void *, uint32_t);
} Runtime;

int v3d_draw_filtered(uint8_t *, int, int, const int32_t *, unsigned,
             const uint8_t *, const uint8_t *, int, const uint16_t *);

typedef struct {
    uint8_t ground[width * height];
    uint8_t original[sw * sh];
    unsigned serial;
} Page;

static Page pages[2];
static int32_t commands[max_commands][words];
static unsigned command_count, serial;
int v3d_race_projection;
static uint8_t ewram[0x40000], iwram[0x8000];
static int16_t source_x[width], source_y[height], guest_edge[sw + 1], next_column[width];
static uint8_t edited[sw * sh];
static uint32_t rgba_lut[32768];
static int initialized;
static const double scale = height / (double)sh;

static void read_memory(const Runtime *api, void *m, uint8_t *out, uint32_t address, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        const uint32_t word = api->read32(m, address + (uint32_t)i);
        memcpy(out + i, &word, sizeof(word));
    }
}

/* The dispatcher fills a back page. The PPU may show the other page for
 * several refreshes, so associate the high resolution image with its page. */
void v3d_trace(const void *runtime, void *m, uint32_t address)
{
    const Runtime *api = runtime;
    if (address == 0x030033fc) {
        command_count = 0;
        if ((api->read16(m, 0x04000000) & 7) != 4)
            return;
        const uint32_t table = api->read32(m, 0x030035f4);
        if (table < 0x02000000 || table > 0x0203fc00)
            return;
        // The menus render a model into a prepainted bitmap. On a race
        // track V3D draws on a cleared back page, with the sky supplied by OBJ.
        const uint32_t target = api->read32(m, 0x03003a84);
        v3d_race_projection = 0;
        if (target == 0x06000000 || target == 0x0600a000) {
            unsigned painted = 0;
            for (int y = 0; y < sh; y += 8)
                for (int x = 0; x < sw; x += 12)
                    painted += api->read32(m, target + y * sw + x) != 0;
            v3d_race_projection = painted < 20;
        }
        for (int i = 255; i >= 0; --i) {
            uint32_t addr = api->read32(m, table + 4 * i);
            while (addr >= 0x02000000 && addr <= 0x02040000 - 48 * 4 &&
                   command_count < max_commands) {
                const uint32_t type = api->read32(m, addr + 4);
                if (type > 4 || type == 0)
                    break;
                int32_t *out = commands[command_count++];
                out[0] = (int32_t)addr;
                for (unsigned j = 0; j < 48; ++j)
                    out[j + 1] = (int32_t)api->read32(m, addr + 4 * j);
                const uint32_t next = (uint32_t)out[1];
                if (next == addr)
                    break;
                addr = next;
            }
        }
    } else if (address == 0x0300346c && command_count) {
        const unsigned count = command_count;
        command_count = 0;
        const uint32_t target = api->read32(m, 0x03003a84);
        if (target != 0x06000000 && target != 0x0600a000)
            return;
        Page *page = &pages[target != 0x06000000];
        if (!v3d_race_projection) {
            page->serial = 0;
            return;
        }
        read_memory(api, m, ewram, 0x02000000, sizeof(ewram));
        read_memory(api, m, iwram, 0x03000000, sizeof(iwram));
        read_memory(api, m, page->original, target, sizeof(page->original));
        static int filter = -1;
        if (filter < 0) {
            const char *setting = getenv("VRALLY3_TERRAIN_FILTER");
            filter = !setting || strcmp(setting, "0") != 0;
        }
        uint16_t palette[256];
        if (filter)
            read_memory(api, m, (uint8_t *)palette, 0x05000000, sizeof(palette));
        v3d_draw_filtered(page->ground, width, height, &commands[0][0], count,
                          ewram, iwram, 2, filter ? palette : NULL);
        page->serial = ++serial;
    }
}

static uint16_t word(const uint8_t *p, int off)
{
    return p[off] | (uint16_t)p[off + 1] << 8;
}

static unsigned color_mixed(unsigned sprite, unsigned under, int eva, int evb)
{
    unsigned result = 0;
    for (int bit = 0; bit <= 10; bit += 5) {
        unsigned mix = (((sprite >> bit & 31) * eva + (under >> bit & 31) * evb) >> 4);
        result |= (mix < 31 ? mix : 31) << bit;
    }
    return result;
}

static int clip(double pos, int max)
{
    const int value = (int)ceil(pos - 0.5);
    return value < 0 ? 0 : (value > max ? max : value);
}

static void draw_objects(uint8_t *rgba, const uint8_t *ground, const uint8_t *display,
                         const uint8_t *vram, const uint8_t *oam, const uint8_t *pal,
                         const uint8_t *io, const uint16_t *backdrop, uint16_t mode)
{
    static const unsigned dims[3][4][2] = {
        {{8,8}, {16,16}, {32,32}, {64,64}},
        {{16,8}, {32,8}, {32,16}, {64,32}},
        {{8,16}, {8,32}, {16,32}, {32,64}}
    };
    const unsigned bg_rank = word(io, 12) & 3;
    const unsigned control = word(io, 0x50), alpha = word(io, 0x52);
    const int ea = alpha & 31, eb = (alpha >> 8) & 31;
    const double center = (width - sw * scale) / 2;
    for (int rank = 3; rank >= 0; --rank) {
        // The cloud and horizon layers repeat every 256 guest pixels. The
        // game normally emits duplicates only for its 240-pixel viewport.
        for (int instance = 383; instance >= 0; --instance) {
            const int n = instance % 128;
            const int shift = instance < 128 ? 0 : (instance < 256 ? -256 : 256);
            const unsigned a0 = word(oam, n * 8), a1 = word(oam, n * 8 + 2);
            const unsigned a2 = word(oam, n * 8 + 4);
            const unsigned shape = a0 >> 14, blend = a0 >> 10 & 3, start = a2 & 1023;
            const int affine = (a0 & 256) != 0, doubled = (a0 & 512) != 0;
            if ((int)(a2 >> 10 & 3) != rank || shape > 2 || blend > 1 ||
                (!affine && doubled) || start < 512)
                continue;
            const unsigned bank = a2 >> 12;
            if (shift && (rank != 1 || affine || (bank != 3 && bank != 4)))
                continue;
            const int w = dims[shape][a1 >> 14][0], h = dims[shape][a1 >> 14][1];
            const int bw = w * (1 + (affine && doubled)), bh = h * (1 + (affine && doubled));
            const int x = shift + (a1 & 511) - ((a1 & 256) ? 512 : 0);
            const int y = (a0 & 255) - ((a0 & 255) >= 160 ? 256 : 0);
            int x0 = clip(center + x * scale, width), x1 = clip(center + (x + bw) * scale, width);
            if (shift < 0 && x1 > (int)center) x1 = (int)center;
            if (shift > 0 && x0 < width - (int)center) x0 = width - (int)center;
            if (x0 >= x1) continue;
            const int y0 = clip(y * scale, height), y1 = clip((y + bh) * scale, height);
            const int matrix = (a1 >> 9 & 31) * 32;
            const double pa = (int16_t)word(oam, matrix + 6) / 256.0;
            const double pb = (int16_t)word(oam, matrix + 14) / 256.0;
            const double pc = (int16_t)word(oam, matrix + 22) / 256.0;
            const double pd = (int16_t)word(oam, matrix + 30) / 256.0;
            const int eight_bits = (a0 & 8192) != 0;
            const unsigned stride = 1 + eight_bits;
            const unsigned row_tiles = mode & 64 ? w / 8 * stride : 32;
            for (int iy = y0; iy < y1; ++iy) {
                const double ly = (iy + 0.5) / scale - y;
                for (int ix = x0; ix < x1;) {
                    const int first = ix;
                    ix = affine ? ix + 1 : (next_column[ix] < x1 ? next_column[ix] : x1);
                    const double lx = (first + 0.5 - center) / scale - x;
                    int u = (int)lx, v = (int)ly;
                    if (affine) {
                        const double dx = lx - bw / 2.0, dy = ly - bh / 2.0;
                        u = (int)floor(dx * pa + dy * pb + w / 2.0);
                        v = (int)floor(dx * pc + dy * pd + h / 2.0);
                    } else {
                        if (a1 & 4096) u = w - 1 - u;
                        if (a1 & 8192) v = h - 1 - v;
                    }
                    if ((unsigned)u >= (unsigned)w || (unsigned)v >= (unsigned)h)
                        continue;
                    const unsigned tile = start + v / 8 * row_tiles + u / 8 * stride;
                    const unsigned address = eight_bits
                        ? 0x10000 + tile / 2 * 64 + (v & 7) * 8 + (u & 7)
                        : 0x10000 + tile * 32 + (v & 7) * 4 + ((u & 7) >> 1);
                    if (address >= 0x18000)
                        continue;
                    const unsigned index = eight_bits ? vram[address]
                        : (vram[address] >> ((u & 1) * 4)) & 15;
                    if (!index)
                        continue;
                    const unsigned pal_index = 256 + index + (eight_bits ? 0 : (a2 >> 12) * 16);
                    const unsigned color = word(pal, pal_index * 2) & 32767;
                    const size_t line = (size_t)iy * width;
                    if (!blend && (unsigned)rank <= bg_rank) {
                        const uint32_t pixel = rgba_lut[color];
                        for (int px = first; px < ix; ++px)
                            memcpy(rgba + 4 * (line + px), &pixel, 4);
                        continue;
                    }
                    const int sx = source_x[first], sy = source_y[iy];
                    const int has_edit = (unsigned)sx < sw && edited[sy * sw + sx];
                    for (int px = first; px < ix; ++px) {
                        const size_t pos = line + px;
                        const unsigned bg = has_edit ? display[sy * sw + sx] : ground[pos];
                        if (bg && (unsigned)rank > bg_rank) continue;
                        unsigned result = color;
                        if (blend && (control & (bg ? 1 << 10 : 1 << 13))) {
                            const unsigned underneath = bg ? word(pal, bg * 2) : backdrop[sy];
                            result = color_mixed(color, underneath, ea < 16 ? ea : 16, eb < 16 ? eb : 16);
                        }
                        memcpy(rgba + 4 * pos, &rgba_lut[result], 4);
                    }
                }
            }
        }
    }
}

/* Output format is memory-order RGBA8. The guest palette 0 is recorded at
 * scanline start because the game rewrites it in HBlank to shade the sky. */
unsigned vrally3_render_frame(const uint8_t *vram, const uint8_t *oam, const uint8_t *pal,
                              const uint8_t *io, const uint16_t *backdrop, unsigned mode,
                              uint8_t *rgba)
{
    if ((mode & 7) != 4 || (mode & 128) || !(mode & 1024))
        return 0;
    const unsigned visible = (mode >> 4) & 1;
    const Page *page = &pages[visible];
    if (!page->serial || serial - page->serial > 2)
        return 0;
    if (!initialized) {
        for (int x = 0; x < width; ++x)
            source_x[x] = (int)floor((x + 0.5 - width * 0.5) / scale + sw * 0.5);
        for (int x = width - 1; x >= 0; --x)
            next_column[x] = (x + 1 < width && source_x[x] == source_x[x + 1])
                ? next_column[x + 1] : x + 1;
        int edge = 0;
        for (int gx = 0; gx <= sw; ++gx) {
            while (edge < width && source_x[edge] < gx) ++edge;
            guest_edge[gx] = (int16_t)edge;
        }
        for (int y = 0; y < height; ++y)
            source_y[y] = (int)floor((y + 0.5) / scale);
        for (unsigned color = 0; color < 32768; ++color) {
            const unsigned r = (color & 31) * 255 / 31;
            const unsigned g = (color >> 5 & 31) * 255 / 31;
            const unsigned b = (color >> 10 & 31) * 255 / 31;
            rgba_lut[color] = 0xff000000 | b << 16 | g << 8 | r;
        }
        initialized = 1;
    }
    const uint8_t *display = vram + visible * 0xa000;
    /* Car and particles are extra bitmap writes after the V3D renderer.
     * Reject a changed page rather than misidentifying a menu as a race. */
    unsigned changed = 0;
    for (size_t i = 0; i < sizeof(edited); ++i) {
        edited[i] = display[i] != page->original[i];
        changed += edited[i];
    }
    // Returning to a bitmap menu replaces most of a page in one operation.
    if (changed > sizeof(edited) / 2)
        return 0;
    uint32_t palette[256];
    for (unsigned i = 1; i < 256; ++i)
        palette[i] = rgba_lut[word(pal, i * 2) & 32767];
    for (int y = 0; y < height; ++y) {
        const int sy = source_y[y];
        palette[0] = rgba_lut[backdrop[sy] & 32767];
        const uint8_t *src = page->ground + (size_t)y * width;
        uint8_t *row = rgba + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x)
            memcpy(row + 4*x, &palette[src[x]], 4);
        const uint8_t *original_row = display + sy * sw;
        const uint8_t *edit_row = edited + sy * sw;
        for (int sx = 0; sx < sw; ++sx) {
            if (!edit_row[sx]) continue;
            const uint32_t color = palette[original_row[sx]];
            for (int x = guest_edge[sx]; x < guest_edge[sx + 1]; ++x)
                memcpy(row + 4*x, &color, 4);
        }
    }
    if (mode & 4096)
        draw_objects(rgba, page->ground, display, vram, oam, pal, io, backdrop, mode);
    return page->serial;
}

