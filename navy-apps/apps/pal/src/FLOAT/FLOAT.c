#include "FLOAT.h"
#include <stdint.h>
#include <assert.h>

static uint32_t F_abs_u32(FLOAT a) {
  return (a < 0) ? (~(uint32_t)a + 1) : (uint32_t)a;
}

static FLOAT F_from_mag(uint32_t mag, int neg) {
  return neg ? (FLOAT)(~mag + 1) : (FLOAT)mag;
}

static uint32_t F_div_mag(uint32_t a, uint32_t b) {
  assert(b != 0);

  uint32_t q = 0;
  uint32_t rem = 0;

  for (int bit = 47; bit >= 0; bit --) {
    uint32_t input = (bit >= FLOAT_FBITS) ? ((a >> (bit - FLOAT_FBITS)) & 1u) : 0;
    rem = (rem << 1) | input;

    if (rem >= b) {
      rem -= b;
      assert(bit < 32);
      q |= 1u << bit;
    }
  }

  return q;
}

FLOAT F_mul_F(FLOAT a, FLOAT b) {
  int64_t product = (int64_t)a * b;
  return (product < 0) ? (FLOAT)(-((-product) >> FLOAT_FBITS)) :
                         (FLOAT)(product >> FLOAT_FBITS);
}

FLOAT F_div_F(FLOAT a, FLOAT b) {
  assert(b != 0);
  int neg = (a < 0) ^ (b < 0);
  uint32_t mag = F_div_mag(F_abs_u32(a), F_abs_u32(b));

  assert(neg ? mag <= 0x80000000u : mag <= 0x7fffffffu);
  return F_from_mag(mag, neg);
}

FLOAT f2F(float a) {
  /* You should figure out how to convert `a' into FLOAT without
   * introducing x87 floating point instructions. Else you can
   * not run this code in NEMU before implementing x87 floating
   * point instructions, which is contrary to our expectation.
   *
   * Hint: The bit representation of `a' is already on the
   * stack. How do you retrieve it to another variable without
   * performing arithmetic operations on it directly?
   */

  union {
    float f;
    uint32_t u;
  } cvt = { .f = a };

  uint32_t bits = cvt.u;
  uint32_t sign = bits >> 31;
  uint32_t raw_exp = (bits >> 23) & 0xff;
  uint32_t frac = bits & 0x7fffff;

  if (raw_exp == 0) {
    return 0;
  }
  assert(raw_exp != 0xff);

  uint32_t mant = (1u << 23) | frac;
  int shift = (int)raw_exp - 134;  /* exp - 127 - 23 + FLOAT_FBITS */
  uint64_t mag;

  if (shift >= 0) {
    assert(shift < 40);
    mag = (uint64_t)mant << shift;
  }
  else {
    int rshift = -shift;
    mag = (rshift >= 64) ? 0 : ((uint64_t)mant >> rshift);
  }

  assert(sign ? mag <= 0x80000000ull : mag <= 0x7fffffffull);
  return F_from_mag((uint32_t)mag, sign);
}

FLOAT Fabs(FLOAT a) {
  assert(a != (FLOAT)0x80000000u);
  return (a < 0) ? -a : a;
}

/* Functions below are already implemented */

FLOAT Fsqrt(FLOAT x) {
  FLOAT dt, t = int2F(2);

  do {
    dt = F_div_int((F_div_F(x, t) - t), 2);
    t += dt;
  } while(Fabs(dt) > f2F(1e-4f));

  return t;
}

FLOAT Fpow(FLOAT x, FLOAT y) {
  /* we only compute x^0.333 */
  (void)y;
  FLOAT t2, dt, t = int2F(2);

  do {
    t2 = F_mul_F(t, t);
    dt = (F_div_F(x, t2) - t) / 3;
    t += dt;
  } while(Fabs(dt) > f2F(1e-4f));

  return t;
}
