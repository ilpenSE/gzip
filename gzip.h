#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  DEFLATE_ALWAYS_FIXED_HUFFMAN = 0,
  DEFLATE_ALWAYS_DYNAMIC_HUFFMAN,
  DEFLATE_AUTO,
} DeflateStrategy;

extern size_t inflate(const char *deflated, size_t size, char *out);
extern size_t deflate(const char *buffer, size_t size, char *out, DeflateStrategy strat);
