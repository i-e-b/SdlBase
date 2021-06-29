#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma once

#ifndef MathBits_h
#define MathBits_h

#include <cstdint>

#define RAND_MAX 0x7FFFFFFF

uint32_t internal_seed = 0xDEADBEEF;

uint32_t triple32(uint32_t* seed) {
    auto x = (seed == nullptr) ? internal_seed : *seed;
    x ^= x >> 17;
    x *= UINT32_C(0xed5ad4bb);
    x ^= x >> 11;
    x *= UINT32_C(0xac4c1b51);
    x ^= x >> 15;
    x *= UINT32_C(0x31848bab);
    x ^= x >> 14;
    if (seed != nullptr) *seed = x;
    else internal_seed = x;
    return x;
}

uint32_t random_at_most(uint32_t max) {
    unsigned long
    // max <= RAND_MAX < ULONG_MAX, so this is okay.
    num_bins = (unsigned long)max + 1,
            num_rand = (unsigned long)RAND_MAX + 1,
            bin_size = num_rand / num_bins,
            defect = num_rand % num_bins;
    uint32_t x;
    do { x = triple32(nullptr); }
    while (num_rand - defect <= (unsigned long)x); // This is carefully written not to overflow

    // Truncated division is intentional
    return x / bin_size;
}

uint32_t random_at_most(uint32_t seedStep, uint32_t max) {
    unsigned long
        // max <= RAND_MAX < ULONG_MAX, so this is okay.
        num_bins = (unsigned long)max + 1,
        num_rand = (unsigned long)RAND_MAX + 1,
        bin_size = num_rand / num_bins,
        defect = num_rand % num_bins;

    uint32_t x = seedStep;
    do { x = triple32(&x); }
    while (num_rand - defect <= (unsigned long)x); // This is carefully written not to overflow

    // Truncated division is intentional
    return x / bin_size;
}

int32_t ranged_random(uint32_t seedStep, int32_t min, int32_t max) {
    uint32_t range = max - min;
    uint32_t v = random_at_most(seedStep, range);
    return ((int32_t)v) + min;
}

uint32_t int_random(uint32_t seedStep) {
    return triple32(&seedStep);
}

float float_random(uint32_t seedStep) {
    auto b = (float)triple32(&seedStep);
    return b / ((float)RAND_MAX);
}

#endif

#pragma clang diagnostic pop