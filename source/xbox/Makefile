# Sonic R - original Xbox build (nxdk)
#
# Reuses the SDL2 desktop sources in ../sdl/src and swaps the platform pieces:
#   - SDL_mixer      -> include/SDL_mixer.h + src/xbox_mixer.c (minimal subset)
#   - window + GL    -> pbgl (OpenGL 1.x on the NV2A), via SONICR_XBOX in platform_sdl.c
#   - network play   -> nxdk's lwIP (src/xbox_net.c + include/ socket shims),
#                       matchmaker over BearSSL, UPnP, UDP transport
#
# Build:
#   eval "$(<path-to-nxdk>/bin/activate -s)"
#   make -C source/xbox NXDK_DIR=<path-to-nxdk>
#
# pbgl must be present at $(NXDK_DIR)/lib/pbgl (git clone https://github.com/Ryzee119/pbgl).

XBE_TITLE = Sonic\ R
GEN_XISO  = $(XBE_TITLE).iso
NXDK_DIR ?= $(CURDIR)/../../../nxdk
NXDK_SDL  = y
NXDK_NET  = y

SDL_SRC := $(abspath $(CURDIR)/../sdl/src)
XBOX_DIR := $(abspath $(CURDIR))

# Everything from the desktop port except what the Xbox build replaces or skips.
EXCLUDE := -path '$(SDL_SRC)/gles2/*' \
        -o -name 'r_soft_backend.c'

SRCS := $(shell find $(SDL_SRC) -name '*.c' ! \( $(EXCLUDE) \)) \
        $(XBOX_DIR)/src/xbox_mixer.c \
        $(XBOX_DIR)/src/xbox_net.c \
        $(XBOX_DIR)/src/xbox_entry.c \
        $(XBOX_DIR)/src/xbox_fastdraw.c \
        $(XBOX_DIR)/src/xbox_fastmem.c

# nxdk compiles its libraries (pdclib, pbkit, pbgl, SDL2, ...) with whatever
# CFLAGS the project passes, and with no -O at all that means clang's -O0.
# Optimise everything; the game objects add their own flags below.
CFLAGS   += -O2
CXXFLAGS += -O2

include $(NXDK_DIR)/Makefile
$(XBOX_DIR)/src/xbox_fastdraw.obj: CFLAGS += -I$(NXDK_DIR)/lib/pbgl/src
LDFLAGS += /MAP:$(XBOX_DIR)/main.map
include $(NXDK_DIR)/lib/pbgl/Makefile

# These flags are scoped to the game's own objects: the force-included compat
# header drops _WIN32, which would break nxdk's own SDL/libc sources.
GAME_OBJS := $(addsuffix .obj,$(basename $(SRCS)))
$(GAME_OBJS): CFLAGS += -std=gnu11 -O2 -fbuiltin -fno-math-errno -fno-common -w \
          -I$(SDL_SRC) -I$(XBOX_DIR)/include \
          -DSONICR_SDL -DSONICR_XBOX -DSONICR_DEBUG_LOG -DSDL_MAIN_HANDLED -DSONICR_MATCHMAKER \
          -DDATA_DIR=\"D:\" \
          -include $(XBOX_DIR)/include/xbox_compat.h

# BearSSL: the Xbox CPU (Pentium III) has no SSE2, AES-NI, PCLMUL or RDRAND,
# and clang's MSVC-style intrinsics headers do not build for it anyway.
BEARSSL_OBJS := $(filter $(SDL_SRC)/net/bearssl/%,$(GAME_OBJS))
$(BEARSSL_OBJS): CFLAGS += -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_RDRAND=0 -DBR_POWER8=0 -DBR_INT128=0 -DBR_UMUL128=0 \
                  -DBR_MSC=0 -DBR_CLANG=0 -DBR_GCC=0

# Must not let the compiler turn anything in here back into memcpy/memset calls.
$(XBOX_DIR)/src/xbox_fastmem.obj: CFLAGS += -fno-builtin

# The game's main() is called from src/xbox_entry.c with a fixed argv, because
# nxdk does not pass command-line arguments.
$(SDL_SRC)/main.obj: CFLAGS += -Dmain=sonicr_main
