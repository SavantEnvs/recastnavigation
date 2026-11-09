#!/usr/bin/env bash
# Builds the Recast/Detour libraries with sanitizers + SanCov, the path-corridor fuzz harness (libFuzzer and
# standalone), and the upstream Catch2 suite and KAT probe with normal flags. Offline, re-runnable.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC="${SRC:-/mayhem}"
cd "$SRC"

FUZZ_BUILD="$SRC/mayhem/build-fuzz"
TEST_BUILD="$SRC/mayhem/build-tests"
OBJ="$FUZZ_BUILD/obj"
mkdir -p "$OBJ"

# 1) The libraries, instrumented for fuzzing: sanitizers, DWARF 3, and SanCov on the library code itself.
FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
CC="$CC" CXX="$CXX" cmake -S . -B "$FUZZ_BUILD" \
  -DCMAKE_C_FLAGS="$FUZZ_CFLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_CFLAGS" \
  -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=OFF -DRECASTNAVIGATION_EXAMPLES=OFF
cmake --build "$FUZZ_BUILD" -j"$MAYHEM_JOBS" --target Recast Detour DetourCrowd

LIBS="$FUZZ_BUILD/DetourCrowd/libDetourCrowd.a $FUZZ_BUILD/Detour/libDetour.a $FUZZ_BUILD/Recast/libRecast.a"
INC="-I$SRC/Recast/Include -I$SRC/Detour/Include -I$SRC/DetourCrowd/Include -I$SRC/DetourTileCache/Include \
-I$SRC/DebugUtils/Include -I$SRC/mayhem"
FUZZ_CXXFLAGS="-std=c++20 -fno-rtti -fno-exceptions $FUZZ_CFLAGS $INC"

# 2) The harness, linked twice: with libFuzzer, and with the run-once standalone driver.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

TARGETS="fuzz_path_corridor"
for t in $TARGETS; do
  $CXX $FUZZ_CXXFLAGS -c "$SRC/mayhem/$t.cpp" -o "$OBJ/$t.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OBJ/$t.o" "$OBJ/lsan_off.o" $LIBS -o "$SRC/$t"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OBJ/$t.o" "$OBJ/standalone_main.o" "$OBJ/lsan_off.o" $LIBS -o "$SRC/$t-standalone"
done

# 3) The upstream Catch2 suite and the KAT probe, with the project's normal flags (no sanitizers, no DWARF override).
CC="$CC" CXX="$CXX" cmake -S . -B "$TEST_BUILD" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=ON -DRECASTNAVIGATION_EXAMPLES=OFF
cmake --build "$TEST_BUILD" -j"$MAYHEM_JOBS"

TLIBS="$TEST_BUILD/DetourCrowd/libDetourCrowd.a $TEST_BUILD/Detour/libDetour.a $TEST_BUILD/Recast/libRecast.a"
$CXX -std=c++20 -fno-rtti -fno-exceptions -O1 $COVERAGE_FLAGS $INC \
  "$SRC/mayhem/kat_probe.cpp" $TLIBS -o "$TEST_BUILD/kat_probe"
