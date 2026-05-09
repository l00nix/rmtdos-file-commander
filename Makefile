# SPDX-License-Identifier: GPL-2.0-or-later

OUTDIR := out
BIN := $(OUTDIR)/rmtdos-file-commander

CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -g
CPPFLAGS += -Isrc
LDLIBS += $(shell $(PKG_CONFIG) ncursesw --libs)
CFLAGS += $(shell $(PKG_CONFIG) ncursesw --cflags)

SRC := \
	src/main.c \
	src/net/file_transfer.c \
	src/net/hostlist.c \
	src/net/raw_socket.c \
	src/ui/commander.c

OBJ := $(SRC:.c=.o)

.PHONY: all clean format

all: $(BIN)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(BIN): $(OUTDIR) $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ) $(LDLIBS)

clean:
	rm -rf $(OUTDIR) $(OBJ)

format:
	clang-format -i $(SRC) src/common/*.h src/net/*.h src/ui/*.h

