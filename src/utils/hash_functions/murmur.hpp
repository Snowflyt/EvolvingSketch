//-------------------------------------------------------------------------------------------------
// MurmurHash was written by Austin Appleby, and is placed in the public domain.
// The author hereby disclaims copyright to this source code.

#pragma once

#include <cstdint>

auto murmur_hash2_x86_32(const void *key, int len, uint32_t seed) -> uint32_t;
auto murmur_hash2_x86_64(const void *key, int len, uint64_t seed) -> uint64_t;
auto murmur_hash2_x64_64(const void *key, int len, uint64_t seed) -> uint64_t;
auto murmur_hash2a_x86_32(const void *key, int len, uint32_t seed) -> uint32_t;

void murmur_hash3_x86_32(const void *key, int len, uint32_t seed, void *out);
void murmur_hash3_x86_128(const void *key, int len, uint32_t seed, void *out);
void murmur_hash3_x64_128(const void *key, int len, uint32_t seed, void *out);
