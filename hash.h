#ifndef HASH_H
#define HASH_H
#include <string>

// 128 bit hash via djbx33ax4 (Daniel Bernstein Times 33 with Addition interleaved 4x for 128 bits)
std::string Hash128(const uint8_t *buffer, size_t size);

#endif
