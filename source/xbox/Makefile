# Sonic R - original Xbox build (nxdk)
#
# Reuses the SDL2 desktop sources in ../sdl/src and swaps the platform pieces:
#   - SDL_mixer      -> include/SDL_mixer.h + src/xbox_mixer.c (minimal subset)
#   - window + GL    -> pbgl (OpenGL 1.x on the NV2A), via SONICR_XBOX in platform_sdl.c
#   - network play   -> left out for now (no matchmaker, no BearSSL/UPnP)
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

SDL_SRC := $(abspath $(CURDIR)/../sdl/src)
XBOX_DIR := $(abspath $(CURDIR))

# Everything from the desktop port except what the Xbox build replaces or skips.
EXCLUDE := -path '$(SDL_SRC)/net/bearssl/*' \
        -o -path '$(SDL_SRC)/gles2/*' \
        -o -name 'r_soft_backend.c' \
        -o -name 'net_upnp.c' \
        -o -name 'net_transport.c'

SRCS := $(shell find $(SDL_SRC) -name '*.c' ! \( $(EXCLUDE) \)) \
        $(XBOX_DIR)/src/xbox_mixer.c \
        $(XBOX_DIR)/src/xbox_net_stub.c \
        $(XBOX_DIR)/src/xbox_entry.c

include $(NXDK_DIR)/Makefile
LDFLAGS += /MAP:$(XBOX_DIR)/main.map
include $(NXDK_DIR)/lib/pbgl/Makefile

# These flags are scoped to the game's own objects: the force-included compat
# header drops _WIN32, which would break nxdk's own SDL/libc sources.
GAME_OBJS := $(addsuffix .obj,$(basename $(SRCS)))
$(GAME_OBJS): CFLAGS += -std=gnu11 -Os -fno-common -w \
          -I$(SDL_SRC) -I$(XBOX_DIR)/include \
          -DSONICR_SDL -DSONICR_XBOX -DSONICR_DEBUG_LOG -DSDL_MAIN_HANDLED \
          -DDATA_DIR=\"D:\" \
          -include $(XBOX_DIR)/include/xbox_compat.h

# The game's main() is called from src/xbox_entry.c with a fixed argv, because
# nxdk does not pass command-line arguments.
$(SDL_SRC)/main.obj: CFLAGS += -Dmain=sonicr_main
