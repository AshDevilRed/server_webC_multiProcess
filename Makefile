CC = gcc
CPPFLAGS = -I../TPS -D_GNU_SOURCE
CFLAGS = -W -Wall -Wextra -Werror -std=c99 -O0 -fPIC -g

LD = gcc
LDFLAGS = -pie -rdynamic
LIBS = -lpthread -lutil

SRC = $(wildcard *.c)
MAIN_SRC = $(shell grep -lE $$'^int[[:space:]]+main[[:space:]]*\\\x28' $(SRC))
COMMON_SRC = $(filter-out $(MAIN_SRC),$(SRC))
COMMON_OBJ = $(COMMON_SRC:%.c=%.o)
OBJ = $(SRC:%.c=%.o)
BIN = $(MAIN_SRC:%.c=%.exe)

.PHONY: all clean

.SECONDARY: $(OBJ)

all: $(BIN)

%.exe: %.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

parse-ld-cache-static.exe: parse-ld-cache.o
	$(LD) -static $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(OBJ) $(BIN)
