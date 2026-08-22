/*
 * SECD Machine for Microcontrollers
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "secd/primitives.h"
#include "secd/machine.h"
#include "hal/hal.h"
#include <stdlib.h>
#include <string.h>

/*
 * Primitive operations implementation.
 *
 * Primitives are C functions exposed to the SECD VM.
 * They receive arguments as a list and return a value.
 */

void secd_prim_init(secd_prim_registry_t *registry) {
    if (!registry) return;
    
    registry->count = 0;
    memset(registry->entries, 0, sizeof(registry->entries));
}

int secd_register_prim(secd_prim_registry_t *registry, const char *name, secd_prim_fn fn) {
    if (!registry || !name || !fn) return -1;
    
    if (registry->count >= SECD_PRIMITIVES_MAX) {
        return -1; /* Registry full */
    }
    
    secd_prim_entry_t *entry = &registry->entries[registry->count];
    entry->name = name;
    entry->fn = fn;
    entry->id = registry->count;
    
    registry->count++;
    
    return 0;
}

int secd_register_prim_at(secd_prim_registry_t *registry, const char *name,
                          secd_prim_fn fn, uint8_t id) {
    if (!registry || !name || !fn) return -1;

    if (id >= SECD_PRIMITIVES_MAX) {
        return -1; /* Fixed id out of range */
    }

    secd_prim_entry_t *entry = &registry->entries[id];
    entry->name = name;
    entry->fn = fn;
    entry->id = id;

    /* Advance the high-water mark so id-based lookups for this slot
     * (and everything below it) remain valid. */
    if (registry->count <= id) {
        registry->count = (uint8_t)(id + 1);
    }

    return 0;
}

secd_prim_entry_t* secd_find_prim(secd_prim_registry_t *registry, const char *name) {
    if (!registry || !name) return NULL;
    
    for (uint8_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].name, name) == 0) {
            return &registry->entries[i];
        }
    }
    
    return NULL;
}

secd_prim_entry_t* secd_find_prim_by_id(secd_prim_registry_t *registry, uint8_t id) {
    if (!registry || id >= registry->count) return NULL;
    
    return &registry->entries[id];
}

secd_value_t secd_call_prim(secd_prim_registry_t *registry, uint8_t id, secd_heap_t *heap, secd_value_t args) {
    if (!registry || id >= registry->count) {
        return SECD_NIL;
    }
    
    secd_prim_entry_t *entry = &registry->entries[id];
    if (!entry->fn) {
        return SECD_NIL;
    }
    
    return entry->fn(heap, args);
}

/*
 * Built-in primitive implementations
 */

/* Helper: get first argument */
static secd_value_t get_arg1(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, args);
}

/* Helper: get second argument */
static secd_value_t get_arg2(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, secd_cdr(heap, args));
}

/* Helper: get third argument */
static secd_value_t get_arg3(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, secd_cdr(heap, secd_cdr(heap, args)));
}

/* Helper: get fourth argument */
static secd_value_t get_arg4(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, secd_cdr(heap, secd_cdr(heap, secd_cdr(heap, args))));
}

/* List operations */
secd_value_t prim_car(secd_heap_t *heap, secd_value_t args) {
    secd_value_t pair = get_arg1(heap, args);
    return secd_car(heap, pair);
}

secd_value_t prim_cdr(secd_heap_t *heap, secd_value_t args) {
    secd_value_t pair = get_arg1(heap, args);
    return secd_cdr(heap, pair);
}

secd_value_t prim_cons(secd_heap_t *heap, secd_value_t args) {
    secd_value_t car = get_arg1(heap, args);
    secd_value_t cdr = get_arg2(heap, args);
    return secd_cons(heap, car, cdr);
}

secd_value_t prim_list(secd_heap_t *heap, secd_value_t args) {
    (void)heap; /* Unused parameter */
    /* list is just args itself */
    return args;
}

/* Arithmetic operations */
secd_value_t prim_add(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a + b);
}

secd_value_t prim_sub(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a - b);
}

secd_value_t prim_mul(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a * b);
}

secd_value_t prim_div(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    if (b == 0) {
        return SECD_NIL; /* Error: division by zero */
    }
    return secd_make_fixnum(a / b);
}

secd_value_t prim_mod(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    if (b == 0) {
        return SECD_NIL; /* Error: division by zero */
    }
    return secd_make_fixnum(a % b);
}

/* Comparison operations */
secd_value_t prim_eq(secd_heap_t *heap, secd_value_t args) {
    secd_value_t a = get_arg1(heap, args);
    secd_value_t b = get_arg2(heap, args);
    return secd_make_bool(a == b);
}

secd_value_t prim_lt(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_bool(a < b);
}

secd_value_t prim_gt(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_bool(a > b);
}

/* Type checking */
secd_value_t prim_null(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    return secd_make_bool(secd_is_nil(val));
}

secd_value_t prim_pair(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    return secd_make_bool(secd_is_pair(val));
}

secd_value_t prim_atom(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    /* atom is true if not a pair and not nil */
    return secd_make_bool(!secd_is_pair(val) && !secd_is_nil(val));
}

/*
 * HAL primitives (board features).
 * Registration order MUST match the "primitives" table in the board
 * metadata so ids line up.
 */

/* Board feature gates (default off unless the build system enables them) */
#ifndef SECD_FEATURE_GPIO
#define SECD_FEATURE_GPIO 0
#endif
#ifndef SECD_FEATURE_UART
#define SECD_FEATURE_UART 0
#endif
#ifndef SECD_FEATURE_HID
#define SECD_FEATURE_HID 0
#endif
#ifndef SECD_FEATURE_I2C
#define SECD_FEATURE_I2C 0
#endif

/* Max segments a single wave-play call can drive (durations in 100ns ticks). */
#define SECD_WAVE_MAX_SEGMENTS 512


/*
 * ---------------------------------------------------------------------
 * Arbitrary-precision integers ("%bn-*" primitives).
 *
 * Representation: SECD_TYPE_BIGNUM heap object { car: sign (raw 0/1),
 * cdr: bytevec descriptor slot } whose byte-vector holds the magnitude
 * little-endian, base 256, normalized (no leading zero high bytes; zero
 * has length 0 and sign 0). Arithmetic is classic schoolbook over the
 * byte limbs with uint32 accumulators; division is Knuth algorithm D
 * base-256. Results are freshly allocated; operands are never mutated.
 */

typedef struct {
    const uint8_t *d;
    uint16_t n;
    int sign;          /* 0 = positive, 1 = negative */
} bn_view;

static bool bn_get(secd_heap_t *heap, secd_value_t v, bn_view *out) {
    if (secd_get_type(v) != SECD_TYPE_BIGNUM) return false;
    secd_object_t *obj = secd_heap_get(heap, secd_get_index(v));
    if (!obj) return false;
    out->sign = (int)(obj->car & 1u);
    uint16_t slot = obj->cdr & SECD_INDEX_MASK;
    secd_bytevec_t *bv = secd_bytevec_get(heap, slot);
    if (!bv) return false;
    out->d = bv->data;
    out->n = bv->len;
    return true;
}

/* Allocate a BIGNUM from raw magnitude bytes; strips leading zeros and
 * canonicalizes the sign of zero. Returns SECD_NIL on allocation failure
 * or when the operand was not a bignum (bad = true). */
static bool bn_is_zero(const bn_view *v) {
    for (uint16_t i = 0; i < v->n; i++) if (v->d[i]) return false;
    return true;
}

static secd_value_t bn_make(secd_heap_t *heap, const uint8_t *d, uint16_t n, int sign, bool *bad) {
    while (n > 0 && d[n - 1] == 0) n--;
    static const uint8_t zero_byte = 0;
    if (n == 0) { d = &zero_byte; n = 1; sign = 0; }   /* canonical zero */
    uint16_t slot = secd_bytevec_alloc(heap, n);
    if (slot == SECD_BYTEVEC_INVALID) { *bad = true; return SECD_NIL; }
    for (uint16_t i = 0; i < n; i++) secd_bytevec_write(heap, slot, i, d[i]);
    uint16_t index = secd_heap_alloc(heap, SECD_TYPE_BIGNUM);
    if (index == 0) { *bad = true; return SECD_NIL; }
    secd_object_t *obj = secd_heap_get(heap, index);
    obj->car = (secd_value_t)(sign & 1u);
    obj->cdr = (secd_value_t)slot;
    return secd_make_handle(SECD_TYPE_BIGNUM, index);
}

static int bn_cmp_mag(const bn_view *a, const bn_view *b) {
    uint16_t an = a->n, bn2 = b->n;
    (void)an; (void)bn2;
    if (bn_is_zero(a)) return bn_is_zero(b) ? 0 : -1;
    if (bn_is_zero(b)) return 1;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (uint16_t i = a->n; i-- > 0;) {
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    }
    return 0;
}

static int bn_cmp(secd_heap_t *heap, secd_value_t av, secd_value_t bv, bool *bad) {
    bn_view a, b;
    if (!bn_get(heap, av, &a) || !bn_get(heap, bv, &b)) { *bad = true; return 0; }
    if (a.sign != b.sign) return a.sign ? -1 : 1;
    int m = bn_cmp_mag(&a, &b);
    return a.sign ? -m : m;
}

/* out = a + b (magnitudes); returns carry-out length via *out_n */
static void bn_add_mag(const uint8_t *a, uint16_t an, const uint8_t *b, uint16_t bn_,
                       uint8_t *out, uint16_t *out_n) {
    uint32_t carry = 0;
    uint16_t i = 0;
    for (; i < an || i < bn_; i++) {
        uint32_t s = carry;
        if (i < an) s += a[i];
        if (i < bn_) s += b[i];
        out[i] = (uint8_t)(s & 0xFFu);
        carry = s >> 8;
    }
    if (carry) out[i++] = (uint8_t)carry;
    *out_n = i;
}

/* out = a - b (magnitudes, requires a >= b) */
static void bn_sub_mag(const uint8_t *a, uint16_t an, const uint8_t *b, uint16_t bn_,
                       uint8_t *out, uint16_t *out_n) {
    int32_t borrow = 0;
    uint16_t i = 0;
    for (; i < an; i++) {
        int32_t s = (int32_t)a[i] - borrow - (i < bn_ ? b[i] : 0);
        borrow = s < 0;
        out[i] = (uint8_t)(s & 0xFFu);
    }
    *out_n = an;
}

static secd_value_t bn_addsub(secd_heap_t *heap, secd_value_t av, secd_value_t bv,
                              bool subtract, bool *bad) {
    bn_view a, b;
    if (!bn_get(heap, av, &a) || !bn_get(heap, bv, &b)) { *bad = true; return SECD_NIL; }
    if (subtract) b.sign ^= 1;

    int sign;
    uint16_t n = (a.n > b.n ? a.n : b.n) + 1;
    uint8_t *tmp = (uint8_t *)malloc(n);
    if (!tmp) { *bad = true; return SECD_NIL; }

    if (a.sign == b.sign) {
        bn_add_mag(a.d, a.n, b.d, b.n, tmp, &n);
        sign = a.sign;
    } else {
        int m = bn_cmp_mag(&a, &b);
        if (m == 0) {
            /* canonical zero; tmp is released by the tail free below */
            return bn_make(heap, tmp, 0, 0, bad);
        }
        if (m > 0) { bn_sub_mag(a.d, a.n, b.d, b.n, tmp, &n); sign = a.sign; }
        else       { bn_sub_mag(b.d, b.n, a.d, a.n, tmp, &n); sign = b.sign; }
    }
    secd_value_t r = bn_make(heap, tmp, n, sign, bad);
    free(tmp);
    return r;
}

static secd_value_t bn_mul(secd_heap_t *heap, secd_value_t av, secd_value_t bv, bool *bad) {
    bn_view a, b;
    if (!bn_get(heap, av, &a) || !bn_get(heap, bv, &b)) { *bad = true; return SECD_NIL; }
    if (bn_is_zero(&a) || bn_is_zero(&b)) return bn_make(heap, NULL, 0, 0, bad);

    uint16_t n = (uint16_t)(a.n + b.n);
    uint8_t *acc = (uint8_t *)calloc(n, 1);
    if (!acc) { *bad = true; return SECD_NIL; }
    for (uint16_t i = 0; i < a.n; i++) {
        uint32_t carry = 0;
        for (uint16_t j = 0; j < b.n; j++) {
            uint32_t cur = acc[i + j] + (uint32_t)a.d[i] * b.d[j] + carry;
            acc[i + j] = (uint8_t)(cur & 0xFFu);
            carry = cur >> 8;
        }
        uint16_t k = i + b.n;
        while (carry) { uint32_t cur = acc[k] + carry; acc[k] = cur & 0xFFu; carry >>= 8; k++; }
    }
    secd_value_t r = bn_make(heap, acc, n, a.sign ^ b.sign, bad);
    free(acc);
    return r;
}

/* Divide MAG (length N, little-endian) by DIVISOR (fits uint32);
 * quotient -> Q (length N, normalized), returns remainder. O(N). */
static uint32_t bn_divmod_u32(uint8_t *mag, uint16_t n, uint32_t divisor,
                              uint8_t *q, uint16_t *qn) {
    uint64_t rem = 0;
    for (uint16_t i = n; i-- > 0;) {
        uint64_t cur = (rem << 8) | mag[i];
        q[i] = (uint8_t)(cur / divisor);
        rem = cur % divisor;
    }
    *qn = n;
    while (*qn > 0 && q[*qn - 1] == 0) (*qn)--;
    return (uint32_t)rem;
}

/* General division, Knuth algorithm D on base-256 limbs.
 * A (AN bytes) divided by B (BN bytes); both inputs are little-endian,
 * B normalized (top byte nonzero). Q gets AN-BN+1 bytes, R gets BN.
 * Requires scratch space internally; inputs not modified. */
static void bn_knuth_d(const uint8_t *a_in, uint16_t an,
                       const uint8_t *b_in, uint16_t bn,
                       uint8_t *Q, uint16_t *qn,
                       uint8_t *R, uint16_t *rn) {
    /* Normalize so the divisor's top byte has its MSB set. */
    unsigned s = 0;
    unsigned tb = b_in[bn - 1], lz = 0;
    while (!(tb & 0x80u)) { tb <<= 1; lz++; }
    s = lz;

    uint16_t un = an + 1;
    uint8_t *u = (uint8_t *)calloc(un, 1);          /* dividend workspace */
    uint8_t *v = (uint8_t *)malloc(bn);             /* normalized divisor */
    memcpy(u, a_in, an);
    memcpy(v, b_in, bn);

    /* u <<= s ; v <<= s (v keeps length bn: top byte nonzero pre-shift) */
    unsigned carry = 0;
    for (uint16_t i = 0; i < an; i++) {
        unsigned wv = ((unsigned)u[i] << s) | carry;
        u[i] = (uint8_t)(wv & 0xFFu);
        carry = wv >> 8;
    }
    u[an] = (uint8_t)carry;
    if (s) {
        unsigned vc = 0;
        for (uint16_t i = 0; i < bn; i++) {
            unsigned wv = ((unsigned)v[i] << s) | vc;
            v[i] = (uint8_t)(wv & 0xFFu);
            vc = wv >> 8;
        }
        /* vc must be 0 here since top byte << s still fits (s = its leading zeros) */
    }

    uint16_t m = an - bn;
    for (uint16_t jj = m + 1; jj-- > 0;) {
        uint32_t qhat = ((uint32_t)u[jj + bn] << 8) | u[jj + bn - 1];
        uint32_t rhat = qhat % v[bn - 1];
        qhat /= v[bn - 1];
        if (qhat > 0xFFFFu) qhat = 0xFFFFu;         /* cannot happen (base 256), safety */
        while (bn > 1 && (uint64_t)qhat * v[bn - 2] >
                             (((uint64_t)qhat << 8) + rhat)) {
            qhat--;
            rhat += v[bn - 1];
            if (rhat >= 256u) break;
        }

        /* u[jj..jj+bn] -= qhat * v */
        uint32_t pc = 0;                            /* product carry */
        int32_t sub_borrow = 0;
        for (uint16_t i = 0; i < bn; i++) {
            pc += (uint32_t)qhat * v[i];
            int32_t t = (int32_t)u[i + jj] - sub_borrow - (int32_t)(pc & 0xFFu);
            u[i + jj] = (uint8_t)(t & 0xFFu);
            sub_borrow = (t < 0) ? 1 : 0;
            pc >>= 8;
        }
        int32_t t = (int32_t)u[jj + bn] - sub_borrow - (int32_t)pc;
        u[jj + bn] = (uint8_t)(t & 0xFFu);

        if (t < 0) {
            /* qhat one too large: add one multiple of v back */
            uint32_t ac = 0;
            for (uint16_t i = 0; i < bn; i++) {
                uint32_t sum = (uint32_t)u[i + jj] + v[i] + ac;
                u[i + jj] = (uint8_t)(sum & 0xFFu);
                ac = sum >> 8;
            }
            u[jj + bn] = (uint8_t)((u[jj + bn] + ac) & 0xFFu);
            Q[jj] = (uint8_t)qhat;
        } else {
            Q[jj] = (uint8_t)qhat;
        }
    }

    /* Denormalize remainder R = (u mod base^bn) >> s */
    if (s) {
        unsigned rc = 0;
        for (uint16_t i = bn; i-- > 0;) {
            unsigned wv = ((unsigned)rc << 8) | u[i];
            R[i] = (uint8_t)((wv >> s) & 0xFFu);
            rc = wv & ((1u << s) - 1u);
        }
    } else {
        memcpy(R, u, bn);
    }
    *rn = bn;
    while (*rn > 0 && R[*rn - 1] == 0) (*rn)--;
    *qn = an - bn + 1;
    while (*qn > 0 && Q[*qn - 1] == 0) (*qn)--;
    free(u);
    free(v);
}

/* Multiply a little-endian magnitude by SMALL (uint32); OUT sized N+4. */
static uint16_t bn_mul_u32(const uint8_t *d, uint16_t n, uint32_t m, uint8_t *out) {
    uint64_t carry = 0;
    uint16_t i = 0;
    for (; i < n; i++) {
        uint64_t cur = (uint64_t)d[i] * m + carry;
        out[i] = (uint8_t)(cur & 0xFFu);
        carry = cur >> 8;
    }
    while (carry) { out[i++] = (uint8_t)(carry & 0xFFu); carry >>= 8; }
    return i;
}

/* Add a small value to a little-endian magnitude in place. */
static void bn_add_u32_inplace(uint8_t *d, uint16_t *n, uint32_t v) {
    uint64_t carry = v;
    uint16_t i = 0;
    while (carry) {
        uint64_t cur = d[i] + (carry & 0xFFu);
        d[i] = (uint8_t)(cur & 0xFFu);
        carry = (carry >> 8) + (cur >> 8);
        i++;
    }
    if (i > *n) *n = i;
}

secd_value_t prim_bn_add(secd_heap_t *heap, secd_value_t args) {
    bool bad = false;
    secd_value_t r = bn_addsub(heap, get_arg1(heap, args), get_arg2(heap, args), false, &bad);
    return bad ? SECD_NIL : r;
}

secd_value_t prim_bn_sub(secd_heap_t *heap, secd_value_t args) {
    bool bad = false;
    secd_value_t r = bn_addsub(heap, get_arg1(heap, args), get_arg2(heap, args), true, &bad);
    return bad ? SECD_NIL : r;
}

secd_value_t prim_bn_mul(secd_heap_t *heap, secd_value_t args) {
    bool bad = false;
    secd_value_t r = bn_mul(heap, get_arg1(heap, args), get_arg2(heap, args), &bad);
    return bad ? SECD_NIL : r;
}

/* Wrap raw magnitude bytes into a fresh BIGNUM value. */
static secd_value_t bn_wrap(secd_heap_t *heap, const uint8_t *d, uint16_t n, int sign) {
    bool bad = false;
    return bn_make(heap, d, n, sign, &bad);
}

secd_value_t prim_bn_div(secd_heap_t *heap, secd_value_t args) {
    bn_view a, b;
    if (!bn_get(heap, get_arg1(heap, args), &a) ||
        !bn_get(heap, get_arg2(heap, args), &b) || bn_is_zero(&b)) {
        return SECD_NIL;
    }
    if (bn_cmp_mag(&a, &b) < 0) return bn_wrap(heap, NULL, 0, 0);
    uint16_t qn = (uint16_t)(a.n - b.n + 1), rn = b.n;
    uint8_t *q = (uint8_t *)calloc(qn ? qn : 1, 1);
    uint8_t *r = (uint8_t *)malloc(rn ? rn : 1);
    if (!q || !r) { free(q); free(r); return SECD_NIL; }
    bn_knuth_d(a.d, a.n, b.d, b.n, q, &qn, r, &rn);
    secd_value_t res = bn_wrap(heap, q, qn, a.sign ^ b.sign);
    free(q); free(r);
    return res;
}

secd_value_t prim_bn_mod(secd_heap_t *heap, secd_value_t args) {
    bn_view a, b;
    if (!bn_get(heap, get_arg1(heap, args), &a) ||
        !bn_get(heap, get_arg2(heap, args), &b) || bn_is_zero(&b)) {
        return SECD_NIL;
    }
    if (bn_cmp_mag(&a, &b) < 0) return bn_wrap(heap, a.d, a.n, a.sign);
    uint16_t qn = (uint16_t)(a.n - b.n + 1), rn = b.n;
    uint8_t *q = (uint8_t *)calloc(qn ? qn : 1, 1);
    uint8_t *r = (uint8_t *)malloc(rn ? rn : 1);
    if (!q || !r) { free(q); free(r); return SECD_NIL; }
    bn_knuth_d(a.d, a.n, b.d, b.n, q, &qn, r, &rn);
    secd_value_t res = bn_wrap(heap, r, rn, a.sign);
    free(q); free(r);
    return res;
}

secd_value_t prim_bn_cmp(secd_heap_t *heap, secd_value_t args) {
    bool bad = false;
    int c = bn_cmp(heap, get_arg1(heap, args), get_arg2(heap, args), &bad);
    return bad ? SECD_NIL : secd_make_fixnum((int16_t)c);
}

/* %bn-to-string: decimal ASCII (leading '-' when negative), returned as a
 * string byte-vector. Converts by repeatedly dividing the working copy by
 * 10^9 (u32 path), peeling 9 digits per pass: O(len^2 / 9). */
secd_value_t prim_bn_to_string(secd_heap_t *heap, secd_value_t args) {
    bn_view a;
    if (!bn_get(heap, get_arg1(heap, args), &a)) return SECD_NIL;

    uint16_t cap = (uint16_t)(a.n > 0 ? (uint32_t)a.n * 3u + 12u : 2u);
    char *digits = (char *)malloc(cap);              /* emitted LSB-group first */
    if (!digits) return SECD_NIL;
    uint16_t ndigits = 0;

    if (a.n == 0) {
        digits[ndigits++] = '0';
    } else {
        uint8_t *work = (uint8_t *)malloc(a.n);
        if (!work) { free(digits); return SECD_NIL; }
        memcpy(work, a.d, a.n);
        uint16_t wn = a.n;
        bool first_group = true;
        while (wn > 0) {
            uint8_t *q = (uint8_t *)malloc(wn);
            if (!q) { free(work); free(digits); return SECD_NIL; }
            uint16_t qn;
            uint32_t rem = bn_divmod_u32(work, wn, 1000000000u, q, &qn);
            /* q holds the quotient (normalized length qn <= wn) */
            for (int i = 8; i >= 0; i--) { digits[ndigits++] = (char)('0' + (rem % 10)); rem /= 10; }
            first_group = false;
            (void)first_group;
            memcpy(work, q, qn);
            wn = qn;
            free(q);
        }
        free(work);
        /* digits[] now holds groups least-significant-first, each padded to
         * 9; strip padding zeros from the very END (most significant group)
         * leaving at least one digit. */
        while (ndigits > 1 && digits[ndigits - 1] == '0') ndigits--;
        /* reverse into place */
        for (uint16_t i = 0; i < ndigits / 2; i++) {
            char t = digits[i]; digits[i] = digits[ndigits - 1 - i]; digits[ndigits - 1 - i] = t;
        }
    }

    uint16_t slot = secd_bytevec_alloc(heap, (uint16_t)(ndigits + (a.sign ? 1u : 0u)));
    if (slot == SECD_BYTEVEC_INVALID) { free(digits); return SECD_NIL; }
    uint16_t pos = 0;
    if (a.sign) secd_bytevec_write(heap, slot, pos++, '-');
    for (uint16_t i = 0; i < ndigits; i++) secd_bytevec_write(heap, slot, pos++, (uint8_t)digits[i]);
    free(digits);
    return secd_make_bytevec(slot);
}

/* %bn-from-string: parse an optional sign followed by decimal digits from a
 * string byte-vector. Returns SECD_NIL on empty/garbage input or OOM. */
secd_value_t prim_bn_from_string(secd_heap_t *heap, secd_value_t args) {
    secd_value_t sv = get_arg1(heap, args);
    if (!secd_is_bytevec(sv)) return SECD_NIL;
    secd_bytevec_t *bv = secd_bytevec_get(heap, secd_get_index(sv));
    if (!bv || bv->len == 0) return SECD_NIL;

    uint16_t i = 0;
    int sign = 0;
    if (bv->data[0] == '-') { sign = 1; i++; }
    else if (bv->data[0] == '+') { i++; }
    if (i >= bv->len) return SECD_NIL;

    uint16_t cap = (uint16_t)((bv->len + 1) / 2 + 4);
    uint8_t *acc = (uint8_t *)calloc(cap, 1);
    uint8_t *tmp = (uint8_t *)malloc((size_t)cap + 8);
    if (!acc || !tmp) { free(acc); free(tmp); return SECD_NIL; }
    uint16_t an = 0;                                  /* magnitude length */

    for (; i < bv->len; i++) {
        uint8_t ch = bv->data[i];
        if (ch < '0' || ch > '9') { free(acc); free(tmp); return SECD_NIL; }
        /* acc = acc * 10 + digit */
        uint16_t tn = bn_mul_u32(acc, an, 10, tmp);
        uint16_t need = (uint16_t)(tn + 1);
        if (need > cap) { free(acc); free(tmp); return SECD_NIL; }
        memcpy(acc, tmp, tn);
        an = tn;
        bn_add_u32_inplace(acc, &an, (uint32_t)(ch - '0'));
        while (an > 0 && acc[an - 1] == 0) an--;
    }
    free(tmp);

    bool bad = false;
    secd_value_t r = bn_make(heap, acc, an, sign, &bad);
    free(acc);
    return bad ? SECD_NIL : r;
}

/* Decode an integer argument that may be a 12-bit fixnum or a boxed wide
 * integer (BIGNUM, from the LDCW literal path) into uint32_t. */
static uint32_t get_arg_u32(secd_heap_t *heap, secd_value_t args, int n) {
    secd_value_t v = (n == 1) ? get_arg1(heap, args)
                   : (n == 2) ? get_arg2(heap, args)
                   : (n == 3) ? get_arg3(heap, args)
                              : get_arg4(heap, args);
    return secd_integer_value(heap, v);
}

#if SECD_FEATURE_GPIO
secd_value_t prim_gpio_init(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t mode = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(hal_gpio_init(pin, mode));
}

secd_value_t prim_gpio_write(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t value = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(hal_gpio_write(pin, value));
}

secd_value_t prim_gpio_read(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    return secd_make_fixnum(hal_gpio_read(pin));
}
#endif

#if SECD_FEATURE_UART
secd_value_t prim_uart_init(secd_heap_t *heap, secd_value_t args) {
    uint32_t baud = get_arg_u32(heap, args, 1);
    hal_serial_init(baud);
    return SECD_NIL;
}

secd_value_t prim_uart_write(secd_heap_t *heap, secd_value_t args) {
    uint8_t byte = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    hal_serial_write(byte);
    return SECD_NIL;
}

secd_value_t prim_uart_read(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(hal_serial_read());
}
#endif

#if SECD_FEATURE_I2C
secd_value_t prim_i2c_init(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    uint8_t sda = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t scl = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    /* Frequency is in kHz (fits the 12-bit fixnum; 100000 Hz would wrap).
     * The argument may also be a boxed wide integer. */
    uint32_t hz = get_arg_u32(heap, args, 3) * 1000u;
    return secd_make_fixnum((int16_t)hal_i2c_init(sda, scl, hz));
}

/* %i2c-write bus addr (reg bytes...): write up to 32 bytes to the 7-bit
 * `addr` on bus `bus`. Returns bytes sent, or -1 on NACK/timeout/bad bus. */
secd_value_t prim_i2c_write(secd_heap_t *heap, secd_value_t args) {
    uint8_t bus = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t addr = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    secd_value_t node = get_arg3(heap, args);
    uint8_t buf[32];
    int n = 0;
    while (secd_is_pair(node) && n < 32) {
        buf[n++] = (uint8_t)secd_fixnum_value(secd_car(heap, node));
        node = secd_cdr(heap, node);
    }
    return secd_make_fixnum((int16_t)hal_i2c_write(bus, addr, buf, (size_t)n));
}

/* %i2c-write-v bus addr vec: transmit the whole byte-vector verbatim in one
 * transaction. Unlike %i2c-write (list, capped at 32 bytes) this is an
 * unbounded vector upload; the register byte rides as element 0, which suits
 * the BMI270 block-config upload. The byte-vector may be a ROM pool literal
 * (it lives in the malloc'd bytecode buffer, i.e. ordinary RAM) or a RAM
 * make-vector; the bus runs in polling mode, so transmitting straight from
 * the vector's backing store is safe. */
secd_value_t prim_i2c_write_vector(secd_heap_t *heap, secd_value_t args) {
    uint8_t bus = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t addr = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    secd_value_t vec = get_arg3(heap, args);
    if (!secd_is_bytevec(vec)) return SECD_NIL;
    secd_bytevec_t *v = secd_bytevec_get(heap, secd_get_index(vec));
    if (!v) return SECD_NIL;
    int rc = (int)hal_i2c_write(bus, addr, v->data, (size_t)v->len);
    return secd_make_fixnum((int16_t)rc);
}

secd_value_t prim_i2c_read(secd_heap_t *heap, secd_value_t args) {
    uint8_t bus = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t addr = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    int16_t count = secd_fixnum_value(get_arg3(heap, args));
    if (count < 1) count = 1;
    if (count > 32) count = 32;
    uint8_t buf[32];
    int got = hal_i2c_read(bus, addr, buf, (size_t)count);
    if (got < 0) { memset(buf, 0, sizeof(buf)); got = count; }
    uint16_t slot = secd_bytevec_alloc(heap, (uint16_t)got);
    if (slot == SECD_BYTEVEC_INVALID) return SECD_NIL;
    for (int i = 0; i < got; i++) {
        secd_bytevec_write(heap, slot, (uint16_t)i, buf[i]);
    }
    return secd_make_bytevec(slot);
}

secd_value_t prim_i2c_write_read(secd_heap_t *heap, secd_value_t args) {
    uint8_t bus = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t addr = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    secd_value_t node = get_arg3(heap, args);
    uint8_t wbuf[32];
    int wn = 0;
    while (secd_is_pair(node) && wn < 32) {
        wbuf[wn++] = (uint8_t)secd_fixnum_value(secd_car(heap, node));
        node = secd_cdr(heap, node);
    }
    int16_t count = secd_fixnum_value(get_arg4(heap, args));
    if (count < 1) count = 1;
    if (count > 32) count = 32;
    uint8_t rbuf[32];
    int got = hal_i2c_write_read(bus, addr, wbuf, (size_t)wn, rbuf, (size_t)count);
    if (got < 0) { memset(rbuf, 0, sizeof(rbuf)); got = count; }
    uint16_t slot = secd_bytevec_alloc(heap, (uint16_t)got);
    if (slot == SECD_BYTEVEC_INVALID) return SECD_NIL;
    for (int i = 0; i < got; i++) {
        secd_bytevec_write(heap, slot, (uint16_t)i, rbuf[i]);
    }
    return secd_make_bytevec(slot);
}
#endif

#if SECD_FEATURE_HID
secd_value_t prim_usb_init(secd_heap_t *heap, secd_value_t args) {
    (void)heap; (void)args;
    hal_usb_init();
    return SECD_NIL;
}

secd_value_t prim_usb_start(secd_heap_t *heap, secd_value_t args) {
    (void)heap; (void)args;
    hal_usb_start();
    return SECD_NIL;
}

secd_value_t prim_usb_serial_add(secd_heap_t *heap, secd_value_t args) {
    (void)heap; (void)args;
    return secd_make_fixnum((int16_t)hal_usb_serial_add());
}

secd_value_t prim_usb_hid_add(secd_heap_t *heap, secd_value_t args) {
    (void)heap; (void)args;
    return secd_make_fixnum((int16_t)hal_usb_hid_add());
}

secd_value_t prim_usb_mouse_add(secd_heap_t *heap, secd_value_t args) {
    (void)heap; (void)args;
    return secd_make_fixnum((int16_t)hal_usb_mouse_add());
}

secd_value_t prim_hid_key(secd_heap_t *heap, secd_value_t args) {
    uint8_t modifier = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t usage = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    hal_hid_keyboard_tap(modifier, usage);
    return SECD_NIL;
}

secd_value_t prim_hid_mouse(secd_heap_t *heap, secd_value_t args) {
    uint8_t buttons = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    int8_t dx = (int8_t)secd_fixnum_value(get_arg2(heap, args));
    int8_t dy = (int8_t)secd_fixnum_value(get_arg3(heap, args));
    int8_t wheel = (int8_t)secd_fixnum_value(get_arg4(heap, args));
    int rc = hal_hid_mouse_send(dx, dy, buttons, wheel);
    return secd_make_fixnum((int16_t)rc);
}

/* Lisp-settable USB device identity. Strings arrive as byte-vectors (the
 * runtime's string type); we copy up to 31 bytes and NUL-terminate. Called
 * before %usb-start so the host sees the new values at enumeration. */
secd_value_t prim_usb_set_vid(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    hal_usb_set_vid((uint16_t)secd_fixnum_value(get_arg1(heap, args)));
    return SECD_NIL;
}

secd_value_t prim_usb_set_pid(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    hal_usb_set_pid((uint16_t)secd_fixnum_value(get_arg1(heap, args)));
    return SECD_NIL;
}

 secd_value_t prim_usb_set_mfr(secd_heap_t *heap, secd_value_t args) {
     secd_value_t v = get_arg1(heap, args);
     if (secd_is_bytevec(v)) {
         secd_bytevec_t *bv = secd_bytevec_get(heap, secd_get_index(v));
         if (bv) hal_usb_set_manufacturer(bv->data, bv->len);
     }
     return SECD_NIL;
 }

 secd_value_t prim_usb_set_product(secd_heap_t *heap, secd_value_t args) {
     secd_value_t v = get_arg1(heap, args);
     if (secd_is_bytevec(v)) {
         secd_bytevec_t *bv = secd_bytevec_get(heap, secd_get_index(v));
         if (bv) hal_usb_set_product(bv->data, bv->len);
     }
     return SECD_NIL;
 }

 secd_value_t prim_usb_set_serial(secd_heap_t *heap, secd_value_t args) {
     secd_value_t v = get_arg1(heap, args);
     if (secd_is_bytevec(v)) {
         secd_bytevec_t *bv = secd_bytevec_get(heap, secd_get_index(v));
         if (bv) hal_usb_set_serial(bv->data, bv->len);
     }
     return SECD_NIL;
 }

secd_value_t prim_serial_write(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    int port = (int)secd_fixnum_value(get_arg1(heap, args));
    uint8_t byte = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum((int16_t)hal_usb_serial_write(port, byte));
}

secd_value_t prim_serial_read(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    int port = (int)secd_fixnum_value(get_arg1(heap, args));
    return secd_make_fixnum((int16_t)hal_usb_serial_read(port));
}

secd_value_t prim_serial_avail(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    int port = (int)secd_fixnum_value(get_arg1(heap, args));
    return secd_make_fixnum((int16_t)hal_usb_serial_available(port));
}
#endif

secd_value_t prim_sleep(secd_heap_t *heap, secd_value_t args) {
    uint32_t ms = get_arg_u32(heap, args, 1);
    hal_sleep(ms);
    return SECD_NIL;
}

secd_value_t prim_millis(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum((int16_t)hal_millis());
}

secd_value_t prim_adc_read(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(0);
}

secd_value_t prim_pwm_write(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(0);
}

/*
 * wave-play: drive a pin through a precomputed waveform.
 *   (wave-play pin start-level (dur0 dur1 ... durN))
 * Durations are in 100ns ticks; the HAL flips the pin level after each one
 * (starting at start-level), so a WS2812 frame is just the alternating
 * high/low pulse durations followed by a reset (a long low segment).
 */
secd_value_t prim_wave_play(secd_heap_t *heap, secd_value_t args) {
    int16_t pin = secd_fixnum_value(get_arg1(heap, args));
    int16_t start_level = secd_fixnum_value(get_arg2(heap, args));
    secd_value_t node = get_arg3(heap, args);

    uint16_t duration_ns[SECD_WAVE_MAX_SEGMENTS];
    int count = 0;
    while (secd_is_pair(node) && count < SECD_WAVE_MAX_SEGMENTS) {
        int16_t ticks = secd_fixnum_value(secd_car(heap, node));
        duration_ns[count++] = (uint16_t)(ticks > 0 ? (uint16_t)ticks * 100u : 0u);
        node = secd_cdr(heap, node);
    }
    hal_wave_play((int)pin, (int)start_level, duration_ns, count);
    return SECD_NIL;
}


/*
 * UTF-16LE encoding / decoding primitives (utf16-enc / utf16-dec).
 *
 * Convert between SECD UTF-8 byte-vectors and UTF-16LE byte-vectors.
 * UTF-16LE is what USB string descriptors require. These are UNIVERSAL
 * runtime primitives: present in EVERY firmware image (S3, C3, rp2040, ...),
 * not HAL-specific. They are bound from Lisp via `def-c-fun`.
 */
static secd_value_t prim_utf16_enc(secd_heap_t *heap,
                                    secd_value_t args) {
    secd_value_t in = get_arg1(heap, args);
    if (!secd_is_bytevec(in)) {
        return SECD_NIL;
    }
    secd_bytevec_t *vin = secd_bytevec_get(heap, secd_get_index(in));
    if (!vin) return SECD_NIL;

    size_t in_len = vin->len;
    size_t units = 0;
    for (size_t i = 0; i < in_len;) {
        int c = vin->data[i];
        i++;
        if (c >= 0xF0) { i += 3; units += 2; }
        else if (c >= 0xE0) { i += 2; units += 1; }
        else if (c >= 0xC0) { i += 1; units += 1; }
        else { units += 1; }
    }

    uint16_t slot = secd_bytevec_alloc(heap, (uint16_t)(units * 2));
    if (slot == SECD_BYTEVEC_INVALID) return SECD_NIL;
    size_t o = 0;
    for (size_t i = 0; i < in_len;) {
        int c = vin->data[i];
        i++;
        if (c >= 0xF0) {
            int c1 = vin->data[i++];
            int c2 = vin->data[i++];
            int c3 = vin->data[i++];
            int cp = 0x10000 + ((c & 0x07) << 18) + ((c1 & 0x3F) << 12)
                     + ((c2 & 0x3F) << 6) + (c3 & 0x3F);
            int hi = 0xD800 + ((cp - 0x10000) >> 10);
            int lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(hi & 0xFF));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(hi >> 8));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(lo & 0xFF));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(lo >> 8));
        } else if (c >= 0xE0) {
            int c1 = vin->data[i++];
            int c2 = vin->data[i++];
            int cp = ((c & 0x0F) << 12) + ((c1 & 0x3F) << 6) + (c2 & 0x3F);
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(cp & 0xFF));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(cp >> 8));
        } else if (c >= 0xC0) {
            int c1 = vin->data[i++];
            int cp = ((c & 0x1F) << 6) + (c1 & 0x3F);
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(cp & 0xFF));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(cp >> 8));
        } else {
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(c & 0xFF));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)0);
        }
    }
    return secd_make_bytevec(slot);
}

static secd_value_t prim_utf16_dec(secd_heap_t *heap,
                                    secd_value_t args) {
    secd_value_t in = get_arg1(heap, args);
    if (!secd_is_bytevec(in)) {
        return SECD_NIL;
    }
    secd_bytevec_t *vin = secd_bytevec_get(heap, secd_get_index(in));
    if (!vin) return SECD_NIL;

    size_t in_len = vin->len;
    size_t units = in_len / 2;
    size_t bytes = 0;
    for (size_t i = 0; i < units;) {
        int u = vin->data[i * 2] | (vin->data[i * 2 + 1] << 8);
        i++;
        if (u >= 0xD800 && u <= 0xDBFF) { i += 1; bytes += 4; }
        else if (u >= 0x0800) { bytes += 3; }
        else if (u >= 0x0080) { bytes += 2; }
        else { bytes += 1; }
    }

    uint16_t slot = secd_bytevec_alloc(heap, (uint16_t)bytes);
    if (slot == SECD_BYTEVEC_INVALID) return SECD_NIL;
    size_t o = 0;
    for (size_t i = 0; i < units;) {
        int u = vin->data[i * 2] | (vin->data[i * 2 + 1] << 8);
        i++;
        if (u >= 0xD800 && u <= 0xDBFF) {
            int lo = vin->data[i * 2] | (vin->data[i * 2 + 1] << 8);
            i++;
            int cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0xF0 | (cp >> 18)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | (cp & 0x3F)));
        } else if (u >= 0x0800) {
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0xE0 | (u >> 12)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | ((u >> 6) & 0x3F)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | (u & 0x3F)));
        } else if (u >= 0x0080) {
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0xC0 | (u >> 6)));
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)(0x80 | (u & 0x3F)));
        } else {
            secd_bytevec_write(heap, slot, (uint16_t)(o++), (uint8_t)u);
        }
    }
    return secd_make_bytevec(slot);
}

/* Register all built-in primitives */
void secd_register_builtins(secd_prim_registry_t *registry) {
    if (!registry) return;
    
    /* List operations */
    secd_register_prim(registry, "car", prim_car);
    secd_register_prim(registry, "cdr", prim_cdr);
    secd_register_prim(registry, "cons", prim_cons);
    secd_register_prim(registry, "list", prim_list);
    
    /* Arithmetic */
    secd_register_prim(registry, "+", prim_add);
    secd_register_prim(registry, "-", prim_sub);
    secd_register_prim(registry, "*", prim_mul);
    secd_register_prim(registry, "/", prim_div);
    secd_register_prim(registry, "%", prim_mod);
    
    /* Comparison */
    secd_register_prim(registry, "=", prim_eq);
    secd_register_prim(registry, "<", prim_lt);
    secd_register_prim(registry, ">", prim_gt);
    
    /* Type checking */
    secd_register_prim(registry, "null?", prim_null);
    secd_register_prim(registry, "pair?", prim_pair);
    secd_register_prim(registry, "atom?", prim_atom);
    
    /* HAL primitives (board features) */
#if SECD_FEATURE_GPIO
    secd_register_prim(registry, "%gpio-init", prim_gpio_init);
    secd_register_prim(registry, "%gpio-write", prim_gpio_write);
    secd_register_prim(registry, "%gpio-read", prim_gpio_read);
#endif
#if SECD_FEATURE_UART
    secd_register_prim(registry, "%uart-init", prim_uart_init);
    secd_register_prim(registry, "%uart-write", prim_uart_write);
    secd_register_prim(registry, "%uart-read", prim_uart_read);
#endif
    secd_register_prim(registry, "%sleep", prim_sleep);
    secd_register_prim(registry, "%millis", prim_millis);
    secd_register_prim(registry, "%adc-read", prim_adc_read);
    secd_register_prim(registry, "%pwm-write", prim_pwm_write);
    secd_register_prim(registry, "%wave-play", prim_wave_play);
#if SECD_FEATURE_I2C
    /* Registered before USB so the I2C ids (26-29) are identical on every
       board that exposes them, regardless of whether the board also has USB
       peripheral primitives (ids 30+). */
    secd_register_prim(registry, "%i2c-init", prim_i2c_init);
    secd_register_prim(registry, "%i2c-write", prim_i2c_write);
    secd_register_prim(registry, "%i2c-read", prim_i2c_read);
    secd_register_prim(registry, "%i2c-write-read", prim_i2c_write_read);
#endif
#if SECD_FEATURE_HID
    /* Registered last so the base primitive table is unchanged on boards
       without USB (else every following id would shift). Lisp builds the
       interface set with the factory primitives then calls %usb-start to
       enumerate; the CDC console is always part of the device. */
    secd_register_prim(registry, "%hid-key", prim_hid_key);
    secd_register_prim(registry, "%usb-init", prim_usb_init);
    secd_register_prim(registry, "%usb-start", prim_usb_start);
    secd_register_prim(registry, "%usb-serial-add", prim_usb_serial_add);
    secd_register_prim(registry, "%usb-hid-add", prim_usb_hid_add);
    secd_register_prim(registry, "%serial-write", prim_serial_write);
    secd_register_prim(registry, "%serial-read", prim_serial_read);
    secd_register_prim(registry, "%serial-avail", prim_serial_avail);
#endif
    /* Registered after USB so the existing base/HID tables never shift.
       On an S3 this lands at id 38; on a C3 (no HID) at id 30. */
#if SECD_FEATURE_I2C
    secd_register_prim(registry, "%i2c-write-v", prim_i2c_write_vector);
#endif
    /* %hid-mouse rides the S3's second HID interface (EP6); on boards
       without HID the primitive is simply not registered, and anything
       after it (none today) would stay id-stable. */
#if SECD_FEATURE_HID
    secd_register_prim(registry, "%hid-mouse", prim_hid_mouse);
    /* Must be sorted by name for lookups and appears after %hid-mouse;
       ids on S3: %usb-mouse-add = 40, %hid-mouse = 39. */
    secd_register_prim(registry, "%usb-mouse-add", prim_usb_mouse_add);
    /* Lisp-settable USB device identity (VID/PID/strings), called before
     * %usb-start. Registered last so ids stay stable on every HID board. */
    secd_register_prim(registry, "%usb-vid", prim_usb_set_vid);
    secd_register_prim(registry, "%usb-pid", prim_usb_set_pid);
    secd_register_prim(registry, "%usb-manufacturer", prim_usb_set_mfr);
    secd_register_prim(registry, "%usb-product", prim_usb_set_product);
    secd_register_prim(registry, "%usb-serial", prim_usb_set_serial);

    /* Software (non-HAL) runtime helpers, present in every firmware build. */
#endif

    /* Arbitrary-precision integers (%bn-*): present in EVERY firmware.
       Registered unconditionally at the tail of the HAL table so ids follow
       whatever feature prims a given chip enables; see the per-chip target metadata
       for the per-chip numbering. */
    secd_register_prim(registry, "%bn-add", prim_bn_add);
    secd_register_prim(registry, "%bn-sub", prim_bn_sub);
    secd_register_prim(registry, "%bn-mul", prim_bn_mul);
    secd_register_prim(registry, "%bn-div", prim_bn_div);
    secd_register_prim(registry, "%bn-mod", prim_bn_mod);
    secd_register_prim(registry, "%bn-cmp", prim_bn_cmp);
    secd_register_prim(registry, "%bn-to-string", prim_bn_to_string);
    secd_register_prim(registry, "%bn-from-string", prim_bn_from_string);

    /* Universal software primitives — registered UNCONDITIONALLY so they
       exist in EVERY firmware image (S3, C3, rp2040, ...). Part of the
       portable runtime, not HAL. Bound from Lisp via `def-c-fun`.
       They use FIXED high ids (200/201) so the bytecode id is identical
       on every target; programs using them are portable. See
       targets/machine-runtime.json. */
    secd_register_prim_at(registry, "utf16-enc", prim_utf16_enc, 200);
    secd_register_prim_at(registry, "utf16-dec", prim_utf16_dec, 201);
}
