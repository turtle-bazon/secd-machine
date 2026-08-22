// Host-side bignum engine test: links the real core sources.
#include "secd/heap.h"
#include "secd/machine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// expose internals from primitives.cpp
#include "secd/primitives.h"

// primitives.cpp compiles as C++; these have C++ linkage
secd_value_t prim_bn_from_string(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_to_string(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_add(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_sub(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_mul(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_div(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_mod(secd_heap_t*, secd_value_t);
secd_value_t prim_bn_cmp(secd_heap_t*, secd_value_t);

static secd_heap_t H;
static int failures = 0;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL(line %d): %s\n", __LINE__, msg); failures++; } } while(0)

// build bignum from decimal string using the prim
static secd_value_t bn_from_str(const char *s) {
    uint16_t slot = secd_bytevec_alloc(&H, strlen(s));
    for (size_t i = 0; i < strlen(s); i++) secd_bytevec_write(&H, slot, i, s[i]);
    secd_value_t sv = secd_make_bytevec(slot);
    secd_value_t args = secd_cons(&H, sv, SECD_NIL);
    return prim_bn_from_string(&H, args);
}
static char *bn_to_str(secd_value_t v) {
    secd_value_t args = secd_cons(&H, v, SECD_NIL);
    secd_value_t sv = prim_bn_to_string(&H, args);
    secd_bytevec_t *bv = secd_bytevec_get(&H, secd_get_index(sv));
    static char buf[80000];
    memcpy(buf, bv->data, bv->len); buf[bv->len] = 0;
    return buf;
}
static secd_value_t bn_op(const char *op, secd_value_t a, secd_value_t b) {
    secd_value_t args = secd_cons(&H, a, secd_cons(&H, b, SECD_NIL));
    if (!strcmp(op,"add")) return prim_bn_add(&H, args);
    if (!strcmp(op,"sub")) return prim_bn_sub(&H, args);
    if (!strcmp(op,"mul")) return prim_bn_mul(&H, args);
    if (!strcmp(op,"div")) return prim_bn_div(&H, args);
    return prim_bn_mod(&H, args);
}
static int bn_cmp_v(secd_value_t a, secd_value_t b) {
    secd_value_t args = secd_cons(&H, a, secd_cons(&H, b, SECD_NIL));
    secd_value_t r = prim_bn_cmp(&H, args);
    return secd_fixnum_value(r);
}

int main() {
    secd_heap_init(&H, 4095);

    // roundtrip small
    CHECK(strcmp(bn_to_str(bn_from_str("0")), "0") == 0, "zero");
    { char *g1=bn_to_str(bn_from_str("115200")); printf("DBG 115200 -> '%s'\n", g1);
  secd_value_t t=bn_from_str("115200"); printf("DBG nil=%d tag=%X\n", secd_is_nil(t), secd_get_type(t));
  CHECK(strcmp(g1,"115200")==0,"115200"); }
    { char *g=bn_to_str(bn_from_str("-42")); printf("DBG -42 got: [%s] nil=%d\n", g, (int)secd_is_nil(bn_from_str("-42"))); CHECK(strcmp(g,"-42")==0, "roundtrip -42"); }

    // add/sub/mul against known values
    struct { const char *a, *b, *sum, *diff, *prod; } V[] = {
        {"123456789","987654321","1111111110","-864197532","121932631112635269"},
        {"999999999","1","1000000000","999999998","999999999"},
        {"170","169","339","1","28730"},
        {"-7","3","-4","-10","-21"},
        {"0","12345","12345","-12345","0"},
    };
    for (auto &v : V) {
        secd_value_t a = bn_from_str(v.a), b = bn_from_str(v.b);
        CHECK(strcmp(bn_to_str(bn_op("add",a,b)), v.sum)==0, v.sum);
        CHECK(strcmp(bn_to_str(bn_op("sub",a,b)), v.diff)==0, v.diff);
        CHECK(strcmp(bn_to_str(bn_op("mul",a,b)), v.prod)==0, v.prod);
        // a = a*b/b must round-trip when b != 0
        if (strcmp(v.b,"0")!=0) {
            secd_value_t p = bn_op("mul",a,b);
            secd_value_t back = bn_op("div",p,b);
            CHECK(bn_cmp_v(back,a)==0, "mul-div inverse");
            secd_value_t m = bn_op("mod",p,b);
            CHECK(bn_cmp_v(m, bn_from_str("0"))==0, "exact mod zero");
        }
    }

    // factorial: 170! known leading/trailing digits + length
    secd_value_t one = bn_from_str("1");
    secd_value_t acc = one;
    for (long i = 2; i <= 170; i++) {
        char ib[16]; snprintf(ib,sizeof ib,"%ld",i);
        acc = bn_op("mul", acc, bn_from_str(ib));
    }
    char *s = bn_to_str(acc);
    size_t L = strlen(s);
    printf("170! length=%zu head=%.10s tail=%s\n", L, s, s+L-10);
    CHECK(L == 307, "170! has 307 digits");   // 170! ~ 7.25e306
    CHECK(strncmp(s, "7257415615", 10)==0, "170! leading digits");
    CHECK(strcmp(s+L-10, "0000000000")==0, "170! trailing zeros");

    // division with remainder on odd sizes
    secd_value_t big = bn_from_str("1000000000000000000000000007");
    secd_value_t d   = bn_from_str("987654321");
    secd_value_t q = bn_op("div", big, d), m = bn_op("mod", big, d);
    // check q*d + m == big
    secd_value_t qd = bn_op("mul", q, d);
    secd_value_t sum = bn_op("add", qd, m);
    CHECK(bn_cmp_v(sum, big)==0, "q*d+m == n");

    // GC survival: force collections between ops
    for (int i = 0; i < 50; i++) { secd_value_t t = bn_op("mul", big, big); (void)t; }
    CHECK(strcmp(bn_to_str(bn_from_str("115200")), "115200")==0, "post-GC sanity");

    printf(failures ? "*** %d FAILURES ***\n" : "ALL PASS\n", failures);
    return failures != 0;
}
