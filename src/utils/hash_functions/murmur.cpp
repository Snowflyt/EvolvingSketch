//-------------------------------------------------------------------------------------------------
// MurmurHash was written by Austin Appleby, and is placed in the public domain.
// The author hereby disclaims copyright to this source code.

#include <cstdint>

#include "murmur.hpp"

//-------------------------------------------------------------------------------------------------
// Platform-specific functions and macros

// Microsoft Visual Studio

#if defined(_MSC_VER)

#define FORCE_INLINE __forceinline

#define ROTL32(x, y) _rotl(x, y)
#define ROTL64(x, y) _rotl64(x, y)

#define BIG_CONSTANT(x) (x)

// Other compilers

#else // defined(_MSC_VER)

#define FORCE_INLINE inline __attribute__((always_inline))

inline auto rotl32(uint32_t x, int8_t r) -> uint32_t { return (x << r) | (x >> (32 - r)); }

inline auto rotl64(uint64_t x, int8_t r) -> uint64_t { return (x << r) | (x >> (64 - r)); }

#define ROTL32(x, y) rotl32(x, y)
#define ROTL64(x, y) rotl64(x, y)

#define BIG_CONSTANT(x) (x##LLU)

#endif // !defined(_MSC_VER)

//-------------------------------------------------------------------------------------------------
// MurmurHash2 was written by Austin Appleby, and is placed in the public domain. The author hereby
// disclaims copyright to this source code.

// Note - This code makes a few assumptions about how your machine behaves -

// 1. We can read a 4-byte value from any address without crashing
// 2. sizeof(int) == 4

// And it has a few limitations -

// 1. It will not work incrementally.
// 2. It will not produce the same results on little-endian and big-endian machines.

/**
 * MurmurHash2, 32-bit hash for 32-bit platforms, by Austin Appleby
 *
 * objsize: 0-0x166: 358
 */
auto murmur_hash2_x86_32(const void *key, int len, uint32_t seed) -> uint32_t {
  // 'm' and 'r' are mixing constants generated offline.
  // They're not really 'magic', they just happen to work well.

  const uint32_t m = 0x5bd1e995;
  const int r = 24;

  // Initialize the hash to a 'random' value

  uint32_t h = seed ^ len;

  // Mix 4 bytes at a time into the hash

  const unsigned char *data = (const unsigned char *)key;

  while (len >= 4) {
    uint32_t k = *(uint32_t *)data;

    k *= m;
    k ^= k >> r;
    k *= m;

    h *= m;
    h ^= k;

    data += 4;
    len -= 4;
  }

  // Handle the last few bytes of the input array

  switch (len) {
  case 3:
    h ^= data[2] << 16;
  case 2:
    h ^= data[1] << 8;
  case 1:
    h ^= data[0];
    h *= m;
  };

  // Do a few final mixes of the hash to ensure the last few
  // bytes are well-incorporated.

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}

/**
 * MurmurHash2, 64-bit hash for 32-bit platforms, by Austin Appleby
 *
 * objsize: 0x340-0x4fc: 444
 */
auto murmur_hash2_x86_64(const void *key, int len, uint64_t seed) -> uint64_t {
  const uint32_t m = 0x5bd1e995;
  const int r = 24;

  uint32_t h1 = uint32_t(seed) ^ len;
  uint32_t h2 = uint32_t(seed >> 32);

  const uint32_t *data = (const uint32_t *)key;

  while (len >= 8) {
    uint32_t k1 = *data++;
    k1 *= m;
    k1 ^= k1 >> r;
    k1 *= m;
    h1 *= m;
    h1 ^= k1;
    len -= 4;

    uint32_t k2 = *data++;
    k2 *= m;
    k2 ^= k2 >> r;
    k2 *= m;
    h2 *= m;
    h2 ^= k2;
    len -= 4;
  }

  if (len >= 4) {
    uint32_t k1 = *data++;
    k1 *= m;
    k1 ^= k1 >> r;
    k1 *= m;
    h1 *= m;
    h1 ^= k1;
    len -= 4;
  }

  switch (len) {
  case 3:
    h2 ^= ((unsigned char *)data)[2] << 16;
  case 2:
    h2 ^= ((unsigned char *)data)[1] << 8;
  case 1:
    h2 ^= ((unsigned char *)data)[0];
    h2 *= m;
  };

  h1 ^= h2 >> 18;
  h1 *= m;
  h2 ^= h1 >> 22;
  h2 *= m;
  h1 ^= h2 >> 17;
  h1 *= m;
  h2 ^= h1 >> 19;
  h2 *= m;

  uint64_t h = h1;

  h = (h << 32) | h2;

  return h;
}

/**
 * MurmurHash2, 64-bit hash for 64-bit platforms, by Austin Appleby
 *
 * The same caveats as 32-bit MurmurHash2 apply here - beware of alignment and endian-ness issues
 * if used across multiple platforms.
 *
 * objsize: 0x170-0x321: 433
 */
auto murmur_hash2_x64_64(const void *key, int len, uint64_t seed) -> uint64_t {
  const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
  const int r = 47;

  uint64_t h = seed ^ (len * m);

  const uint64_t *data = (const uint64_t *)key;
  const uint64_t *end = data + (len / 8);

  while (data != end) {
    uint64_t k = *data++;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char *data2 = (const unsigned char *)data;

  switch (len & 7) {
  case 7:
    h ^= uint64_t(data2[6]) << 48;
  case 6:
    h ^= uint64_t(data2[5]) << 40;
  case 5:
    h ^= uint64_t(data2[4]) << 32;
  case 4:
    h ^= uint64_t(data2[3]) << 24;
  case 3:
    h ^= uint64_t(data2[2]) << 16;
  case 2:
    h ^= uint64_t(data2[1]) << 8;
  case 1:
    h ^= uint64_t(data2[0]);
    h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

#define mmix(h, k)                                                                                 \
  {                                                                                                \
    k *= m;                                                                                        \
    k ^= k >> r;                                                                                   \
    k *= m;                                                                                        \
    h *= m;                                                                                        \
    h ^= k;                                                                                        \
  }

/**
 * MurmurHash2A, 32-bit hash for 32-bit platforms, by Austin Appleby
 *
 * This is a variant of MurmurHash2 modified to use the Merkle-Damgard construction. Bulk speed
 * should be identical to Murmur2, small-key speed will be 10%-20% slower due to the added overhead
 * at the end of the hash.
 *
 * This variant fixes a minor issue where null keys were more likely to collide with each other
 * than expected, and also makes the function more amenable to incremental implementations.
 */
auto murmur_hash2a_x86_32(const void *key, int len, uint32_t seed) -> uint32_t {
  const uint32_t m = 0x5bd1e995;
  const int r = 24;
  uint32_t l = len;

  const unsigned char *data = (const unsigned char *)key;

  uint32_t h = seed;

  while (len >= 4) {
    uint32_t k = *(uint32_t *)data;

    mmix(h, k);

    data += 4;
    len -= 4;
  }

  uint32_t t = 0;

  switch (len) {
  case 3:
    t ^= data[2] << 16;
  case 2:
    t ^= data[1] << 8;
  case 1:
    t ^= data[0];
  };

  mmix(h, t);
  mmix(h, l);

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}

//-------------------------------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public domain. The author hereby
// disclaims copyright to this source code.

// Note - The x86 and x64 versions do _not_ produce the same results, as the algorithms are
// optimized for their respective platforms. You can still compile and run any of them on any
// platform, but your performance with the non-native version will be less than optimal.

//-------------------------------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only handle aligned reads, do
// the conversion here

FORCE_INLINE auto getblock32(const uint32_t *p, int i) -> uint32_t { return p[i]; }

FORCE_INLINE auto getblock64(const uint64_t *p, int i) -> uint64_t { return p[i]; }

//-------------------------------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

FORCE_INLINE auto fmix32(uint32_t h) -> uint32_t {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

//----------

FORCE_INLINE auto fmix64(uint64_t k) -> uint64_t {
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xff51afd7ed558ccd);
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
  k ^= k >> 33;

  return k;
}

//-------------------------------------------------------------------------------------------------

/**
 * MurmurHash3, 32-bit hash for 32-bit platforms, by Austin Appleby
 *
 * objsize: 0x0-0x15f: 351
 */
void murmur_hash3_x86_32(const void *key, int len, uint32_t seed, void *out) {
  const uint8_t *data = (const uint8_t *)key;
  const int nblocks = len / 4;

  uint32_t h1 = seed;

  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  //----------
  // body

  const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);

  for (int i = -nblocks; i; i++) {
    uint32_t k1 = getblock32(blocks, i);

    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;

    h1 ^= k1;
    h1 = ROTL32(h1, 13);
    h1 = h1 * 5 + 0xe6546b64;
  }

  //----------
  // tail

  const uint8_t *tail = (const uint8_t *)(data + nblocks * 4);

  uint32_t k1 = 0;

  switch (len & 3) {
  case 3:
    k1 ^= tail[2] << 16;
  case 2:
    k1 ^= tail[1] << 8;
  case 1:
    k1 ^= tail[0];
    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;
    h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;

  h1 = fmix32(h1);

  *(uint32_t *)out = h1;
}

/**
 * MurmurHash3, 128-bit hash for 32-bit platforms, by Austin Appleby
 *
 * objsize: 0x160-0x4bb: 859
 */
void murmur_hash3_x86_128(const void *key, const int len, uint32_t seed, void *out) {
  const uint8_t *data = (const uint8_t *)key;
  const int nblocks = len / 16;

  uint32_t h1 = seed;
  uint32_t h2 = seed;
  uint32_t h3 = seed;
  uint32_t h4 = seed;

  const uint32_t c1 = 0x239b961b;
  const uint32_t c2 = 0xab0e9789;
  const uint32_t c3 = 0x38b34ae5;
  const uint32_t c4 = 0xa1e38b93;

  //----------
  // body

  const uint32_t *blocks = (const uint32_t *)(data + nblocks * 16);

  for (int i = -nblocks; i; i++) {
    uint32_t k1 = getblock32(blocks, i * 4 + 0);
    uint32_t k2 = getblock32(blocks, i * 4 + 1);
    uint32_t k3 = getblock32(blocks, i * 4 + 2);
    uint32_t k4 = getblock32(blocks, i * 4 + 3);

    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;
    h1 ^= k1;

    h1 = ROTL32(h1, 19);
    h1 += h2;
    h1 = h1 * 5 + 0x561ccd1b;

    k2 *= c2;
    k2 = ROTL32(k2, 16);
    k2 *= c3;
    h2 ^= k2;

    h2 = ROTL32(h2, 17);
    h2 += h3;
    h2 = h2 * 5 + 0x0bcaa747;

    k3 *= c3;
    k3 = ROTL32(k3, 17);
    k3 *= c4;
    h3 ^= k3;

    h3 = ROTL32(h3, 15);
    h3 += h4;
    h3 = h3 * 5 + 0x96cd1c35;

    k4 *= c4;
    k4 = ROTL32(k4, 18);
    k4 *= c1;
    h4 ^= k4;

    h4 = ROTL32(h4, 13);
    h4 += h1;
    h4 = h4 * 5 + 0x32ac3b17;
  }

  //----------
  // tail

  const uint8_t *tail = (const uint8_t *)(data + nblocks * 16);

  uint32_t k1 = 0;
  uint32_t k2 = 0;
  uint32_t k3 = 0;
  uint32_t k4 = 0;

  switch (len & 15) {
  case 15:
    k4 ^= tail[14] << 16;
  case 14:
    k4 ^= tail[13] << 8;
  case 13:
    k4 ^= tail[12] << 0;
    k4 *= c4;
    k4 = ROTL32(k4, 18);
    k4 *= c1;
    h4 ^= k4;

  case 12:
    k3 ^= tail[11] << 24;
  case 11:
    k3 ^= tail[10] << 16;
  case 10:
    k3 ^= tail[9] << 8;
  case 9:
    k3 ^= tail[8] << 0;
    k3 *= c3;
    k3 = ROTL32(k3, 17);
    k3 *= c4;
    h3 ^= k3;

  case 8:
    k2 ^= tail[7] << 24;
  case 7:
    k2 ^= tail[6] << 16;
  case 6:
    k2 ^= tail[5] << 8;
  case 5:
    k2 ^= tail[4] << 0;
    k2 *= c2;
    k2 = ROTL32(k2, 16);
    k2 *= c3;
    h2 ^= k2;

  case 4:
    k1 ^= tail[3] << 24;
  case 3:
    k1 ^= tail[2] << 16;
  case 2:
    k1 ^= tail[1] << 8;
  case 1:
    k1 ^= tail[0] << 0;
    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;
    h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;
  h2 ^= len;
  h3 ^= len;
  h4 ^= len;

  h1 += h2;
  h1 += h3;
  h1 += h4;
  h2 += h1;
  h3 += h1;
  h4 += h1;

  h1 = fmix32(h1);
  h2 = fmix32(h2);
  h3 = fmix32(h3);
  h4 = fmix32(h4);

  h1 += h2;
  h1 += h3;
  h1 += h4;
  h2 += h1;
  h3 += h1;
  h4 += h1;

  ((uint32_t *)out)[0] = h1;
  ((uint32_t *)out)[1] = h2;
  ((uint32_t *)out)[2] = h3;
  ((uint32_t *)out)[3] = h4;
}

/**
 * MurmurHash3, 128-bit hash for 64-bit platforms, by Austin Appleby
 *
 * objsize: 0x500-0x7bb: 699
 */
void murmur_hash3_x64_128(const void *key, const int len, const uint32_t seed, void *out) {
  const uint8_t *data = (const uint8_t *)key;
  const int nblocks = len / 16;

  uint64_t h1 = seed;
  uint64_t h2 = seed;

  const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
  const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

  //----------
  // body

  const uint64_t *blocks = (const uint64_t *)(data);

  for (int i = 0; i < nblocks; i++) {
    uint64_t k1 = getblock64(blocks, i * 2 + 0);
    uint64_t k2 = getblock64(blocks, i * 2 + 1);

    k1 *= c1;
    k1 = ROTL64(k1, 31);
    k1 *= c2;
    h1 ^= k1;

    h1 = ROTL64(h1, 27);
    h1 += h2;
    h1 = h1 * 5 + 0x52dce729;

    k2 *= c2;
    k2 = ROTL64(k2, 33);
    k2 *= c1;
    h2 ^= k2;

    h2 = ROTL64(h2, 31);
    h2 += h1;
    h2 = h2 * 5 + 0x38495ab5;
  }

  //----------
  // tail

  const uint8_t *tail = (const uint8_t *)(data + nblocks * 16);

  uint64_t k1 = 0;
  uint64_t k2 = 0;

  switch (len & 15) {
  case 15:
    k2 ^= ((uint64_t)tail[14]) << 48;
  case 14:
    k2 ^= ((uint64_t)tail[13]) << 40;
  case 13:
    k2 ^= ((uint64_t)tail[12]) << 32;
  case 12:
    k2 ^= ((uint64_t)tail[11]) << 24;
  case 11:
    k2 ^= ((uint64_t)tail[10]) << 16;
  case 10:
    k2 ^= ((uint64_t)tail[9]) << 8;
  case 9:
    k2 ^= ((uint64_t)tail[8]) << 0;
    k2 *= c2;
    k2 = ROTL64(k2, 33);
    k2 *= c1;
    h2 ^= k2;

  case 8:
    k1 ^= ((uint64_t)tail[7]) << 56;
  case 7:
    k1 ^= ((uint64_t)tail[6]) << 48;
  case 6:
    k1 ^= ((uint64_t)tail[5]) << 40;
  case 5:
    k1 ^= ((uint64_t)tail[4]) << 32;
  case 4:
    k1 ^= ((uint64_t)tail[3]) << 24;
  case 3:
    k1 ^= ((uint64_t)tail[2]) << 16;
  case 2:
    k1 ^= ((uint64_t)tail[1]) << 8;
  case 1:
    k1 ^= ((uint64_t)tail[0]) << 0;
    k1 *= c1;
    k1 = ROTL64(k1, 31);
    k1 *= c2;
    h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;
  h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t *)out)[0] = h1;
  ((uint64_t *)out)[1] = h2;
}
