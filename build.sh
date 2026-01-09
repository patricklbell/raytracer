#!/bin/bash
set -e

CC=${CC:-g++}
CFLAGS="${CFLAGS} -x c++ -I src -Wno-writable-strings -Wno-write-strings"
LDFLAGS="${LDFLAGS} -lm"
LDFLAGS_GFX="-lX11 -lXext"

BUILD_DIR="build"
BUILD_EXT=""
DATA_DIR="data"

SCRIPT_NAME="build.sh"

print_help() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTION]... [TARGETS]...

Options:
  --debug         Debug build (default)
  --release       Release build
  --trace         Enable Tracy profiling
  --help          Display this help and exit

Targets:
  spheres         Build spheres demo

Environment variables:
  CC              C compiler to use (default: g++)
  CFLAGS          Additional compiler flags
  LDFLAGS         Additional linker flags
EOF
}

print_info() {
    echo "- Build directory: $BUILD_DIR"
    echo "- Compiler:        $CC"
    if [[ -n "$release" ]]; then
    echo "- Mode:            Release"
    else
    echo "- Mode:            Debug"
    fi
    if [[ -v trace ]]; then
    echo "- Tracy:           Enabled"
    else
    echo "- Tracy:           Disabled"
    fi
}

build_single_file() {
    mkdir -p "${BUILD_DIR}"
    
    local demo_name="$1"
    local main_file="src/demos/${demo_name}/main.c"
    
    build_command="${CC} ${CFLAGS} ${main_file} ${LDFLAGS} ${LDFLAGS_GFX} -o ${BUILD_DIR}/${demo_name}${BUILD_EXT}"
    echo "${build_command}"
    eval ${build_command}
}

build_all_demos() {
    build_single_file spheres
}

main() {
    # Parse options and filter flags
    actions=()
    for arg in "$@"; do
        case "$arg" in
            --release)   release=1;;
            --trace)     trace=1;;
            --debug)     debug=1;;
            --help)      print_help; exit 0;;
            *)           actions+=("$arg");;
        esac
    done
    if ! [[ -v release ]]; then debug=1; fi

    # Handle build mode
    if [[ -v release ]]; then
        CFLAGS="${CFLAGS} -s -O3 -DBUILD_DEBUG=0"
    elif [[ -v debug ]]; then
        CFLAGS="${CFLAGS} -g -O0 -DBUILD_DEBUG=1 -fno-omit-frame-pointer"
    fi
    if [[ -v trace ]]; then
        CFLAGS="${CFLAGS} -DTRACY_ENABLE src/third_party/tracy/public/TracyClient.cpp"
    fi

    # Print build info
    print_info
    echo ""

    # Execute command
    for arg in "${actions}"; do
        case "$arg" in
            all)           build_all_demos;;
            spheres)       build_single_file spheres;;
            "")            echo "Error: No command specified"
                           echo "Try '$SCRIPT_NAME --help' for more information."
                           exit 1;;
            *)             echo "Error: Unknown command '$arg'"
                           echo "Try '$SCRIPT_NAME --help' for more information."
                           exit 1;;
        esac
    done
}

main "$@"