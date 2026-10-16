#!/bin/bash

set -euo pipefail

BUILD_TYPE=Debug
BUILD_VERBOSE=OFF
CLEAN_BUILD=0
ACTION=build
BUILD_TESTS=ON
RUN_TESTS=0
TEST_FILTER=""
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

usage() {
  cat <<USAGE
Usage: ./build.sh [options]
  -C, --clean     Wipe build/ and bin/ first then build (clean build)
  -R, --release   Release build (default: Debug)
  -V, --verbose   Show full compiler commands
  -d              Delete build/ and bin/ and exit
  -v              Only verify required files and exit
  -t, --test      Build then run the tests
  --filter=REGEX  Build then run only the tests matching REGEX (implies -t)
  --no-tests      Do not build the tests
USAGE
}

check_tools() {
  if ! command -v perl >/dev/null 2>&1; then
    echo "Perl is not installed, cannot verify required files"
    exit 1
  fi

  echo "Perl installed, checking required files..."
  if ! perl ./scripts/verify_files.pl; then
    exit 1
  fi
}

build_project() {
  if [ "$CLEAN_BUILD" -eq 1 ]; then
    cleanup
  fi

  local build_args="--parallel $JOBS"
  if [ "$BUILD_VERBOSE" = "ON" ]; then
    build_args="$build_args --verbose"
  fi

  echo "Building project (VERBOSE=${BUILD_VERBOSE}) (MODE=${BUILD_TYPE}) (TESTS=${BUILD_TESTS})"
  cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DMTS_BUILD_TESTS="${BUILD_TESTS}"
  cmake --build build $build_args
  echo "Creating bin directory..."
  mkdir -p bin
  echo "Copying final binary to bin directory..."
  cp build/app/mts_app bin/mts_app
  echo "Type ./bin/mts_app to run"
}

run_tests() {
  echo "Running tests..."
  if [ -n "$TEST_FILTER" ]; then
    ctest --test-dir build --output-on-failure -j "$JOBS" -R "$TEST_FILTER"
  else
    ctest --test-dir build --output-on-failure -j "$JOBS"
  fi
}

cleanup() {
  echo "Cleaning project..."
  echo "Removing build directory..."
  rm -rf build
  echo "Removing bin directory..."
  rm -rf bin
  echo "Cleaned up project"
}

for arg in "$@"; do
  case "$arg" in
    -C|--clean)
      CLEAN_BUILD=1
    ;;
    -V|--verbose)
      BUILD_VERBOSE=ON
    ;;
    -R|--release)
      BUILD_TYPE=Release
    ;;
    -t|--test)
      RUN_TESTS=1
    ;;
    --filter=*)
      TEST_FILTER="${arg#--filter=}"
      RUN_TESTS=1
    ;;
    --no-tests)
      BUILD_TESTS=OFF
    ;;
    -d)
      ACTION=clean
    ;;
    -v)
      ACTION=verify
    ;;
    -h|--help)
      usage;
      exit 0;
    ;;
    *)
      echo "Unknown option: $arg";
      usage;
      exit 1;
    ;;
  esac
done

if [ "$RUN_TESTS" -eq 1 ] && [ "$BUILD_TESTS" = "OFF" ]; then
  echo "--test/--filter and --no-tests cannot be combined"
  exit 1
fi

case "$ACTION" in
  clean)
    cleanup
  ;;
  verify)
    check_tools
  ;;
  build)
    check_tools
    build_project
    if [ "$RUN_TESTS" -eq 1 ]; then
      run_tests
    fi
  ;;
esac

