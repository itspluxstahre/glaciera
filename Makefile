#
# Makefile for mp3berg
#

CC ?= clang
CFLAGS ?= -O3 -Wall -pedantic
CPPFLAGS ?= -I/opt/homebrew/include
CPPFLAGS += -I$(SRC_DIR)
LDFLAGS ?= -L/opt/homebrew/lib
STRIP ?= strip

SRC_DIR := src
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

MP3BERG_OBJS := $(OBJ_DIR)/mp3berg.o $(OBJ_DIR)/common.o $(OBJ_DIR)/git_version.o \
                $(OBJ_DIR)/mod_mp3.o $(OBJ_DIR)/mod_ogg.o $(OBJ_DIR)/mod_flac.o \
                $(OBJ_DIR)/mod_pls.o $(OBJ_DIR)/music.o
MP3BUILD_OBJS := $(OBJ_DIR)/mp3build.o $(OBJ_DIR)/common.o $(OBJ_DIR)/git_version.o \
                 $(OBJ_DIR)/mod_mp3.o $(OBJ_DIR)/mod_ogg.o $(OBJ_DIR)/mod_flac.o \
                 $(OBJ_DIR)/mod_pls.o $(OBJ_DIR)/music.o
SEARCHMP3BERG_OBJS := $(OBJ_DIR)/searchmp3berg.o $(OBJ_DIR)/common.o $(OBJ_DIR)/git_version.o

GIT_VERSION_SRC := $(SRC_DIR)/git_version.c

TARGETS := $(BIN_DIR)/mp3berg $(BIN_DIR)/mp3build $(BIN_DIR)/searchmp3berg.fcgi

.PHONY: all clean dist backup install mp3berg mp3build searchmp3berg.fcgi FORCE

all: $(TARGETS)

mp3berg: $(BIN_DIR)/mp3berg
mp3build: $(BIN_DIR)/mp3build
searchmp3berg.fcgi: $(BIN_DIR)/searchmp3berg.fcgi

$(BIN_DIR)/mp3berg: $(MP3BERG_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lncurses -lpthread -lvorbisfile -lvorbis -lFLAC -logg -lm
	$(STRIP) $@

$(BIN_DIR)/mp3build: $(MP3BUILD_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lpthread -lvorbisfile -lvorbis -lFLAC -logg -lm
	$(STRIP) $@

$(BIN_DIR)/searchmp3berg.fcgi: $(SEARCHMP3BERG_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lfcgi
	$(STRIP) $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(GIT_VERSION_SRC): FORCE
	@version=$$(git describe --tags --dirty --always 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	escaped=$$(printf '%s' "$$version" | sed 's/\\/\\\\/g; s/"/\\"/g'); \
	printf 'const char* git_version(void) { static const char* GIT_Version = "%s"; return GIT_Version; }\n' "$$escaped" > $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -f callgrind.out.*
	rm -f *~
	rm -f core.*

dist: $(BIN_DIR)/mp3build $(BIN_DIR)/mp3berg
	mkdir -p dist
	tar cjf dist/mp3berg-`date +%Y-%m-%d`.dist.i386.tar.bz $(BIN_DIR)/mp3build $(BIN_DIR)/mp3berg README rippers mp3bergrc

backup: clean
	tar cjf ../`date +%Y-%m-%d`src.tar.bz2 *

install: $(BIN_DIR)/mp3berg $(BIN_DIR)/mp3build
	install $(BIN_DIR)/mp3berg $(DESTDIR)/usr/bin
	install $(BIN_DIR)/mp3build $(DESTDIR)/usr/bin

debs: clean
	dpkg-buildpackage -rfakeroot -uc -b

FORCE:
