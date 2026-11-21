
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
	include/livestream.h
	src/livestream.cpp
	include/livecontroller.h
	src/livecontroller.cpp
	include/keystore.h
	src/keystore.cpp
	include/livescenes.h
	src/livescenes.cpp
	include/websocketclient.h
	src/websocketclient.cpp
	include/twitch.h
	src/twitch.cpp
	include/livealerts.h
	src/livealerts.cpp
	include/twitchelements.h
	src/twitchelements.cpp
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
	include/coplaylinkpreview.h
	src/coplaylinkpreview.cpp
	src/streamsession_chat.cpp
	src/coplayaddress.cpp
	src/coplaymeet.cpp
	src/coplayrooms.cpp
	include/qmlsvgprovider.h
	src/qmlsvgprovider.cpp
	include/systemdinhibit.h
	src/systemdinhibit.cpp
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
	)
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
set(PSRPT_TWITCH_CLIENT_ID "" CACHE STRING "Client id of the app at Twitch (dev.twitch.tv), for logging in")
target_compile_definitions(chiaki PRIVATE PSRPT_TWITCH_CLIENT_ID="${PSRPT_TWITCH_CLIENT_ID}")
# Nayuki's QR code generator (MIT): the code to the room on the Play with us scene
target_include_directories(chiaki PRIVATE ${CMAKE_SOURCE_DIR}/third-party/qrcodegen)

add_library(placebo-libav-impl src/libav_impl.c)
target_link_libraries(placebo-libav-impl PkgConfig::LIBPLACEBO FFMPEG::avcodec FFMPEG::avutil FFMPEG::avformat)
target_link_libraries(chiaki PkgConfig::LIBPLACEBO placebo-libav-impl)

target_link_libraries(chiaki chiaki-lib)
target_link_libraries(chiaki coplay)
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
