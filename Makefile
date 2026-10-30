CC = clang
CFLAGS = -Wall -Wextra -Wno-unused-function
CFLAGS_DEBUG = -ggdb -fsanitize=undefined,address
CFLAGS_RELEASE = -O3 -DNDEBUG

ifeq ($(r),1)
	CFLAGS += $(CFLAGS_RELEASE)
else
	CFLAGS += $(CFLAGS_DEBUG)
endif

main: main.c gzip.c gzip.h
	$(CC) $(CFLAGS) -o main main.c gzip.c
