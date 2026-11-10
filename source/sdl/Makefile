# Sonic R SDL2 build
# Pure C + SDL2 + OpenGL. Builds on macOS, Linux, and Windows (MSYS2/MinGW UCRT64).

SRCDIR = src
BUILDDIR = build

# Platform detection
UNAME_S  := $(shell uname -s)
IS_MINGW := $(filter MINGW% MSYS%, $(UNAME_S))

# Optional features. Override from the command line, e.g. `make SOFT=1`.

ifeq ($(UNAME_S),Darwin)
  CC = clang
  GL_CFLAGS = -DGL_SILENCE_DEPRECATION
  GL_LIBS = -framework OpenGL
  PLATFORM_CFLAGS = -Wno-pointer-bool-conversion
  SANITIZE = -fsanitize=address,undefined -fno-sanitize=shift
  EXTRA_LDFLAGS =
else ifneq ($(IS_MINGW),)
  CC ?= gcc
  GL_CFLAGS =
  GL_LIBS =
  PLATFORM_CFLAGS = -Wno-strict-aliasing -Wno-unused-result -Wno-misleading-indentation \
                     -Wno-array-bounds -Wno-attribute-warning -Wno-maybe-uninitialized \
                     -Wno-parentheses -Wno-type-limits -Wno-stringop-overflow \
                     -Wno-unused-but-set-variable -Wno-implicit-fallthrough \
                     -Wno-error=unknown-pragmas \
                     -D_USE_MATH_DEFINES
  SANITIZE =
  EXTRA_LDFLAGS = -static
else
  CC ?= gcc
  GL_CFLAGS =
  GL_LIBS = -lGL
  PLATFORM_CFLAGS = -Wno-strict-aliasing -Wno-unused-result -Wno-misleading-indentation \
                     -Wno-array-bounds -Wno-attribute-warning -Wno-maybe-uninitialized \
                     -Wno-parentheses -Wno-type-limits -Wno-stringop-overflow \
                     -Wno-unused-but-set-variable -Wno-implicit-fallthrough \
                     -D_GNU_SOURCE
  SANITIZE =
  EXTRA_LDFLAGS =
endif

# All C source files (recursive — picks up subdirectories automatically)
# Exclude GLES2 backend (selected via Makefile.emscripten or GLES2=1)
CSRC = $(shell find $(SRCDIR) -name '*.c' ! -path '$(SRCDIR)/gles2/*')

SDL_CFLAGS = $(shell sdl2-config --cflags)
ifneq ($(IS_MINGW),)
  # Static-link chain for MSYS2 UCRT64: explicit SDL2 + SDL2_mixer codec deps
  # + Win32 system libs. Order matters; opengl32 lives at the tail.
  SDL_LIBS = -lmingw32 -lSDL2main -lSDL2_mixer -lSDL2 \
             -lopusfile -lopus -logg \
             -lvorbisfile -lvorbis -logg \
             -lFLAC -lmpg123 -lxmp -lwavpack \
             -limm32 -lversion -lole32 -loleaut32 -lsetupapi \
             -lwinmm -lgdi32 -luser32 -lshell32 -lws2_32 \
             -lopengl32 -lshlwapi
else
  SDL_LIBS = $(shell sdl2-config --libs) -lSDL2_mixer
endif

# Matchmaker — always enabled (BearSSL for HTTPS + miniupnpc for UPnP).
# BearSSL is compiled from source (sdl/src/net/bearssl/), no external dep.
ifneq ($(IS_MINGW),)
  MATCHMAKER_CFLAGS = -DSONICR_MATCHMAKER -DMINIUPNP_STATICLIB
  MATCHMAKER_LIBS   = -lminiupnpc -lws2_32 -liphlpapi
else
  # miniupnpc lives outside the default compiler search path on macOS
  # (Homebrew) and on many Linux distros, so ask pkg-config for the real
  # -I/-L flags. Fall back to a bare -lminiupnpc if pkg-config can't see it.
  MINIUPNPC_PC := $(shell pkg-config --exists miniupnpc && echo yes)
  ifeq ($(MINIUPNPC_PC),yes)
    MATCHMAKER_CFLAGS = -DSONICR_MATCHMAKER $(shell pkg-config --cflags miniupnpc)
    MATCHMAKER_LIBS   = $(shell pkg-config --libs miniupnpc)
  else
    MATCHMAKER_CFLAGS = -DSONICR_MATCHMAKER
    MATCHMAKER_LIBS   = -lminiupnpc
  endif
endif

# Single-precision build: route sr_double → float. The actual DC build pairs
# this with `-m4-single-only` (sh-elf-gcc), which constrains code generation
# to single precision regardless of literal types. Host FLOAT32=1 builds are
# for type/wrapper-coverage testing only, not bit-exact DC simulation.
FLOAT32 ?= 0
ifeq ($(FLOAT32),1)
  FLOAT32_CFLAGS = -DSONICR_FLOAT32
else
  FLOAT32_CFLAGS =
endif

# Software wireframe backend: r_soft_backend.c replaces r_gl_backend.c and
# render_gl.c (they gate themselves out under the define). No header deps in
# this Makefile — run `make clean` when toggling.
SOFT ?= 0
ifeq ($(SOFT),1)
  SOFT_CFLAGS = -DSONICR_SOFT_RENDER
else
  SOFT_CFLAGS =
endif

#         -Wno-unused-variable -Wno-unused-function \
#         -Wno-unused-parameter \


CFLAGS = -I$(SRCDIR) -std=c11 -Os -g -Wall -Wextra -Werror \
         $(SANITIZE) -fno-common \
         -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast \
         -DSONICR_SDL -DSONICR_DEBUG_LOG \
         $(PLATFORM_CFLAGS) $(GL_CFLAGS) \
         $(SDL_CFLAGS) $(MATCHMAKER_CFLAGS) $(FLOAT32_CFLAGS) $(SOFT_CFLAGS)
LDFLAGS = $(SANITIZE) $(GL_LIBS) -lm $(SDL_LIBS) $(MATCHMAKER_LIBS) $(EXTRA_LDFLAGS)

# Object files
COBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(CSRC))
OBJS = $(COBJS)

TARGET = $(BUILDDIR)/sonicr

# Release build: no sanitizers, full optimization
RELEASE_BUILDDIR = build-release
RELEASE_CFLAGS = -I$(SRCDIR) -std=c11 -Os -g -Wall -Wextra \
                 -fno-common \
                 -Wno-unused-variable -Wno-unused-function \
                 -Wno-unused-parameter \
                 -DSONICR_SDL \
                         $(PLATFORM_CFLAGS) $(GL_CFLAGS) \
                 $(SDL_CFLAGS) $(MATCHMAKER_CFLAGS) $(FLOAT32_CFLAGS) $(SOFT_CFLAGS)
RELEASE_LDFLAGS = $(GL_LIBS) -lm $(SDL_LIBS) $(MATCHMAKER_LIBS) $(EXTRA_LDFLAGS)
RELEASE_OBJS = $(patsubst $(SRCDIR)/%.c, $(RELEASE_BUILDDIR)/%.o, $(CSRC))
RELEASE_TARGET = $(RELEASE_BUILDDIR)/sonicr

.PHONY: all clean release release-clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/excluded/%.o: $(SRCDIR)/excluded/%.c | $(BUILDDIR)/excluded
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/excluded:
	mkdir -p $(BUILDDIR)/excluded

clean:
	rm -rf $(BUILDDIR)

release: $(RELEASE_TARGET)

$(RELEASE_TARGET): $(RELEASE_OBJS)
	$(CC) -o $@ $^ $(RELEASE_LDFLAGS)

$(RELEASE_BUILDDIR)/excluded/%.o: $(SRCDIR)/excluded/%.c | $(RELEASE_BUILDDIR)/excluded
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

$(RELEASE_BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

$(RELEASE_BUILDDIR)/excluded:
	mkdir -p $(RELEASE_BUILDDIR)/excluded

release-clean:
	rm -rf $(RELEASE_BUILDDIR)
