
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(APPLE)
	enable_language(OBJCXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Concurrent Network Svg Qml Quick Widgets)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.10.0")
	find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

find_package(Qt6 COMPONENTS WebEngineQuick)
# Webcams in Play Together. Without it the app still shows the other players' webcams.
find_package(Qt6 COMPONENTS Multimedia)
find_package(Qt6 COMPONENTS DBus)

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCE_FILES
	include/exception.h
	src/main.cpp
	include/discoverymanager.h
	src/discoverymanager.cpp
	include/streamsession.h
	src/streamsession.cpp
	src/streamsession_coplay.cpp
	src/streamsession_pc.cpp
	include/sessionlog.h
	src/sessionlog.cpp
	include/settings.h
	src/settings.cpp
	include/host.h
	src/host.cpp
	res/resources.qrc
	res/overlay/overlay.qrc
	include/overlayserver.h
	src/overlayserver.cpp
	include/controllermanager.h
	src/controllermanager.cpp
	include/psnaccountid.h
	src/psnaccountid.cpp
	include/psntoken.h
	src/psntoken.cpp
	include/jsonrequester.h
	src/jsonrequester.cpp
	src/qml/qml.qrc
	include/qmlmainwindow.h
	src/qmlmainwindow.cpp
	include/qmlbackend.h
	src/qmlbackend.cpp
	include/gta6yabridge.h
	src/gta6yabridge.cpp
	include/qmlcontroller.h
	src/qmlcontroller.cpp
	include/qmlsettings.h
	src/qmlsettings.cpp
	src/qmlsettings_coplay.cpp
	src/qmlbackend_coplay.cpp
	src/qmlbackend_display.cpp
	src/qmlbackend_update.cpp
	include/presetsettings.h
	src/presetsettings.cpp
	include/voicechat.h
	src/voicechat.cpp
	include/devicecheck.h
	src/devicecheck.cpp
	include/appupdate.h
	src/appupdate.cpp
	include/appupdater.h
	src/appupdater.cpp
	include/keymousepad.h
	src/keymousepad.cpp
	include/keymouse.h
	src/keymouse.cpp
	include/introsound.h
	src/introsound.cpp
	include/livestream.h
	src/livestream.cpp
	include/livecontroller.h
	src/livecontroller.cpp
	include/keystore.h
	src/keystore.cpp
	include/friends.h
	src/friends.cpp
	include/tray.h
	src/tray.cpp
	include/autostart.h
	src/autostart.cpp
	include/chattext.h
	src/chattext.cpp
	include/livescenes.h
	src/livescenes.cpp
	include/websocketclient.h
	src/websocketclient.cpp
	include/twitch.h
	src/twitch.cpp
	include/livealerts.h
	src/livealerts.cpp
	include/twitchelements.h
	include/vieweractions.h
	src/twitchelements.cpp
	src/vieweractions.cpp
	include/sounddecoder.h
	src/sounddecoder.cpp
	include/scenemusic.h
	src/scenemusic.cpp
	${CMAKE_SOURCE_DIR}/third-party/qrcodegen/qrcodegen.cpp
	include/coplaycamera.h
	src/coplaycamera.cpp
	include/coplayaddress.h
	include/coplaymeet.h
	include/coplayrooms.h
	include/coplayrecent.h
	include/coplaylinkpreview.h
	src/coplaylinkpreview.cpp
	src/streamsession_chat.cpp
	src/coplayaddress.cpp
	src/coplaymeet.cpp
	src/coplayrooms.cpp
	src/coplayrecent.cpp
	include/qmlsvgprovider.h
	src/qmlsvgprovider.cpp
	include/systemdinhibit.h
	src/systemdinhibit.cpp
	include/pcsource.h
	include/pcsource_p.h
	src/pcsource.cpp
	src/pcdevice.cpp
	include/pcscale.h
	src/pcscale.cpp
	include/keymap.h
	src/keymap.cpp
	include/pcinput.h
	src/pcinput.cpp
	include/virtualpads.h
	src/virtualpads.cpp
	)
set(RESOURCE_FILES "")

if(APPLE)
	list(APPEND RESOURCE_FILES "chiaking.icns")
elseif(WIN32)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/chiaking.rc.in
		${CMAKE_CURRENT_SOURCE_DIR}/chiaking.rc
		@ONLY)
	list(APPEND RESOURCE_FILES "chiaking.rc")
endif()

if(CHIAKI_ENABLE_STEAMDECK_NATIVE AND LINUX)
	list(APPEND SOURCE_FILES
		include/sdinputcontext.h
		src/sdinputcontext.cpp
	)
endif()

if(APPLE)
	list(APPEND SOURCE_FILES
		include/macMicPermission.h
		src/macMicPermission.m
		include/macWakeSleep.h
		src/macWakeSleep.cpp
		include/macpermissions.h
		src/macpermissions.mm
		include/macmouse.h
		src/macmouse.mm
	)
	set_source_files_properties(src/macpermissions.mm src/macmouse.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
	list(APPEND SOURCE_FILES include/macpermissions.h src/macpermissions.cpp)
endif()

if(APPLE)
elseif(WIN32)
	list(APPEND SOURCE_FILES
		include/windowsWakeSleep.h
		src/windowsWakeSleep.cpp
	)
endif()

add_executable(chiaki WIN32
		${RESOURCE_FILES}
		${SOURCE_FILES}
)
target_include_directories(chiaki PRIVATE include)
# The app's id at Twitch (a public client: no secret). Registered by the project on dev.twitch.tv;
# without one, logging in to Twitch is off. Builds of your own can set theirs.
# The project's app at Twitch (a public client: the id is not a secret, every copy carries it).
# Forks may register their own at dev.twitch.tv and set it here or with -DPSRPT_TWITCH_CLIENT_ID=...
set(PSRPT_TWITCH_CLIENT_ID "r6c2otmrwul0sevqxk9nb9fzmnlkwk" CACHE STRING "Client id of the app at Twitch (dev.twitch.tv), for logging in")
target_compile_definitions(chiaki PRIVATE PSRPT_TWITCH_CLIENT_ID="${PSRPT_TWITCH_CLIENT_ID}")
# Nayuki's QR code generator (MIT): the code to the room on the Play with us scene
target_include_directories(chiaki PRIVATE ${CMAKE_SOURCE_DIR}/third-party/qrcodegen)

add_library(placebo-libav-impl src/libav_impl.c)
target_link_libraries(placebo-libav-impl PkgConfig::LIBPLACEBO FFMPEG::avcodec FFMPEG::avutil FFMPEG::avformat)
target_link_libraries(chiaki PkgConfig::LIBPLACEBO placebo-libav-impl)

target_link_libraries(chiaki chiaki-lib)
target_link_libraries(chiaki coplay)

# This computer as the game source (docs/pc-source.md): the sound goes out as Opus
find_package(Opus REQUIRED)
target_include_directories(chiaki PRIVATE ${Opus_INCLUDE_DIRS})
target_link_libraries(chiaki ${Opus_LIBRARIES})
if(WIN32)
	# A window or a screen through FFmpeg's capture filters (Windows Graphics Capture)
	find_package(FFMPEG COMPONENTS avfilter)
	if(TARGET FFMPEG::avfilter)
		target_link_libraries(chiaki FFMPEG::avfilter)
		target_compile_definitions(chiaki PRIVATE PSRPT_PC_CAPTURE_FILTERS=1)
		message(STATUS "Sharing a game on this computer: capture through FFmpeg ${FFMPEG_VERSION}")
	else()
		message(WARNING "FFmpeg has no libavfilter: sharing a game on this computer is off")
	endif()
	# Windows, the program's name and its sound
	target_link_libraries(chiaki dwmapi ole32 version)
	# The players' controllers as Xbox 360 controllers of the ViGEmBus driver: ViGEmClient
	# (MIT), the library that talks to it, at a fixed commit (the project is archived)
	include(FetchContent)
	FetchContent_Declare(vigemclient
		URL https://github.com/nefarius/ViGEmClient/archive/b66d02d57e32cc8595369c53418b843e958649b4.tar.gz
		URL_HASH SHA256=c2bc824cc96ed18cf0620b734ae552474f107e7e680485e5c0cb63e675d9f72a)
	FetchContent_GetProperties(vigemclient)
	if(NOT vigemclient_POPULATED)
		# Only its sources: its own CMakeLists.txt builds a resource file and installs
		FetchContent_Populate(vigemclient)
	endif()
	add_library(vigemclient STATIC ${vigemclient_SOURCE_DIR}/src/ViGEmClient.cpp)
	target_include_directories(vigemclient PUBLIC ${vigemclient_SOURCE_DIR}/include PRIVATE ${vigemclient_SOURCE_DIR}/src)
	# It names Windows' headers as the Windows SDK spells them; mingw-w64's are lower case,
	# which only matters on a case-sensitive file system (cross-compiling on Linux)
	if(NOT CMAKE_HOST_WIN32)
		target_include_directories(vigemclient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/vigem-compat)
	endif()
	target_compile_definitions(vigemclient PRIVATE WIN32_LEAN_AND_MEAN)
	set_target_properties(vigemclient PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	if(NOT MSVC)
		target_compile_options(vigemclient PRIVATE -Wno-pragma-pack)
	endif()
	target_link_libraries(vigemclient PUBLIC setupapi)
	target_link_libraries(chiaki vigemclient)
	# The charge of Xbox controllers over Bluetooth, which XInput does not give
	target_sources(chiaki PRIVATE src/padbattery_win.cpp)
	target_link_libraries(chiaki cfgmgr32)
	target_compile_definitions(chiaki PRIVATE PSRPT_VIGEM=1)
elseif(APPLE)
	# A window or a screen and its sound through ScreenCaptureKit (macOS 13 and later)
	target_sources(chiaki PRIVATE src/pcsource_mac.mm src/pcinput_mac.mm)
	set_source_files_properties(src/pcsource_mac.mm src/pcinput_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
	target_link_libraries(chiaki "-framework ScreenCaptureKit" "-framework CoreMedia" "-framework CoreVideo"
		"-framework CoreGraphics" "-framework AppKit" "-framework ApplicationServices")
	target_compile_definitions(chiaki PRIVATE PSRPT_PC_SCREENCAPTUREKIT=1)
	message(STATUS "Sharing a game on this computer: ScreenCaptureKit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# The system's screen sharing (xdg-desktop-portal over D-Bus) and PipeWire, which is opened
	# when needed (only its headers here); the pictures are scaled by pcscale.cpp
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PIPEWIRE libpipewire-0.3)
	if(PIPEWIRE_FOUND AND Qt6DBus_FOUND)
		target_sources(chiaki PRIVATE src/pcsource_linux.cpp)
		target_include_directories(chiaki PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
		target_link_libraries(chiaki ${CMAKE_DL_LIBS})
		target_compile_definitions(chiaki PRIVATE PSRPT_PC_PIPEWIRE=1)
		message(STATUS "Sharing a game on this computer: PipeWire ${PIPEWIRE_VERSION} headers")
	else()
		message(WARNING "No PipeWire headers (libpipewire-0.3-dev) or Qt D-Bus: sharing a game on this computer is off")
	endif()
endif()
if(CHIAKI_ENABLE_CLI)
	add_definitions(-DCHIAKI_ENABLE_CLI)
	target_link_libraries(chiaki chiaki-cli-lib)
endif()

target_link_libraries(chiaki Qt::Core Qt::Gui Qt::GuiPrivate Qt::Concurrent Qt::Network Qt::Svg Qt::Qml Qt::Quick Qt::Widgets)
if (${Qt6DBus_FOUND} AND NOT WIN32)
	target_link_libraries(chiaki Qt::DBus)
if (NOT APPLE)
	target_compile_definitions(chiaki PRIVATE CHIAKI_HAVE_DBUS)
endif()
endif()
if (${Qt6Multimedia_FOUND})
	target_link_libraries(chiaki Qt::Multimedia)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_CAMERA=1 CHIAKI_HAVE_MULTIMEDIA=1)
	message(STATUS "Play Together webcam capture enabled")
else()
	message(STATUS "Play Together webcam capture disabled (Qt Multimedia not found)")
endif()
# The players' voice chat encodes and decodes Opus itself
find_package(Opus REQUIRED)
target_include_directories(chiaki PRIVATE ${Opus_INCLUDE_DIRS})
target_link_libraries(chiaki ${Opus_LIBRARIES})
if (${Qt6WebEngineQuick_FOUND})
	target_link_libraries(chiaki Qt::WebEngineQuick)
	target_compile_definitions(chiaki PRIVATE CHIAKI_HAVE_WEBENGINE)
endif()
target_link_libraries(chiaki SDL2::SDL2)
if(CHIAKI_ENABLE_SPEEX)
  target_link_libraries(chiaki PkgConfig::SpeexDSP)
  target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SPEEX)
endif()
if(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
endif()
if(CHIAKI_ENABLE_SETSU)
	target_link_libraries(chiaki setsu)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SETSU)
endif()
if(CHIAKI_ENABLE_STEAMDECK_NATIVE)
	target_link_libraries(chiaki sdeck)
	target_compile_definitions(chiaki PRIVATE QT_STATICPLUGIN CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE)
endif()
if(CHIAKI_ENABLE_STEAM_SHORTCUT)
	target_link_libraries(chiaki cpp-steam-tools)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_STEAM_SHORTCUT)
endif()
if(NOT CHIAKI_ENABLE_FFMPEG_DECODER)
	message(FATAL_ERROR "Chiaki GUI requires CHIAKI_ENABLE_FFMPEG_DECODER=ON")
endif()
if(APPLE)
	# Security: the stream key in the keychain (keystore.cpp)
	target_link_libraries(chiaki "-framework AVFoundation" "-framework IOKit" "-framework Security" "-framework CoreFoundation")
elseif(WIN32)
	# advapi32: the stream key in the Credential Manager (keystore.cpp)
	target_link_libraries(chiaki advapi32)
else()
	# libsecret is opened when needed (keystore.cpp)
	target_link_libraries(chiaki ${CMAKE_DL_LIBS})
endif()
# Starting with the computer on a Mac: the login item (ServiceManagement, macOS 13 and later),
# how the app was opened, and its Dock icon while it sits in the menu bar (autostart_mac.mm)
if(APPLE)
	target_sources(chiaki PRIVATE src/autostart_mac.mm)
	set_source_files_properties(src/autostart_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
	target_link_libraries(chiaki "-framework ServiceManagement")
endif()

# Friends end to end: clients of the app against a friends server (test/friends-client.sh
# starts one with php -S); not part of ctest, it needs PHP
if(COPLAY_ENABLE_TESTS)
	add_executable(friends-client-test "${CMAKE_SOURCE_DIR}/test/friends-client.cpp" include/friends.h src/friends.cpp include/keystore.h src/keystore.cpp
		include/chattext.h src/chattext.cpp include/coplaylinkpreview.h src/coplaylinkpreview.cpp)
	target_include_directories(friends-client-test PRIVATE include)
	target_link_libraries(friends-client-test coplay Qt::Core Qt::Gui Qt::Network)
	if(APPLE)
		target_link_libraries(friends-client-test "-framework Security" "-framework CoreFoundation")
	elseif(WIN32)
		target_link_libraries(friends-client-test advapi32)
	else()
		target_link_libraries(friends-client-test ${CMAKE_DL_LIBS})
	endif()
	# Pictures of the friends panel with the app's QML, without the app (test/friends-shot.sh)
	add_executable(friends-shot "${CMAKE_SOURCE_DIR}/test/friends-shot.cpp" include/friends.h src/friends.cpp include/keystore.h src/keystore.cpp
		include/chattext.h src/chattext.cpp include/coplaylinkpreview.h src/coplaylinkpreview.cpp src/qml/qml.qrc res/resources.qrc)
	target_include_directories(friends-shot PRIVATE include)
	target_link_libraries(friends-shot coplay Qt::Core Qt::Gui Qt::GuiPrivate Qt::Network Qt::Qml Qt::Quick)
	if(APPLE)
		target_link_libraries(friends-shot "-framework Security" "-framework CoreFoundation")
	elseif(WIN32)
		target_link_libraries(friends-shot advapi32)
	else()
		target_link_libraries(friends-shot ${CMAKE_DL_LIBS})
	endif()
endif()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fanalyzer")
# The CMake target keeps upstream's name; the program people start is PSRemotePlayTogether
# (PSRemotePlayTogether.exe, PSRemotePlayTogether.app, usr/bin/PSRemotePlayTogether)
set_target_properties(chiaki PROPERTIES
		OUTPUT_NAME PSRemotePlayTogether
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME PSRemotePlayTogether
		MACOSX_BUNDLE_BUNDLE_VERSION ${PSRPT_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "GamerNova.net, Street Pea, thestr4ng3r (AGPLv3)"
		MACOSX_BUNDLE_GUI_IDENTIFIER "net.gamernova.psremoteplaytogether"
		MACOSX_BUNDLE_ICON_FILE chiaking.icns
		RESOURCE "${RESOURCE_FILES}")

install(TARGETS chiaki
		RUNTIME DESTINATION bin
		BUNDLE DESTINATION bin)
install(FILES chiaking.desktop DESTINATION share/applications)
install(FILES chiaking.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES io.github.streetpea.Chiaki4deck.appdata.xml DESTINATION share/metainfo)
