#include "gzip.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void print_bytes(const char *buffer, size_t size)
{
  for (size_t i = 0; i < size; i++) {
    printf("%02x%s", buffer[i] & 0xFF, (i == size - 1 ? "\n" : " "));
  }
}

size_t read_entire_file(const char *file_path, char *out)
{
  FILE *f = fopen(file_path, "rb");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  size_t read = n;
  if (out) {
    read = fread(out, 1, n, f);
  }
  fclose(f);
  return read;
}

int main(void)
{
  const char *file_path = "./gzip.c";
  char *file_content = malloc(read_entire_file(file_path, NULL) + 1);
  size_t file_size = read_entire_file(file_path, file_content);
  printf("File %s contains %zu bytes.\n", file_path, file_size);

  char *output = malloc(file_size + file_size / 8 + 64);
  size_t output_sz = deflate(file_content, file_size, output);

  char out_file_name[256];
  snprintf(out_file_name, sizeof(out_file_name), "%s_deflated", file_path);
  FILE *f = fopen(out_file_name, "wb");
  size_t written = fwrite(output, 1, output_sz, f);
  fclose(f);
  printf("Written %zu bytes to %s\n", written, out_file_name);

  free(output); free(file_content);
}
