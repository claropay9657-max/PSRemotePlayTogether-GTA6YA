
add_library(munit "${CMAKE_CURRENT_SOURCE_DIR}/munit/munit.c")
target_include_directories(munit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/munit")

# MinGW's tmpfile() fails in some Windows/MSYS environments. MUnit uses it
# only to buffer test stderr, so disable that optional capture on Windows.
if(WIN32)
	target_compile_definitions(munit PRIVATE MUNIT_NO_BUFFER)
endif()

set(CHIAKI_UNIT_SOURCES
				main.c
				http.c
				rpcrypt.c
				gkcrypt.c
				takion.c
				seqnum.c
				keystate.c
				feedback.c
				reorderqueue.c
				fec.c
				test_log.c
				test_log.h
				bitstream.c
				regist.c)

if(NOT CHIAKI_LIB_ENABLE_MBEDTLS)
	list(APPEND CHIAKI_UNIT_SOURCES aia.c)
endif()

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	list(APPEND CHIAKI_UNIT_SOURCES ffmpegdecoder.c)
endif()

add_executable(chiaki-unit ${CHIAKI_UNIT_SOURCES})
add_test(NAME unit COMMAND chiaki-unit)

# Mouse and keyboard as a controller: the part of the app that works it out, tried on its own
# (it needs neither Qt nor the stream library)
add_executable(keymouse-unit keymousepad.cpp "${CMAKE_SOURCE_DIR}/gui/src/keymousepad.cpp")
target_include_directories(keymouse-unit PRIVATE "${CMAKE_SOURCE_DIR}/gui/include")
set_target_properties(keymouse-unit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME keymouse COMMAND keymouse-unit)

# The app updating itself: which file, which addresses, the sum, the script for Windows, and
# the changelog taken apart (plain C++ as well)
add_executable(appupdate-unit appupdate.cpp "${CMAKE_SOURCE_DIR}/gui/src/appupdate.cpp")
target_include_directories(appupdate-unit PRIVATE "${CMAKE_SOURCE_DIR}/gui/include")
set_target_properties(appupdate-unit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME appupdate COMMAND appupdate-unit)

# Starting with the computer: the .desktop file, the Run value and the --tray flag they carry,
# read back as the desktop, Windows and the app read them (gui/src/autostart.cpp, with Qt Core)
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6Core_FOUND)
	add_executable(autostart-unit autostart.cpp "${CMAKE_SOURCE_DIR}/gui/src/autostart.cpp")
	target_include_directories(autostart-unit PRIVATE "${CMAKE_SOURCE_DIR}/gui/include")
	target_link_libraries(autostart-unit Qt6::Core)
	set_target_properties(autostart-unit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	add_test(NAME autostart COMMAND autostart-unit)
endif()

target_link_libraries(chiaki-unit chiaki-lib munit)

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	target_link_libraries(chiaki-unit FFMPEG::avcodec FFMPEG::avutil)
endif()
