set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CV5_TOOLCHAIN
    "/opt/opensdk/toolchain/cortex-a76-2022.08-gcc12.1-linux5.15")

set(CMAKE_C_COMPILER
    "${CV5_TOOLCHAIN}/bin/aarch64-linux-gnu-gcc")

set(CMAKE_CXX_COMPILER
    "${CV5_TOOLCHAIN}/bin/aarch64-linux-gnu-g++")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)