#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: build-opencv-cv5.sh [--project-root PATH]

Cross-build OpenCV for Hanwha Vision CV5 and copy the ARM64 artifacts into
the selected project's app/3rd_party directory.

Options:
  --project-root PATH
             Target project root. By default, use PROJECT_ROOT or the parent
             directory of the directory containing this script.
  -h, --help Show this help message.

Optional environment variables:
  PROJECT_ROOT       Overrides automatic project-root detection.
  OPENCV_SOURCE_DIR  Default: PROJECT_ROOT/opencv/sources
  TOOLCHAIN_FILE     Default: PROJECT_ROOT/opencv/cv5-toolchain.cmake
  OPENCV_BUILD_DIR   Default: /tmp/opencv-build-cv5-PROJECT_NAME
  OPENCV_INSTALL_DIR Default: /tmp/opencv-install-cv5-PROJECT_NAME
  CMAKE_BIN          Default: /opt/opensdk/toolchain/cmake-3.24.1/bin/cmake
  JOBS               Default: 2
EOF
}

log() {
    printf '\n==> %s\n' "$*"
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_root_arg=""

while (( $# > 0 )); do
    case "$1" in
        --project-root)
            (( $# >= 2 )) || die "--project-root requires a path."
            project_root_arg="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "Unknown option: $1"
            ;;
    esac
done

PROJECT_ROOT="${project_root_arg:-${PROJECT_ROOT:-$(dirname -- "${script_dir}")}}"
[[ -d "${PROJECT_ROOT}" ]] || die "Project root not found: ${PROJECT_ROOT}"
PROJECT_ROOT="$(cd -- "${PROJECT_ROOT}" && pwd -P)"
project_name="$(basename -- "${PROJECT_ROOT}")"
project_key="${project_name//[^a-zA-Z0-9_.-]/_}"

OPENCV_SOURCE_DIR="${OPENCV_SOURCE_DIR:-${PROJECT_ROOT}/opencv/sources}"
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-${PROJECT_ROOT}/opencv/cv5-toolchain.cmake}"
OPENCV_BUILD_DIR="${OPENCV_BUILD_DIR:-/tmp/opencv-build-cv5-${project_key}}"
OPENCV_INSTALL_DIR="${OPENCV_INSTALL_DIR:-/tmp/opencv-install-cv5-${project_key}}"
CMAKE_BIN="${CMAKE_BIN:-/opt/opensdk/toolchain/cmake-3.24.1/bin/cmake}"
JOBS="${JOBS:-2}"

APP_ROOT="${PROJECT_ROOT}/app"
THIRD_PARTY_ROOT="${APP_ROOT}/3rd_party"

[[ -x "${CMAKE_BIN}" ]] || die "CMake not found or not executable: ${CMAKE_BIN}"
[[ -f "${OPENCV_SOURCE_DIR}/CMakeLists.txt" ]] || \
    die "OpenCV sources not found: ${OPENCV_SOURCE_DIR}"
[[ -f "${TOOLCHAIN_FILE}" ]] || die "Toolchain file not found: ${TOOLCHAIN_FILE}"
[[ -d "${APP_ROOT}" ]] || die "Application directory not found: ${APP_ROOT}"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "JOBS must be a positive integer: ${JOBS}"
command -v file >/dev/null 2>&1 || die "The file command is required."

mkdir -p "${OPENCV_BUILD_DIR}" "${OPENCV_INSTALL_DIR}"

log "Project root: ${PROJECT_ROOT}"
log "Configuring OpenCV for CV5"
"${CMAKE_BIN}" \
    -S "${OPENCV_SOURCE_DIR}" \
    -B "${OPENCV_BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_INSTALL_PREFIX="${OPENCV_INSTALL_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_LIST=core,imgproc,imgcodecs \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_JAVA=OFF \
    -DBUILD_opencv_python3=OFF \
    -DWITH_IPP=OFF \
    -DWITH_ITT=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_GTK=OFF \
    -DWITH_QT=OFF \
    -DWITH_FFMPEG=OFF \
    -DWITH_GSTREAMER=OFF

log "Building OpenCV with ${JOBS} parallel jobs"
"${CMAKE_BIN}" --build "${OPENCV_BUILD_DIR}" --parallel "${JOBS}"

log "Installing OpenCV into ${OPENCV_INSTALL_DIR}"
"${CMAKE_BIN}" --install "${OPENCV_BUILD_DIR}"

shopt -s nullglob
opencv_core_versions=("${OPENCV_INSTALL_DIR}"/lib/libopencv_core.so.*.*.*)
shopt -u nullglob
(( ${#opencv_core_versions[@]} > 0 )) || \
    die "A fully versioned OpenCV core library was not installed."
opencv_core="${opencv_core_versions[0]}"

architecture="$(file "${opencv_core}")"
printf '%s\n' "${architecture}"
[[ "${architecture}" == *"ARM aarch64"* ]] || \
    die "OpenCV output is not an ARM aarch64 shared library."

log "Copying OpenCV headers and libraries into ${THIRD_PARTY_ROOT}"
mkdir -p "${THIRD_PARTY_ROOT}/include" "${THIRD_PARTY_ROOT}/lib"
cp -a \
    "${OPENCV_INSTALL_DIR}/include/opencv4/opencv2" \
    "${THIRD_PARTY_ROOT}/include/"

shopt -s nullglob
opencv_libraries=("${OPENCV_INSTALL_DIR}"/lib/libopencv_*.so*)
shopt -u nullglob
(( ${#opencv_libraries[@]} > 0 )) || die "No OpenCV shared libraries were installed."

# VirtualBox shared folders commonly reject symbolic links. Dereference each
# versioned OpenCV symlink and store it as a regular file in the shared project.
cp -L "${opencv_libraries[@]}" "${THIRD_PARTY_ROOT}/lib/"

[[ -f "${THIRD_PARTY_ROOT}/include/opencv2/opencv.hpp" ]] || \
    die "OpenCV header copy failed."
[[ -f "${THIRD_PARTY_ROOT}/lib/libopencv_core.so" ]] || \
    die "OpenCV library copy failed."

log "CV5 OpenCV build and project copy completed successfully"
printf '%s\n' \
    'Exit the container, move to the project root on the host, and run:' \
    "  APP_NAME=${APP_NAME:-${project_name}} SDK_VER=${SDK_VER:-26.05.19} SOC=${SOC:-cv5} docker compose up"
