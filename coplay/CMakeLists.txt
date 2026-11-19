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

add_library(coplay STATIC
	include/coplay/bytes.h
	include/coplay/client.h
	include/coplay/crypto.h
	include/coplay/host.h
	include/coplay/invite.h
	include/coplay/mixer.h
	include/coplay/protocol.h
	include/coplay/socket.h
	include/coplay/video.h
	src/client.cpp
	src/crypto.cpp
	src/host.cpp
	src/invite.cpp
	src/mixer.cpp
	src/protocol.cpp
	src/socket.cpp
	src/video.cpp
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
