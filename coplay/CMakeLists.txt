# Co-play: several people share one Remote Play session.
#
# This library has no dependency on the rest of the project (no Qt, no chiaki
# lib), only on a C++17 compiler and OpenSSL's libcrypto. It can also be built
# on its own:   cmake -S coplay -B build-coplay && cmake --build build-coplay

cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(coplay CXX)
	set(COPLAY_STANDALONE ON)
else()
	set(COPLAY_STANDALONE OFF)
endif()

option(COPLAY_ENABLE_TESTS "Build the co-play tests" ON)
option(COPLAY_ENABLE_SIM "Build coplay-sim, a simulated console for testing without a PlayStation" ON)
option(COPLAY_ENABLE_WEB "Let friends join from a web browser (downloads and builds libdatachannel)" ON)
set(COPLAY_LIBDATACHANNEL_DIR "" CACHE PATH "A libdatachannel source tree to build instead of downloading one")

add_library(coplay STATIC
	include/coplay/blob.h
	include/coplay/bytes.h
	include/coplay/client.h
	include/coplay/crypto.h
	include/coplay/host.h
	include/coplay/invite.h
	include/coplay/media.h
	include/coplay/meet.h
	include/coplay/mixer.h
	include/coplay/protocol.h
	include/coplay/socket.h
	include/coplay/stun.h
	include/coplay/video.h
	include/coplay/web.h
	src/blob.cpp
	src/client.cpp
	src/crypto.cpp
	src/host.cpp
	src/invite.cpp
	src/media.cpp
	src/meet.cpp
	src/mixer.cpp
	src/protocol.cpp
	src/socket.cpp
	src/stun.cpp
	src/video.cpp
	src/web.cpp
	src/webrtc.cpp
)
target_compile_features(coplay PUBLIC cxx_std_17)
set_target_properties(coplay PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(coplay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
	target_compile_options(coplay PRIVATE /W3)
else()
	target_compile_options(coplay PRIVATE -Wall -Wextra)
endif()

if(TARGET OpenSSL_Crypto)
	# OpenSSL built as part of the project (CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT)
	target_link_libraries(coplay PUBLIC OpenSSL_Crypto)
else()
	find_package(OpenSSL REQUIRED)
	target_link_libraries(coplay PUBLIC OpenSSL::Crypto)
endif()

find_package(Threads REQUIRED)
target_link_libraries(coplay PUBLIC Threads::Threads)
if(WIN32)
	target_link_libraries(coplay PUBLIC ws2_32)
endif()

# Guests in a web browser reach the host over WebRTC. The library for that is
# libdatachannel (MPL 2.0) with its own copies of libjuice and usrsctp, built
# into the app; nothing of it has to be installed. The commit is pinned, and
# only what data channels need is built (no media transport, no WebSocket).
if(COPLAY_ENABLE_WEB)
	include(FetchContent)
	if(COPLAY_LIBDATACHANNEL_DIR)
		set(FETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL "${COPLAY_LIBDATACHANNEL_DIR}" CACHE PATH "" FORCE)
	endif()
	set(NO_MEDIA ON CACHE BOOL "" FORCE)
	set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
	set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
	set(NO_TESTS ON CACHE BOOL "" FORCE)
	set(COPLAY_LIBDATACHANNEL_SOURCE
		GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
		GIT_TAG 6b1e2e620f1e37f0eafeee702eaea0043cb305fd # v0.24.6
		GIT_SUBMODULES deps/plog deps/usrsctp deps/libjuice
		GIT_SUBMODULES_RECURSE OFF)
	# Static, and out of "all" and "install": only what the app links is built
	set(COPLAY_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
	set(BUILD_SHARED_LIBS OFF)
	if(CMAKE_VERSION VERSION_LESS 3.28)
		FetchContent_Declare(libdatachannel ${COPLAY_LIBDATACHANNEL_SOURCE})
		FetchContent_GetProperties(libdatachannel)
		if(NOT libdatachannel_POPULATED)
			FetchContent_Populate(libdatachannel)
			add_subdirectory("${libdatachannel_SOURCE_DIR}" "${libdatachannel_BINARY_DIR}" EXCLUDE_FROM_ALL)
		endif()
	else()
		FetchContent_Declare(libdatachannel ${COPLAY_LIBDATACHANNEL_SOURCE} EXCLUDE_FROM_ALL)
		FetchContent_MakeAvailable(libdatachannel)
	endif()
	set(BUILD_SHARED_LIBS "${COPLAY_SAVED_BUILD_SHARED_LIBS}")
	# This version of libdatachannel uses std::transform in a file that does not include
	# <algorithm>, which newer standard libraries (llvm-mingw's libc++) no longer bring along
	if(MSVC)
		target_compile_options(datachannel-static PRIVATE /FIalgorithm)
	else()
		target_compile_options(datachannel-static PRIVATE -include algorithm)
	endif()
	target_link_libraries(coplay PRIVATE LibDataChannel::LibDataChannelStatic)
	target_compile_definitions(coplay PRIVATE COPLAY_WEB)
	message(STATUS "Co-play: guests can join from a web browser (libdatachannel)")
else()
	message(STATUS "Co-play: built without support for guests in a web browser")
endif()

if(COPLAY_ENABLE_TESTS)
	add_executable(coplay-tests test/tests.cpp)
	target_link_libraries(coplay-tests coplay)
	if(COPLAY_STANDALONE)
		enable_testing()
	endif()
	add_test(NAME coplay COMMAND coplay-tests)
endif()

if(COPLAY_ENABLE_SIM)
	add_executable(coplay-sim tools/sim.cpp)
	target_link_libraries(coplay-sim coplay)
	# With Opus the simulated console also sends sound
	find_package(Opus QUIET)
	if(Opus_FOUND)
		target_include_directories(coplay-sim PRIVATE ${Opus_INCLUDE_DIRS})
		target_link_libraries(coplay-sim ${Opus_LIBRARIES})
		target_compile_definitions(coplay-sim PRIVATE COPLAY_SIM_OPUS)
	endif()
endif()
