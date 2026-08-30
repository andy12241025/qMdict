# Cross-compiles the Windows build from Linux with MinGW-w64.
#
# Qt's official win64_mingw packages are built with MinGW 13, so a matching
# host toolchain (Ubuntu's g++-mingw-w64-x86-64, currently 13.2) links against
# them cleanly. The POSIX threading variant is required: Qt uses std::thread.
#
# Pass MINGW_ROOT if the toolchain is not on PATH, e.g. an extracted
# .tools/mingw prefix.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)

if(DEFINED MINGW_ROOT)
    set(_bin "${MINGW_ROOT}/usr/bin/")
    list(APPEND CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}/usr/${_triple}")
else()
    set(_bin "")
endif()

set(CMAKE_C_COMPILER   "${_bin}${_triple}-gcc-posix")
set(CMAKE_CXX_COMPILER "${_bin}${_triple}-g++-posix")
set(CMAKE_RC_COMPILER  "${_bin}${_triple}-windres")
set(CMAKE_AR           "${_bin}${_triple}-ar")
set(CMAKE_RANLIB       "${_bin}${_triple}-ranlib")

# Look for libraries and headers in the target sysroot, but keep using host
# programs (moc, rcc and friends come from QT_HOST_PATH).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
