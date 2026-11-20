#include <stdint.h>

static inline uint32_t vr_ror(uint32_t value, unsigned amount)
{
    amount &= 31;
    return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

static inline uint32_t vr_shift(uint32_t value, unsigned amount, unsigned kind,
                                unsigned carry_in, unsigned *carry_out)
{
    unsigned carry = carry_in;
    if (amount) {
        switch (kind) {
        case 0:
            carry = amount <= 32 ? value >> (32 - amount) & 1u : 0;
            value = amount < 32 ? value << amount : 0;
            break;
        case 1:
            carry = amount <= 32 ? value >> (amount - 1) & 1u : 0;
            value = amount < 32 ? value >> amount : 0;
            break;
        case 2: {
            unsigned shift = amount < 32 ? amount : 31;
            carry = value >> (amount <= 32 ? amount - 1 : 31) & 1u;
            value = (uint32_t)((int32_t)value >> shift);
            break;
        }
        case 3:
            value = vr_ror(value, amount);
            carry = value >> 31;
            break;
        }
    }
    if (carry_out)
        *carry_out = carry;
    return value;
}

static inline uint32_t vr_write(const RtApi *a, void *m, uint32_t *c, unsigned dst,
                                uint32_t value, uint32_t next, unsigned ticks)
{
    if (dst == 15)
        next = value & ~3u;
    else
        c[dst] = value;
    c[15] = next;
    a->tick(m, ticks);
    return next;
}

static inline uint32_t vr_load_pc(const RtApi *a, void *m, uint32_t *c, uint32_t addr, unsigned ticks)
{
    return vr_write(a, m, c, 15, vr_ror(a->read32(m, addr & ~3u), (addr & 3u) * 8), 0, ticks);
}

static inline uint32_t vr_thumb_shift(const RtApi *a, void *m, uint32_t *c,
                                      unsigned dst, unsigned amount, unsigned kind,
                                      uint32_t next, unsigned ticks)
{
    unsigned carry;
    const uint32_t value = vr_shift(c[dst], amount, kind, c[16] >> 29 & 1u, &carry);
    c[16] = (c[16] & 0x1fffffffu) | (value & 0x80000000u)
          | (value == 0 ? 0x40000000u : 0) | (carry << 29);
    c[dst] = value;
    c[15] = next;
    a->tick(m, ticks);
    return next | 1u;
}



static inline uint32_t vr_thumb_pc(const RtApi *a, void *m, uint32_t *c,
                                   uint32_t value, unsigned ticks)
{
    c[15] = value & ~1u;
    a->tick(m, ticks);
    return c[15] | 1u;
}

static inline unsigned vr_arm_cond(uint32_t status, unsigned condition)
{
    const unsigned n = status >> 31, z = status >> 30 & 1u;
    const unsigned carry = status >> 29 & 1u, v = status >> 28 & 1u;
    unsigned passed;
    switch (condition >> 1) {
    case 0: passed = z; break;
    case 1: passed = carry; break;
    case 2: passed = n; break;
    case 3: passed = v; break;
    case 4: passed = carry && !z; break;
    case 5: passed = n == v; break;
    case 6: passed = !z && n == v; break;
    default: passed = 1; break;
    }
    return passed ^ (condition & 1u);
}

static inline uint32_t vr_bx(const RtApi *a, void *m, uint32_t *c, uint32_t target,
                             unsigned cond, uint32_t next, unsigned ticks)
{
    if (vr_arm_cond(c[16], cond)) {
        c[16] = (c[16] & ~32u) | ((target & 1u) << 5);
        next = target & ((target & 1u) ? ~1u : ~3u);
    }
    c[15] = next;
    a->tick(m, ticks);
    return next | ((c[16] >> 5) & 1u);
}
