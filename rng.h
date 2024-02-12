#ifndef __RNG_H__
#define __RNG_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t (*next)(void* self);
} RNG;

static inline uint64_t rng_next(RNG* self)
{
    return self->next(self);
}

// xoshiro256**
// The state is fully contained in the struct,
// meaning a copy of the struct is also
// a copy of its state.
typedef struct {
    RNG base;
    uint64_t s[4];
} RNG_XoShiRo256ss;

RNG_XoShiRo256ss rng_xoshiro256ss(uint64_t seed);

// This is the jump function for the generator. It is equivalent
// to 2^128 calls to next(); it can be used to generate 2^128
// non-overlapping subsequences for parallel computations.
void rng_xoshiro256ss_jump(RNG_XoShiRo256ss* self);

// Generate next pseudorandom number
uint64_t rng_next(RNG* self);
// Generate uint64_t
uint64_t rng_u64(RNG* self);
// Generate uint64_t with exclusive limit
uint64_t rng_u64_cap(RNG* self, uint64_t cap);
// Generate int64_t
int64_t rng_i64(RNG* self);
// Generate uint64_t with exclusive limit
int64_t rng_i64_cap(RNG* self, int64_t cap);
// Generate double between 0 and 1
double rng_f64(RNG* self);
// Generate double with limit
double rng_f64_cap(RNG* self, double cap);
// Generate double in range
double rng_f64_range(RNG* self, double min, double max);
// Generate bool with p_true as probability of outputting true
bool rng_bool(RNG* self, double p_true);
// Generate a gauss distributed double with unit variance
// (variance of 1) and mean of 0
double rng_gauss(RNG* self);
// Generate a gauss distributed double with mean (mu)
// and standard deviation (sigma)
double rng_gauss_ex(RNG* self, double mu, double sigma);

#endif // __RNG_H__
