// fast_utils.hpp
#pragma once

#if defined(__GNUC__) || defined(__clang__)
  #define AI inline __attribute__((always_inline))
#else
  #define AI inline
#endif

// ========================= Utilities =========================
// lsb64(x) returns the mask of x’s least-significant 1-bit (x & −x)
// ctz64(x) returns the number of trailing zero bits (the index of that bit).
// ctz64(lsb64(x)) returns the index of the first active candidate with ~speed~
static AI int popcount64(uint64_t x){ return __builtin_popcountll(x); }
static AI int ctz64(uint64_t x){ return __builtin_ctzll(x); }
static AI uint64_t lsb64(uint64_t x){ return x & -x; }
