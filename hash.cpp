#include <cstring> // std::memcpy

#include "hash.h"

// 128 bit hash via djbx33ax4 (Daniel Bernstein Times 33 with Addition interleaved 4x for 128 bits)
static void Hash128(const uint8_t *buffer, size_t size, uint8_t *out)
{
	const uint8_t *const end = (const uint8_t *const )buffer + size;
	uint32_t state[] = { 5381, 5381, 5381, 5381 };
	size_t s = 0;
	for (const uint8_t *p = buffer; p < end; p++)
	{
		state[s] = state[s] * 33  + *p;
		s = (s+1) & 0x03;
	}
	std::memcpy(out, state, sizeof state);
}

std::string Hash128(const uint8_t *buffer, size_t size)
{
	std::string result;
	uint8_t output[16];
	Hash128(buffer, size, output);
	static const char *k_hex = "0123456789ABCDEF";
	for (size_t i = 0; i < sizeof output; ++i)
	{
		result += k_hex[(output[i] >> 4) & 0x0F];
		result += k_hex[output[i] & 0x0F];
	}
	return result;
}
