#!/bin/bash
set -e

CC=${CC:-g++}
CFLAGS="${CFLAGS} -x c++ -I src -Wno-writable-strings -Wno-write-strings"
LDFLAGS="${LDFLAGS} -lm"

BUILD_DIR="build"
BUILD_EXT=""
DATA_DIR="data"

SCRIPT_NAME="build.sh"

print_help() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTION]... [TARGETS]...

Options:
    --debug          Debug build (default)
    --release        Release build
    --relwithdebinfo RelWithDebInfo build
    --vulkan         Enable Vulkan support
    --trace          Enable Tracy profiling
    --help           Display this help and exit

Targets:
  all              Build all demos
  spheres          Build spheres demo
  tri              Build tri demo
  cornell          Build Cornell box demo
  bunny            Build stanford bunny demo

Environment variables:
    CC               C compiler to use (default: g++)
    CFLAGS           Additional compiler flags
    LDFLAGS          Additional linker flags
    VULKAN_SDK       Path to Vulkan SDK (optional)
EOF
}

print_info() {
    echo "- Build directory: $BUILD_DIR"
    echo "- Compiler:        $CC"
    if [[ -n "$release" ]]; then
    echo "- Mode:            Release"
    elif [[ -n "$relwithdebinfo" ]]; then
    echo "- Mode:            RelWithDebInfo"
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
    
    build_command="${CC} ${CFLAGS} ${main_file} ${LDFLAGS} -o ${BUILD_DIR}/${demo_name}${BUILD_EXT}"
    echo "${build_command}"
    eval ${build_command}
}

build_all_demos() {
    build_single_file spheres
    build_single_file tri
    build_single_file cornell
    build_single_file bunny
}

main() {
    # Parse options and filter flags
    actions=()
    for arg in "$@"; do
        case "$arg" in
            --release)          release=1;;
            --trace)            trace=1;;
            --debug)            debug=1;;
            --relwithdebinfo)   relwithdebinfo=1;;
            --vulkan)           vulkan=1;;
            --help)             print_help; exit 0;;
            *)                  actions+=("$arg");;
        esac
    done
    if ! [[ -v release || -v relwithdebinfo ]]; then debug=1; fi

    # Handle build mode
    if [[ -v release ]]; then
        CFLAGS="${CFLAGS} -s -O3 -DBUILD_DEBUG=0"
    elif [[ -v relwithdebinfo ]]; then
        CFLAGS="${CFLAGS} -g -O2 -DBUILD_DEBUG=1 -fno-omit-frame-pointer"
    elif [[ -v debug ]]; then
        CFLAGS="${CFLAGS} -g -O0 -DBUILD_DEBUG=1 -fno-omit-frame-pointer"
    fi

    if [[ -v vulkan ]]; then
        VULKAN_SDK_PATH="${VULKAN_SDK:-/usr/local/vulkan}"
        CFLAGS="${CFLAGS} -DVULKAN_ENABLED -I${VULKAN_SDK_PATH}/include"
        LDFLAGS="${LDFLAGS} -L${VULKAN_SDK_PATH}/lib -lvulkan"

        # Compile GLSL ray tracing shaders to SPIR-V.
        # Requires glslc (from the Vulkan SDK or shaderc) to be on PATH.
        SHADER_SRC_DIR="src/raytracer/vulkan/shaders"
        SHADER_OUT_DIR="${BUILD_DIR}/shaders"
        mkdir -p "${SHADER_OUT_DIR}"
        glslc --target-env=vulkan1.3 -fshader-stage=rgen  "${SHADER_SRC_DIR}/rgen.glsl"  -o "${SHADER_OUT_DIR}/rgen.spv"
        glslc --target-env=vulkan1.3 -fshader-stage=rchit "${SHADER_SRC_DIR}/rchit.glsl" -o "${SHADER_OUT_DIR}/rchit.spv"
        glslc --target-env=vulkan1.3 -fshader-stage=rmiss "${SHADER_SRC_DIR}/rmiss.glsl" -o "${SHADER_OUT_DIR}/rmiss.spv"
        echo "- Shaders compiled to ${SHADER_OUT_DIR}/"
    fi
    if [[ -v trace ]]; then
        CFLAGS="${CFLAGS} -DTRACY_ENABLE -DTRACY_DELAYED_INIT src/third_party/tracy/public/TracyClient.cpp"
    fi

    # Print build info
    print_info
    echo ""

    # Execute command
    for arg in "${actions}"; do
        case "$arg" in
            all)           build_all_demos;;
            spheres)       build_single_file spheres;;
            tri)           build_single_file tri;;
            cornell)       build_single_file cornell;;
            bunny)         build_single_file bunny;;
            "")            echo "Error: No command specified"
                           echo "Try '$SCRIPT_NAME --help' for more information."
                           exit 1;;
            *)             echo "Error: Unknown target '$arg'"
                           echo "Try '$SCRIPT_NAME --help' for more information."
                           exit 1;;
        esac
    done
}

main "$@"