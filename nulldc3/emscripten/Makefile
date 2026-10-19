# The compiler and linker flags of the Emscripten port: a generic host (no recompiler, no fault handler, no sampling
# profiler) on the Linux platform sources, with EGL for the context and SDL for the audio.
# There are no rules here: the list of sources is the one of the CMake build.

RZDCY_SRC_DIR = ../../libswirl

CC_PREFIX ?= 

CXX=${CC_PREFIX}em++
CC=${CC_PREFIX}emcc
#AS=${CC_PREFIX}as
STRIP=${CC_PREFIX}emstrip

LD=${CC}

MFLAGS := 
ASFLAGS := 

# the BIOS, the flash, the disc and emu.cfg are packed into the page's file system
LDFLAGS	:= --preload-file data/dc_boot.bin --preload-file data/dc_flash.bin  --preload-file roms/ct.chd --preload-file emu.cfg -s TOTAL_MEMORY=536870912 -O3 -s USE_PTHREADS=1 -s USE_SDL=2 #-g2 #for debug


CXXONLYFLAGS := -std=c++14
CXXFLAGS := -s USE_PTHREADS=1 -s USE_SDL=2 -O3  --closure 1 -D RELEASE -c -D TARGET_EMSCRIPTEN -D TARGET_NO_REC -D TARGET_NO_EXCEPTIONS -D TARGET_NO_WEBUI -D TARGET_NO_COREIO_HTTP -D TARGET_NO_OPENMP -D SUPPORT_EGL -D FEAT_HAS_NIXPROF=0 
CXXFLAGS += -D USE_SDL_AUDIO=1
CXXFLAGS += #-g2 # for debug
CXXFLAGS += -fno-strict-aliasing
CXXFLAGS += -ffast-math

CXXFLAGS += $(CFLAGS) $(MFLAGS) -fexceptions -frtti


INCS	:= -I$(RZDCY_SRC_DIR) -I$(RZDCY_SRC_DIR)/deps -I$(RZDCY_SRC_DIR)/khronos -I../linux-deps/include

LIBS	:= #emscripten has all the basic libs, and egl, and sdl, built in

EXECUTABLE=nulldc3.html
