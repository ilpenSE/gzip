#include "gzip.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define MAX_DIST  4096
#define MIN_MATCH 3
#define MAX_MATCH 258
#define LITLEN_SYMS 288
#define DIST_SYMS 30

typedef struct {
  bool is_lit;
  union {
    unsigned char lit;
    struct {
      uint16_t dist;
      uint16_t len;
    };
  } as;
} Token;

typedef struct {
  uint16_t litlen_sym;   // symbol for literal and length
  uint16_t len_extra;    // extra bit for length
  uint8_t  len_extra_n;  // amount of bits in len_extra
  uint16_t dist_sym;     // symbol for distance
  uint16_t dist_extra;   // extra bit for distance
  uint8_t  dist_extra_n; // amount of bits in dist_extra
  bool match;
} Symbol;

typedef struct {
  unsigned char *out;
  size_t pos;
  uint32_t bitbuf;
  int bitcnt;
} BitWriter;

static size_t build_symbols(const Token *ts, size_t nt, Symbol *syms);
static size_t lzss_encode(const char *buffer, size_t size, Token *out);
static size_t lzss_decode(const Token *ts, size_t nt, char *out);
static void make_codes(const uint8_t *lens, size_t size, uint16_t *codes);
static void put_bits(BitWriter *bw, uint32_t v, int n);
static void put_huff(BitWriter *bw, int sym, const uint16_t *codes, const uint8_t *lens);
static void flush_bits(BitWriter *bw);
static void calculate_freqs(Symbols *syms, size_t ns, uint32_t *litlen_freq, uint32_t *dist_freq);
static void fixed_huffman(uint8_t *litlen_lens, uint8_t *dist_lens);
static void huffman_lengths(uint32_t *freq, size_t nfreq, uint8_t *lens, int max_bits);

size_t deflate(const char *buffer, size_t size, char *out, DeflateStrategy strat)
{
  Token *tokens = malloc(size * sizeof(Token));
  size_t nt = lzss_encode(buffer, size, tokens);
  Symbol *symbols = malloc(nt * sizeof(*symbols));
  size_t ns = build_symbols(tokens, nt, symbols);

  uint8_t litlen_lens[LITLEN_SYMS], dist_lens[DIST_SYMS];
  uint16_t litlen_codes[LITLEN_SYMS], dist_codes[DIST_SYMS];

  if (strat == DEFLATE_ALWAYS_DYNAMIC_HUFFMAN) {
    uint32_t litlen_freq[LITLEN_SYMS] = {0}, dist_freq[DIST_SYMS] = {0};
    calculate_freqs(symbols, ns, litlen_freq, dist_freq);

    huffman_lengths(litlen_freq, LITLEN_SYMS, litlen_lens, 15);
    huffman_lengths(dist_freq, DIST_SYMS, dist_lens, 15);
  } else if (strat == DEFLATE_ALWAYS_FIXED_HUFFMAN) {
    fixed_huffman(litlen_codes, dist_lens);
  }

  make_codes(litlen_lens, LITLEN_SYMS, litlen_codes);
  make_codes(dist_lens, DIST_SYMS, dist_codes);

  BitWriter bw = {.out=(unsigned char *)out};
  put_bits(&bw, 1, 1);   // BFINAL = 1

  if (strat == DEFLATE_ALWAYS_DYNAMIC_HUFFMAN) {
    put_bits(&bw, 2, 2);   // BTYPE = 10 (dynamic)
  } else if (strat == DEFLATE_ALWAYS_FIXED_HUFFMAN) {
    put_bits(&bw, 1, 2);   // BTYPE = 01 (fixed)
  }

  for (size_t i = 0; i < ns; i++) {
    Symbol *s = &symbols[i];
    put_huff(&bw, s->litlen_sym, litlen_codes, litlen_lens);
    if (s->match) {
      put_bits(&bw, s->len_extra, s->len_extra_n);
      put_huff(&bw, s->dist_sym, dist_codes, dist_lens);
      put_bits(&bw, s->dist_extra, s->dist_extra_n);
    }
  }
  put_huff(&bw, 256, litlen_codes, litlen_lens);
  flush_bits(&bw);

  free(symbols); free(tokens);
  return bw.pos;
}

size_t inflate(const char *deflated, size_t size, char *out)
{
  (void)deflated; (void)size; (void)out;
  return 0;
}

static void calculate_freqs(Symbols *syms, size_t ns, uint32_t *litlen_freq, uint32_t *dist_freq)
{
  for (size_t i = 0; i < ns; i++) {
    Symbol *s = &syms[i];
    litlen_freq[s->litlen_sym]++;
    if (s->match) dist_freq[s->dist_sym]++;
  }
  litlen_freq[256] = 1;
}

static void huffman_lengths(uint32_t *freq, size_t n, uint8_t *lens, int max_bits)
{
  uint32_t f[LITLEN_SYMS];
  memcpy(f, freq, n * sizeof(uint32_t));
  assert(false && "TODO: huffman_lengths");
}

static void fixed_huffman(uint8_t *litlen_lens, uint8_t *dist_lens)
{
  for (int i = 0;   i <= 143; i++) litlen_lens[i] = 8;
  for (int i = 144; i <= 255; i++) litlen_lens[i] = 9;
  for (int i = 256; i <= 279; i++) litlen_lens[i] = 7;
  for (int i = 280; i <= 287; i++) litlen_lens[i] = 8;
  for (int i = 0; i < 30; i++)     dist_lens[i] = 5;
}

static uint32_t reverse_bits(uint32_t c, int n)
{
  uint32_t r = 0;
  for (int i = 0; i < n; i++) {
    r = (r << 1) | (c & 1);
    c >>= 1;
  }
  return r;
}

static void put_huff(BitWriter *bw, int sym, const uint16_t *codes, const uint8_t *lens)
{
  put_bits(bw, reverse_bits(codes[sym], lens[sym]), lens[sym]);
}

static void put_bits(BitWriter *bw, uint32_t v, int n)
{
  bw->bitbuf |= v << bw->bitcnt;
  bw->bitcnt += n;
  while (bw->bitcnt >= 8) {
    bw->out[bw->pos++] = bw->bitbuf & 0xFF;
    bw->bitbuf >>= 8;
    bw->bitcnt -= 8;
  }
}

static void flush_bits(BitWriter *bw)
{
  if (bw->bitcnt <= 0) return;
  bw->out[bw->pos++] = bw->bitbuf & 0xFF;
  bw->bitbuf = 0;
  bw->bitcnt = 0;
}

static void make_codes(const uint8_t *lens, size_t size, uint16_t *codes)
{
  int count[16] = {0}, next[16];
  for (size_t i = 0; i < size; i++) count[lens[i]]++;
  count[0] = 0;
  int code = 0;
  for (int l = 1; l <= 15; l++) {
    code = (code + count[l-1]) << 1;
    next[l] = code;
  }

  for (size_t i = 0; i < size; i++)
    if (lens[i]) codes[i] = next[lens[i]]++;
}

static size_t lzss_encode(const char *buffer, size_t size, Token *out)
{
  size_t i = 0, nt = 0;
  while (i < size) {
    size_t best_l = 0, best_d = 0;
    // i - j <= MAX_DIST
    for (size_t j = (i > MAX_DIST ? i - MAX_DIST : 0); j < i; j++) {
      size_t l = 0;
      while (l < MAX_MATCH && i + l < size && buffer[i + l] == buffer[j + l]) l++;
      if (l > best_l) { best_l = l; best_d = i - j; }
    }

    if (best_l >= MIN_MATCH) {
      out[nt++] = (Token){.is_lit=false, .as={.dist=best_d, .len=best_l}};
      i += best_l;
    } else {
      out[nt++] = (Token){.is_lit=true, .as.lit=(unsigned char)buffer[i]};
      i += 1;
    }
  }
  return nt;
}

static size_t lzss_decode(const Token *ts, size_t nt, char *out)
{
  size_t size = 0;
  for (size_t i = 0; i < nt; i++) {
    Token tok = ts[i];
    if (tok.is_lit) {
      out[size++] = (char)tok.as.lit;
    } else {
      for (size_t j = 0; j < tok.as.len; j++, size++) {
        out[size] = out[size - tok.as.dist];
      }
    }
  }
  return size;
}

// uzunluk sembolü 257+i için: taban uzunluk ve ek bit sayısı
static const uint16_t len_base[29] = {
  3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
  35,43,51,59, 67,83,99,115, 131,163,195,227, 258
};
static const uint8_t len_extra[29] = {
  0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
  3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};

// mesafe sembolü i için
static const uint16_t dist_base[30] = {
  1,2,3,4, 5,7, 9,13, 17,25, 33,49, 65,97, 129,193,
  257,385, 513,769, 1025,1537, 2049,3073, 4097,6145,
  8193,12289, 16385,24577
};
static const uint8_t dist_extra[30] = {
  0,0,0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6,
  7,7, 8,8, 9,9, 10,10, 11,11, 12,12, 13,13
};

static int find_len_index(uint16_t len)
{
  for (int i = 28; i >= 0; i--)
    if (len_base[i] <= len) return i;
  return -1;
}

static int find_dist_index(uint16_t dist)
{
  for (int i = 29; i >= 0; i--)
    if (dist_base[i] <= dist) return i;
  return -1;
}

static size_t build_symbols(const Token *ts, size_t nt, Symbol *syms)
{
  size_t ns = 0;
  for (size_t i = 0; i < nt; i++) {
    const Token *t = &ts[i];
    if (t->is_lit) {
      syms[ns++] = (Symbol){.litlen_sym=t->as.lit, .match=false};
    } else {
      uint16_t len = t->as.len;
      uint16_t dist = t->as.dist;
      int li = find_len_index(len);
      int di = find_dist_index(dist);

      syms[ns++] = (Symbol){
        .litlen_sym=257 + li, .len_extra=len - len_base[li], .len_extra_n=len_extra[li],
        .dist_sym=di, .dist_extra=dist - dist_base[di], .dist_extra_n=dist_extra[di],
        .match=true
      };
    }
  }
  return ns;
}
