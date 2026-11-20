/* Indexed-color mipmaps for the game's 256x256 terrain atlases. Keep index 0
 * transparent and return palette indices so fades still use the display palette. */
enum { atlas_side = 256, atlas_bytes = atlas_side * atlas_side, mip_levels = 3 };
typedef struct {
    uint8_t source[atlas_bytes], *planes[4][4];
    unsigned built;
    int checked, valid;
} MipAtlas;

static MipAtlas atlases[4];
static uint16_t mip_palette[256];
static uint8_t nearest_index[32768];

static unsigned nearest_color(unsigned r, unsigned g, unsigned b)
{
    unsigned key = r | g << 5 | b << 10;
    if (nearest_index[key]) return nearest_index[key];
    int best = 1, error = 1 << 20;
    for (int i = 1; i < 256; ++i) {
        const int c = mip_palette[i];
        const int dr = (c & 31) - (int)r, dg = (c >> 5 & 31) - (int)g;
        const int db = (c >> 10 & 31) - (int)b;
        const int e = 2 * dr * dr + 3 * dg * dg + db * db;
        if (e < error) { best = i; error = e; }
        if (!error) break;
    }
    return nearest_index[key] = (uint8_t)best;
}

static void mip_begin(const uint16_t *palette)
{
    /* Entry zero changes with the sky and never contributes to a mip. */
    const int changed = memcmp(palette + 1, mip_palette + 1, 255 * sizeof(*palette)) != 0;
    if (changed) {
        memcpy(mip_palette, palette, sizeof(mip_palette));
        memset(nearest_index, 0, sizeof(nearest_index));
    }
    for (unsigned i = 0; i < 4; ++i) {
        atlases[i].checked = 0;
        if (changed) atlases[i].valid = 0;
    }
}

static MipAtlas *mip_atlas(unsigned address, const uint8_t *ewram)
{
    if (address < 0x02000000 || address >= 0x02040000 || (address & 65535)) return NULL;
    const unsigned offset = address - 0x02000000;
    MipAtlas *a = &atlases[offset / atlas_bytes];
    if (a->checked) return a;
    a->checked = 1;
    const uint8_t *src = ewram + offset;
    if (a->valid && !memcmp(src, a->source, atlas_bytes)) return a;
    memcpy(a->source, src, atlas_bytes);
    a->built = 0;
    a->valid = 1;
    return a;
}

/* Each axis can shrink independently, e.g. a bank seen edge-on. The 16
 * possible planes are built on demand and retained until the atlas changes. */
static const uint8_t *mip_plane(MipAtlas *atlas, unsigned lu, unsigned lv)
{
    if (!lu && !lv) return atlas->source;
    const unsigned flag = 1u << (4 * lu + lv);
    uint8_t **plane = &atlas->planes[lu][lv];
    if (atlas->built & flag) return *plane;
    const unsigned rows = atlas_side >> lu, cols = atlas_side >> lv;
    if (!*plane) *plane = malloc(rows * cols);
    if (!*plane) return NULL;
    for (unsigned u = 0; u < rows; ++u) {
        for (unsigned v = 0; v < cols; ++v) {
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (unsigned i = 0; i < (1u << lu); ++i) {
                const uint8_t *row = atlas->source + ((u << lu) + i) * atlas_side + (v << lv);
                for (unsigned j = 0; j < (1u << lv); ++j) {
                    const unsigned idx = row[j];
                    if (!idx) continue;
                    const unsigned c = mip_palette[idx];
                    r += c & 31; g += c >> 5 & 31; b += c >> 10 & 31; ++n;
                }
            }
            (*plane)[u * cols + v] = n ? nearest_color((r + n/2)/n, (g + n/2)/n, (b + n/2)/n) : 0;
        }
    }
    atlas->built |= flag;
    return *plane;
}

typedef struct {
    const uint8_t *pixels;
    unsigned lu, lv;
} MipView;

typedef struct {
    MipView low, high;
    unsigned weight;
} MipSpan;

static int mip_span(MipSpan *span, unsigned address, const uint8_t *ewram,
                    double du, double dv)
{
    const double major = fmax(du, dv);
    if (major <= 1.0625) return 0;
    MipAtlas *atlas = mip_atlas(address, ewram);
    if (!atlas) return 0;
    const unsigned level = major >= 4 ? 2 : (major >= 2 ? 1 : 0);
    const double minor = fmin(du, dv);
    const unsigned offset = major >= 5 * minor ? 3 : major >= 2.5 * minor ? 2 : major >= 1.5 * minor ? 1 : 0;
    MipView *views[] = {&span->low, &span->high};
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned large = level + i, small = large > offset ? large - offset : 0;
        MipView *view = views[i];
        view->lu = du >= dv ? large : small;
        view->lv = dv > du ? large : small;
        view->pixels = mip_plane(atlas, view->lu, view->lv);
        if (!view->pixels) return 0;
    }
    span->weight = (unsigned)(fmin(1, major / (1u << level) - 1) * 8 + 0.5);
    return 1;
}

static inline unsigned mip_texel(MipView view, unsigned u, unsigned v)
{
    return view.pixels[((u >> view.lu) << (8 - view.lv)) + (v >> view.lv)];
}

static inline unsigned mip_blend(unsigned a, unsigned b, unsigned weight)
{
    if (a == b || !weight || !a || !b) return a;
    if (weight == 8) return b;
    unsigned c = mip_palette[a], d = mip_palette[b], w = 8 - weight;
    return nearest_color(((c & 31) * w + (d & 31) * weight + 4) >> 3,
                         (((c >> 5 & 31) * w + (d >> 5 & 31) * weight + 4) >> 3),
                         (((c >> 10 & 31) * w + (d >> 10 & 31) * weight + 4) >> 3));
}
