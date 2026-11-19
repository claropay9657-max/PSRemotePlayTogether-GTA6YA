# CMake toolchain file: build for 64-bit Windows on Linux with llvm-mingw.
#
# The compiler and libraries are installed by scripts/ci/setup-windows-cross.sh
# into PSRPT_WIN_ROOT (default /opt/psrpt-win). Used by scripts/ci/build-windows.sh.

if(DEFINED ENV{PSRPT_WIN_ROOT} AND NOT "$ENV{PSRPT_WIN_ROOT}" STREQUAL "")
	set(PSRPT_WIN_ROOT "$ENV{PSRPT_WIN_ROOT}")
elseif(NOT PSRPT_WIN_ROOT)
	set(PSRPT_WIN_ROOT "/opt/psrpt-win")
endif()
# The small test projects CMake configures on the side must see the same root
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PSRPT_WIN_ROOT)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(_psrpt_triple x86_64-w64-mingw32)
set(_psrpt_bin "${PSRPT_WIN_ROOT}/llvm-mingw/bin")
set(CMAKE_C_COMPILER "${_psrpt_bin}/${_psrpt_triple}-clang")
set(CMAKE_CXX_COMPILER "${_psrpt_bin}/${_psrpt_triple}-clang++")
set(CMAKE_RC_COMPILER "${_psrpt_bin}/${_psrpt_triple}-windres")
set(CMAKE_AR "${_psrpt_bin}/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_psrpt_bin}/llvm-ranlib" CACHE FILEPATH "")

# Where headers, libraries and CMake packages for Windows are; nothing of the
# Linux system may be picked up for the target, but programs (python, protoc,
# pkg-config) are the build machine's.
set(CMAKE_FIND_ROOT_PATH
	"${PSRPT_WIN_ROOT}/ffmpeg"
	"${PSRPT_WIN_ROOT}/qt/target"
	"${PSRPT_WIN_ROOT}/msys2/clang64"
	"${PSRPT_WIN_ROOT}/llvm-mingw/${_psrpt_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt's code generators (moc, rcc, qmlcachegen ...) have to run on this machine
set(QT_HOST_PATH "${PSRPT_WIN_ROOT}/qt/host" CACHE PATH "")

# pkg-config must only describe the Windows libraries
set(ENV{PKG_CONFIG_LIBDIR} "${PSRPT_WIN_ROOT}/ffmpeg/lib/pkgconfig:${PSRPT_WIN_ROOT}/msys2/clang64/lib/pkgconfig:${PSRPT_WIN_ROOT}/msys2/clang64/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
