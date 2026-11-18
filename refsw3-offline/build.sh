#!/bin/bash
# Build refsw3-offline (see main.cpp): refsw3-core (refsw3/refcore) and refsw3-ta (refsw3/refta) without the emulator. The compiler settings are those of the emulator's RelWithDebInfo build,
# so that the renderer core gives the same numbers in both.
#   bash build.sh [output folder, default: this folder]
HERE=$(cd "$(dirname "$0")" && pwd)
N=$(dirname "$HERE")
OUT=${1:-$HERE}
mkdir -p "$OUT/obj"
FLAGS="-fno-operator-names -fpermissive -fno-strict-aliasing -msse4 -O2 -g -DNDEBUG -std=c++20 -w"
INC="-I$N -I$N/libswirl -I$N/libswirl/deps -I$N/libswirl/khronos -I$N/refsw3"
DEF="-DBUILD_COMPILER=0x30000002 -DCMAKE_BUILD -DDEF_CONSOLE -DFEAT_AREC=0x40000001 -DFEAT_SHREC=0x40000002 -DHOST_CPU=0x20000004 -DHOST_OS=0x10000002 -DTARGET_NO_OPENMP -DTARGET_NO_WEBUI -DREFSW_OFFLINE"
SRC="$HERE/main.cpp $N/refsw3/refcore/refsw_lists.cpp $N/refsw3/refcore/refsw_tile.cpp $N/refsw3/refta/refta.cpp $N/libswirl/hw/pvr/pvr_mem.cpp $N/libswirl/rend/TexCache.cpp $N/libswirl/oslib/posix/threading.cpp"
pids=""; objs=""
for s in $SRC; do
  o="$OUT/obj/$(basename "$s" .cpp).o"; objs="$objs $o"
  # only what changed: the object is older than its source or than a header of the renderer
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$(find "$N/refsw3" "$N/libswirl/hw/pvr" -name '*.h' -newer "$o" | head -1)" ]; then
    ( c++ $FLAGS $DEF $INC -c "$s" -o "$o" || rm -f "$o" ) &
    pids="$pids $!"
  fi
done
for p in $pids; do wait $p; done
for o in $objs; do [ -f "$o" ] || { echo "refsw3-offline: build failed"; exit 1; }; done
# link only when something changed, and put the new program in place in one step: other runs may be using it
new=""; for o in $objs; do [ "$o" -nt "$OUT/refsw3-offline" ] && new=1; done
if [ -n "$new" ] || [ ! -f "$OUT/refsw3-offline" ]; then
  c++ $objs -lpthread -o "$OUT/refsw3-offline.new.$$" && mv "$OUT/refsw3-offline.new.$$" "$OUT/refsw3-offline" && echo "built $OUT/refsw3-offline"
else
  echo "$OUT/refsw3-offline is up to date"
fi
