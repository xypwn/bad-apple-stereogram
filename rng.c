#include "rng.h"

#include <math.h>
#include <float.h>

// xoshiro256** 1.0,
// derived from David Blackman and Sebastiano Vigna's public domain implmentation
// <https://prng.di.unimi.it/>
static inline uint64_t rotl(const uint64_t x, int32_t k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256ss_next(void* _self)
{
    RNG_XoShiRo256ss* self = _self;
    const uint64_t result = rotl(self->s[1] * 5, 7) * 9;

    const uint64_t t = self->s[1] << 17;

    self->s[2] ^= self->s[0];
    self->s[3] ^= self->s[1];
    self->s[1] ^= self->s[2];
    self->s[0] ^= self->s[3];

    self->s[2] ^= t;

    self->s[3] = rotl(self->s[3], 45);

    return result;
}

RNG_XoShiRo256ss rng_xoshiro256ss(uint64_t seed)
{
    RNG_XoShiRo256ss res = {
        .base = {
            .next = xoshiro256ss_next,
        }
    };
    uint64_t x = seed;
    for (size_t i = 0; i < 4; i++) {
        // splitmix64 to fill seed array, derived from
        // Sebastiano Vigna's public domain implementation
        // <https://prng.di.unimi.it/splitmix64.c>
        uint64_t z = (x += (uint64_t)0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * (uint64_t)0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * (uint64_t)0x94D049BB133111EB;
        res.s[i] = z ^ (z >> 31);
    }
    return res;
}

void rng_xoshiro256ss_jump(RNG_XoShiRo256ss* self)
{
    static const uint64_t JUMP[] = {
        0x180ec6d33cfd0aba,
        0xd5a61266f0c9392c,
        0xa9582618e03fc9aa,
        0x39abdc4529b1661c,
    };

    uint64_t s0 = 0;
    uint64_t s1 = 0;
    uint64_t s2 = 0;
    uint64_t s3 = 0;
    for (size_t i = 0; i < 4; i++) {
        for (size_t b = 0; b < 64; b++) {
            if (JUMP[i] & (uint64_t)1 << b) {
                s0 ^= self->s[0];
                s1 ^= self->s[1];
                s2 ^= self->s[2];
                s3 ^= self->s[3];
            }
            rng_next((RNG*)self);
        }
    }

    self->s[0] = s0;
    self->s[1] = s1;
    self->s[2] = s2;
    self->s[3] = s3;
}

uint64_t rng_u64(RNG* self)
{
    return rng_next(self);
}

uint64_t rng_u64_cap(RNG* self, uint64_t cap)
{
    // Bitmask with rejection
    // <https://www.pcg-random.org/posts/bounded-rands.html>
    uint64_t mask = ~(uint64_t)0;
    cap--;
    mask >>= __builtin_clzll(cap | 1);
    uint64_t x;
    do {
        x = rng_next(self) & mask;
    } while (x > cap);
    return x;
}

int64_t rng_i64(RNG* self)
{
    uint64_t x = rng_next(self);
    return *((int64_t*)&x);
}

int64_t rng_i64_cap(RNG* self, int64_t cap)
{
    return (int64_t)rng_u64_cap(self, cap);
}

double rng_f64(RNG* self)
{
    uint64_t x = rng_next(self);
    union {
        uint64_t i;
        double d;
    } u = { .i = (uint64_t)0x3FF << 52 | x >> 12 };
    return u.d - 1.0;
}

double rng_f64_cap(RNG* self, double cap)
{
    return rng_f64(self) * cap;
}

double rng_f64_range(RNG* self, double min, double max)
{
    return min + rng_f64_cap(self, max - min);
}

bool rng_bool(RNG* self, double p_true)
{
    return rng_f64(self) < p_true;
}

double rng_gauss(RNG* self)
{
    // Box Mueller transform
    // <https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform>
    double u;
    do {
        u = rng_f64(self);
    } while (u <= DBL_EPSILON);
    double v = rng_f64(self);
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

double rng_gauss_ex(RNG* self, double mu, double sigma)
{
    return rng_gauss(self) * sigma + mu;
}
