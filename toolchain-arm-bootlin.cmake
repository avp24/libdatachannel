set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT "/home/arjav/Projects/IP_Camera/Development/POC/packages/bootlin_toolchain/armv7-eabihf--uclibc--stable-2020.08-1")
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}/arm-buildroot-linux-uclibcgnueabihf/sysroot")

set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-g++")
set(CMAKE_AR "${TOOLCHAIN_ROOT}/bin/arm-linux-ar")
set(CMAKE_RANLIB "${TOOLCHAIN_ROOT}/bin/arm-linux-ranlib")
set(CMAKE_STRIP "${TOOLCHAIN_ROOT}/bin/arm-linux-strip")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
