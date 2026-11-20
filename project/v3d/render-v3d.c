#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { words = 49, max_vertices = 9 };
typedef struct { double y, x, u, v; } Vertex;
typedef struct { double x, u, v; } Intersection;
typedef struct { const uint8_t *ewram, *iwram; } Memory;

static const uint8_t *texture_base(Memory memory, uint32_t address, uint32_t *mask)
{
    switch (address >> 24) {
    case 2: *mask = 0x3ffff; return memory.ewram;
    case 3: *mask = 0x7fff; return memory.iwram;
    default: return NULL;
    }
}

static int edge(const Vertex *a, const Vertex *b, double y, Intersection *at)
{
    if (!((a->y <= y && y < b->y) || (b->y <= y && y < a->y)))
        return 0;
    const double t = (y - a->y) / (b->y - a->y);
    at->x = a->x + (b->x - a->x) * t;
    at->u = a->u + (b->u - a->u) * t;
    at->v = a->v + (b->v - a->v) * t;
    return 1;
}

static int clipped_start(double position, int max)
{
    const int i = (int)ceil(position - 0.5);
    return i < 0 ? 0 : (i > max ? max : i);
}

static void polygon(uint8_t *out, int width, int height, const int32_t *command,
                    Memory memory, double offset_x, double scale_x, double scale_y)
{
    const int solid = command[2] == 1;
    const unsigned count = (unsigned)command[5];
    if (count < 3 || count > max_vertices)
        return;
    Vertex vertices[max_vertices];
    double lo = INFINITY, hi = -INFINITY;
    for (unsigned i = 0; i < count; ++i) {
        const int32_t *v = command + 6 + (solid ? 2 : 4) * i;
        if (v[0] < -4096 || v[0] >= 4096 || v[1] < -65536 || v[1] >= 65536)
            return;
        vertices[i] = (Vertex){
            .y = v[0] * scale_y, .x = offset_x + v[1] * scale_x / 16,
            .u = solid ? 0 : v[2] / 16.0, .v = solid ? 0 : v[3] / 16.0
        };
        if (vertices[i].y < lo) lo = vertices[i].y;
        if (vertices[i].y > hi) hi = vertices[i].y;
    }
    const uint32_t address = (uint32_t)command[4];
    uint32_t mask = 0;
    const uint8_t *texture = solid ? NULL : texture_base(memory, address, &mask);
    if (!solid && !texture)
        return;

    const int first = clipped_start(lo, height), last = clipped_start(hi, height);
    for (int y = first; y < last; ++y) {
        Intersection left = {INFINITY, 0, 0}, right = {-INFINITY, 0, 0};
        for (unsigned i = 0; i < count; ++i) {
            Intersection at;
            const unsigned next = (i + 1 == count) ? 0 : i + 1;
            if (edge(&vertices[i], &vertices[next], y + 0.5, &at)) {
                if (at.x < left.x) left = at;
                if (at.x > right.x) right = at;
            }
        }
        if (!(right.x > left.x))
            continue;
        const int x0 = clipped_start(left.x, width), x1 = clipped_start(right.x, width);
        uint8_t *row = out + (size_t)y * width;
        if (solid) {
            memset(row + x0, address & 255, (size_t)(x1 - x0));
            continue;
        }
        const double reciprocal = 1.0 / (right.x - left.x);
        for (int x = x0; x < x1; ++x) {
            const double t = (x + 0.5 - left.x) * reciprocal;
            const unsigned u = (unsigned)(int)floor(left.u + (right.u - left.u) * t) & 255;
            const unsigned v = (unsigned)(int)floor(left.v + (right.v - left.v) * t) & 255;
            row[x] = texture[(address + 256 * u + v) & mask];
        }
    }
}

static void sprite(uint8_t *out, int width, int height, const int32_t *command,
                   Memory memory, double offset_x, double scale_x, double scale_y, int wide)
{
    const int32_t *q = command + 5;
    if (q[0] < -4096 || q[4] >= 4096 || q[1] < -4096 || q[5] >= 4096)
        return;
    if (q[2] < 0 || q[2] > 65535 || q[3] < 0 || q[3] > 65535 ||
        q[6] < 0 || q[6] > 65535 || q[7] < 0 || q[7] > 65535)
        return;
    const double aspect = wide ? 27.0 / 32 : 1.0;
    const double left = offset_x + (120 + (q[1] - 120) * aspect) * scale_x;
    const double right = offset_x + (120 + (q[5] - 120) * aspect) * scale_x;
    const double top = q[0] * scale_y, bottom = q[4] * scale_y;
    if (!(right > left && bottom > top))
        return;
    const uint32_t address = (uint32_t)command[4];
    uint32_t mask;
    const uint8_t *texture = texture_base(memory, address, &mask);
    if (!texture)
        return;
    const int y0 = clipped_start(top, height), y1 = clipped_start(bottom, height);
    const int x0 = clipped_start(left, width), x1 = clipped_start(right, width);
    for (int y = y0; y < y1; ++y) {
        const double fy = (y + 0.5 - top) / (bottom - top);
        const unsigned u = (unsigned)(int)(q[2] + (q[6] + 1.0 - q[2]) * fy);
        const uint32_t col = address + 256 * u;
        uint8_t *row = out + (size_t)y * width;
        for (int x = x0; x < x1; ++x) {
            const double fx = (x + 0.5 - left) / (right - left);
            const unsigned v = (unsigned)(int)(q[3] + (q[7] + 1.0 - q[3]) * fx);
            const uint8_t texel = texture[(col + v) & mask];
            if (texel != 0)
                row[x] = texel;
        }
    }
}

int v3d_draw(uint8_t *out, int width, int height, const int32_t *commands,
             unsigned count, const uint8_t *ewram, const uint8_t *iwram, int wide)
{
    if (!out || !commands || !ewram || !iwram || width < 1 || width > 4096 ||
        height < 1 || height > 4096 || count > 4096)
        return 0;
    const double sy = height / 160.0;
    const double sx = wide ? width / 240.0 : sy;
    const double offset = (width - 240 * sx) / 2;
    const Memory memory = {ewram, iwram};
    memset(out, 0, (size_t)width * height);
    for (unsigned i = 0; i < count; ++i) {
        const int32_t *q = commands + i * words;
        if (q[2] == 1 || q[2] == 3)
            polygon(out, width, height, q, memory, offset, sx, sy);
        else if (q[2] == 4)
            sprite(out, width, height, q, memory, offset, sx, sy, wide == 1);
    }
    return 1;
}

