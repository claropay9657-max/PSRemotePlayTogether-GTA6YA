#pragma once

#include "settings.h"

#include <coplay/crypto.h>

class CoPlayRecent;

class QmlSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool remotePlayAsk READ remotePlayAsk WRITE setRemotePlayAsk NOTIFY remotePlayAskChanged)
    Q_PROPERTY(bool addSteamShortcutAsk READ addSteamShortcutAsk WRITE setAddSteamShortcutAsk NOTIFY addSteamShortcutAskChanged)
    Q_PROPERTY(bool hideCursor READ hideCursor WRITE setHideCursor NOTIFY hideCursorChanged)
    Q_PROPERTY(int audioVideoDisabled READ audioVideoDisabled WRITE setAudioVideoDisabled NOTIFY audioVideoDisabledChanged)
    Q_PROPERTY(int resolutionLocalPS4 READ resolutionLocalPS4 WRITE setResolutionLocalPS4 NOTIFY resolutionLocalPS4Changed)
    Q_PROPERTY(int resolutionRemotePS4 READ resolutionRemotePS4 WRITE setResolutionRemotePS4 NOTIFY resolutionRemotePS4Changed)
    Q_PROPERTY(int resolutionLocalPS5 READ resolutionLocalPS5 WRITE setResolutionLocalPS5 NOTIFY resolutionLocalPS5Changed)
    Q_PROPERTY(int resolutionRemotePS5 READ resolutionRemotePS5 WRITE setResolutionRemotePS5 NOTIFY resolutionRemotePS5Changed)
    Q_PROPERTY(int disconnectAction READ disconnectAction WRITE setDisconnectAction NOTIFY disconnectActionChanged)
    Q_PROPERTY(int suspendAction READ suspendAction WRITE setSuspendAction NOTIFY suspendActionChanged)
    Q_PROPERTY(bool logVerbose READ logVerbose WRITE setLogVerbose NOTIFY logVerboseChanged)
    Q_PROPERTY(bool logSanitize READ logSanitize WRITE setLogSanitize NOTIFY logSanitizeChanged)
    Q_PROPERTY(bool vSyncEnabled READ vSyncEnabled WRITE setVSyncEnabled NOTIFY vSyncEnabledChanged)
    Q_PROPERTY(int rumbleHapticsIntensity READ rumbleHapticsIntensity WRITE setRumbleHapticsIntensity NOTIFY rumbleHapticsIntensityChanged)
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    Q_PROPERTY(bool steamDeckHaptics READ steamDeckHaptics WRITE setSteamDeckHaptics NOTIFY steamDeckHapticsChanged)
    Q_PROPERTY(bool verticalDeck READ verticalDeck WRITE setVerticalDeck NOTIFY verticalDeckChanged)
#endif
    Q_PROPERTY(bool buttonsByPosition READ buttonsByPosition WRITE setButtonsByPosition NOTIFY buttonsByPositionChanged)
    Q_PROPERTY(bool startMicUnmuted READ startMicUnmuted WRITE setStartMicUnmuted NOTIFY startMicUnmutedChanged)
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    Q_PROPERTY(bool speechProcessing READ speechProcessing WRITE setSpeechProcessing NOTIFY speechProcessingChanged)
    Q_PROPERTY(int noiseSuppressLevel READ noiseSuppressLevel WRITE setNoiseSuppressLevel NOTIFY noiseSuppressLevelChanged)
    Q_PROPERTY(int echoSuppressLevel READ echoSuppressLevel WRITE setEchoSuppressLevel NOTIFY echoSuppressLevelChanged)
#endif
    Q_PROPERTY(bool showStreamStats READ showStreamStats WRITE setShowStreamStats NOTIFY showStreamStatsChanged)
    Q_PROPERTY(bool streamerMode READ streamerMode WRITE setStreamerMode NOTIFY streamerModeChanged)
    // The intro film: "pending" (plays by itself at the next start), "seen", or "" (never opened here)
    Q_PROPERTY(QString introState READ introState WRITE setIntroState NOTIFY introStateChanged)
    Q_PROPERTY(bool introSound READ introSound WRITE setIntroSound NOTIFY introSoundChanged)
    Q_PROPERTY(bool updateCheckEnabled READ updateCheckEnabled WRITE setUpdateCheckEnabled NOTIFY updateCheckEnabledChanged)
    Q_PROPERTY(float hapticOverride READ hapticOverride WRITE setHapticOverride NOTIFY hapticOverrideChanged)
    Q_PROPERTY(int displayTargetContrast READ displayTargetContrast WRITE setDisplayTargetContrast NOTIFY displayTargetContrastChanged)
    Q_PROPERTY(int displayTargetPeak READ displayTargetPeak WRITE setDisplayTargetPeak NOTIFY displayTargetPeakChanged)
    Q_PROPERTY(int displayTargetPrim READ displayTargetPrim WRITE setDisplayTargetPrim NOTIFY displayTargetPrimChanged)
    Q_PROPERTY(int displayTargetTrc READ displayTargetTrc WRITE setDisplayTargetTrc NOTIFY displayTargetTrcChanged)
    Q_PROPERTY(bool fullscreenDoubleClick READ fullscreenDoubleClick WRITE setFullscreenDoubleClick NOTIFY fullscreenDoubleClickChanged)
    Q_PROPERTY(int fpsLocalPS4 READ fpsLocalPS4 WRITE setFpsLocalPS4 NOTIFY fpsLocalPS4Changed)
    Q_PROPERTY(int fpsRemotePS4 READ fpsRemotePS4 WRITE setFpsRemotePS4 NOTIFY fpsRemotePS4Changed)
    Q_PROPERTY(int fpsLocalPS5 READ fpsLocalPS5 WRITE setFpsLocalPS5 NOTIFY fpsLocalPS5Changed)
    Q_PROPERTY(int fpsRemotePS5 READ fpsRemotePS5 WRITE setFpsRemotePS5 NOTIFY fpsRemotePS5Changed)
    Q_PROPERTY(int bitrateLocalPS4 READ bitrateLocalPS4 WRITE setBitrateLocalPS4 NOTIFY bitrateLocalPS4Changed)
    Q_PROPERTY(int bitrateRemotePS4 READ bitrateRemotePS4 WRITE setBitrateRemotePS4 NOTIFY bitrateRemotePS4Changed)
    Q_PROPERTY(int bitrateLocalPS5 READ bitrateLocalPS5 WRITE setBitrateLocalPS5 NOTIFY bitrateLocalPS5Changed)
    Q_PROPERTY(int bitrateRemotePS5 READ bitrateRemotePS5 WRITE setBitrateRemotePS5 NOTIFY bitrateRemotePS5Changed)
    Q_PROPERTY(int codecLocalPS5 READ codecLocalPS5 WRITE setCodecLocalPS5 NOTIFY codecLocalPS5Changed)
    Q_PROPERTY(int codecRemotePS5 READ codecRemotePS5 WRITE setCodecRemotePS5 NOTIFY codecRemotePS5Changed)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize WRITE setAudioBufferSize NOTIFY audioBufferSizeChanged)
    Q_PROPERTY(int audioVolume READ audioVolume WRITE setAudioVolume NOTIFY audioVolumeChanged)
    Q_PROPERTY(QString audioInDevice READ audioInDevice WRITE setAudioInDevice NOTIFY audioInDeviceChanged)
    Q_PROPERTY(QString audioOutDevice READ audioOutDevice WRITE setAudioOutDevice NOTIFY audioOutDeviceChanged)
    Q_PROPERTY(QString decoder READ decoder WRITE setDecoder NOTIFY decoderChanged)
    Q_PROPERTY(bool useZeroCopy READ useZeroCopy WRITE setUseZeroCopy NOTIFY useZeroCopyChanged)
    Q_PROPERTY(bool vulkanDeferredSwap READ vulkanDeferredSwap WRITE setVulkanDeferredSwap NOTIFY vulkanDeferredSwapChanged)
    Q_PROPERTY(int windowType READ windowType WRITE setWindowType NOTIFY windowTypeChanged)
    Q_PROPERTY(uint customResolutionWidth READ customResolutionWidth WRITE setCustomResolutionWidth NOTIFY customResolutionWidthChanged)
    Q_PROPERTY(uint customResolutionHeight READ customResolutionHeight WRITE setCustomResolutionHeight NOTIFY customResolutionHeightChanged)
    Q_PROPERTY(int videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(int rendererBackend READ rendererBackend WRITE setRendererBackend NOTIFY rendererBackendChanged)
    Q_PROPERTY(float sZoomFactor READ sZoomFactor WRITE setSZoomFactor NOTIFY sZoomFactorChanged)
    Q_PROPERTY(int packetLossReportedMax READ packetLossReportedMax WRITE setPacketLossReportedMax NOTIFY packetLossReportedMaxChanged)
    Q_PROPERTY(QString autoConnectMac READ autoConnectMac WRITE setAutoConnectMac NOTIFY autoConnectMacChanged)
    Q_PROPERTY(bool allowJoystickBackgroundEvents READ allowJoystickBackgroundEvents WRITE setAllowJoystickBackgroundEvents NOTIFY allowJoystickBackgroundEventsChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QStringList availableDecoders READ availableDecoders CONSTANT)
    Q_PROPERTY(QStringList availableAudioInDevices READ availableAudioInDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QStringList availableAudioOutDevices READ availableAudioOutDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList registeredHosts READ registeredHosts NOTIFY registeredHostsChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(uint wifiDroppedNotif READ wifiDroppedNotif WRITE setWifiDroppedNotif NOTIFY wifiDroppedNotifChanged)
    Q_PROPERTY(bool portGuessingEnabled READ portGuessingEnabled WRITE setPortGuessingEnabled NOTIFY portGuessingEnabledChanged)
    Q_PROPERTY(int portGuessCount READ portGuessCount WRITE setPortGuessCount NOTIFY portGuessCountChanged)
    Q_PROPERTY(int portGuessSocketCount READ portGuessSocketCount WRITE setPortGuessSocketCount NOTIFY portGuessSocketCountChanged)
    Q_PROPERTY(QString psnRefreshToken READ psnRefreshToken WRITE setPsnRefreshToken NOTIFY psnRefreshTokenChanged)
    Q_PROPERTY(QString psnAuthToken READ psnAuthToken WRITE setPsnAuthToken NOTIFY psnAuthTokenChanged)
    Q_PROPERTY(QString psnAuthTokenExpiry READ psnAuthTokenExpiry WRITE setPsnAuthTokenExpiry NOTIFY psnAuthTokenExpiryChanged)
    Q_PROPERTY(QString psnAccountId READ psnAccountId WRITE setPsnAccountId NOTIFY psnAccountIdChanged)
    Q_PROPERTY(bool mouseTouchEnabled READ mouseTouchEnabled WRITE setMouseTouchEnabled NOTIFY mouseTouchEnabledChanged)
    Q_PROPERTY(bool keyboardEnabled READ keyboardEnabled WRITE setKeyboardEnabled NOTIFY keyboardEnabledChanged)
    // Play Together, see qmlsettings_coplay.cpp
    Q_PROPERTY(bool coplayHostEnabled READ coplayHostEnabled WRITE setCoplayHostEnabled NOTIFY coplayChanged)
    Q_PROPERTY(int coplayPort READ coplayPort WRITE setCoplayPort NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayAddress READ coplayAddress WRITE setCoplayAddress NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayName READ coplayName WRITE setCoplayName NOTIFY coplayChanged)
    Q_PROPERTY(int coplayMixMode READ coplayMixMode WRITE setCoplayMixMode NOTIFY coplayChanged)
    /** A game on this computer: the picture's height (720, 1080, 1440, 2160). */
    Q_PROPERTY(int coplayPcHeight READ coplayPcHeight WRITE setCoplayPcHeight NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayPcKeyMouse READ coplayPcKeyMouse WRITE setCoplayPcKeyMouse NOTIFY coplayChanged)
    /** Hosting a console: friends get the picture and sound of a capture card, and which one (Chiaki.cardDevices()). */
    Q_PROPERTY(bool coplayCard READ coplayCard WRITE setCoplayCard NOTIFY coplayChanged)
    Q_PROPERTY(QVariantMap coplayCardTarget READ coplayCardTarget WRITE setCoplayCardTarget NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayLastInvite READ coplayLastInvite WRITE setCoplayLastInvite NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayControllerOnly READ coplayControllerOnly WRITE setCoplayControllerOnly NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayAutoReconnect READ coplayAutoReconnect WRITE setCoplayAutoReconnect NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayInvite READ coplayInvite NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayLocalAddress READ coplayLocalAddress CONSTANT)
    Q_PROPERTY(QString coplayInviteLan READ coplayInviteLan NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayInvitePublic READ coplayInvitePublic NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayInviteIPv6 READ coplayInviteIPv6 NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayPublicAddress READ coplayPublicAddress NOTIFY coplayChanged)
    Q_PROPERTY(int coplayPublicLookupState READ coplayPublicLookupState NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayPublicLookupError READ coplayPublicLookupError NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayIPv6Address READ coplayIPv6Address NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayStunServer READ coplayStunServer WRITE setCoplayStunServer NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayMeetServer READ coplayMeetServer WRITE setCoplayMeetServer NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayMeetOn READ coplayMeetOn NOTIFY coplayChanged)
    /** The own address the meeting point had before it was switched off ("" for the default). */
    Q_PROPERTY(QString coplayMeetServerKept READ coplayMeetServerKept NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayMeetDefault READ coplayMeetDefault CONSTANT)
    Q_PROPERTY(bool coplayWebSupported READ coplayWebSupported CONSTANT)
    Q_PROPERTY(bool coplayWebGuests READ coplayWebGuests WRITE setCoplayWebGuests NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayGuestsMaySleep READ coplayGuestsMaySleep WRITE setCoplayGuestsMaySleep NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayKeepRoomAsleep READ coplayKeepRoomAsleep WRITE setCoplayKeepRoomAsleep NOTIFY coplayChanged)
    Q_PROPERTY(int coplayMaxGuests READ coplayMaxGuests WRITE setCoplayMaxGuests NOTIFY coplayChanged)
    // Hosting a public room, and the list of rooms
    Q_PROPERTY(bool coplayHostPublic READ coplayHostPublic WRITE setCoplayHostPublic NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayRoomTitle READ coplayRoomTitle WRITE setCoplayRoomTitle NOTIFY coplayChanged)
    /** A public room shows a small picture of its game in the list now and then. */
    Q_PROPERTY(bool coplayRoomPreview READ coplayRoomPreview WRITE setCoplayRoomPreview NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayRoomLanguage READ coplayRoomLanguage WRITE setCoplayRoomLanguage NOTIFY coplayChanged)
    Q_PROPERTY(int coplayJoinPolicy READ coplayJoinPolicy WRITE setCoplayJoinPolicy NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayRoomsServer READ coplayRoomsServer WRITE setCoplayRoomsServer NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayRoomsDefault READ coplayRoomsDefault CONSTANT)
    Q_PROPERTY(bool coplayRoomsOn READ coplayRoomsOn NOTIFY coplayChanged)
    /** The own address the room list had before it was switched off ("" for the default). */
    Q_PROPERTY(QString coplayRoomsServerKept READ coplayRoomsServerKept NOTIFY coplayChanged)
    Q_PROPERTY(QVariantList coplayLanguages READ coplayLanguages CONSTANT)
    Q_PROPERTY(QVariantList coplayRooms READ coplayRooms NOTIFY coplayRoomsChanged)
    /** Sessions joined before, newest first (coplayrecent.h). */
    Q_PROPERTY(QVariantList coplayRecent READ coplayRecent NOTIFY coplayRecentChanged)
    Q_PROPERTY(int coplayRoomsState READ coplayRoomsState NOTIFY coplayRoomsChanged)
    Q_PROPERTY(QString coplayRoomsError READ coplayRoomsError NOTIFY coplayRoomsChanged)
    Q_PROPERTY(QString coplayWebUrl READ coplayWebUrl WRITE setCoplayWebUrl NOTIFY coplayChanged)
    Q_PROPERTY(int coplayWebPort READ coplayWebPort WRITE setCoplayWebPort NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayWebDefault READ coplayWebDefault CONSTANT)
    Q_PROPERTY(QString coplayInviteWeb READ coplayInviteWeb NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayAvatarUrl READ coplayAvatarUrl NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayVoiceEnabled READ coplayVoiceEnabled WRITE setCoplayVoiceEnabled NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayVoiceStartMuted READ coplayVoiceStartMuted WRITE setCoplayVoiceStartMuted NOTIFY coplayChanged)
    Q_PROPERTY(int coplayVoiceVolume READ coplayVoiceVolume WRITE setCoplayVoiceVolume NOTIFY coplayChanged)
    Q_PROPERTY(QString coplayCameraDevice READ coplayCameraDevice WRITE setCoplayCameraDevice NOTIFY coplayChanged)
    Q_PROPERTY(int coplayCameraQuality READ coplayCameraQuality WRITE setCoplayCameraQuality NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayCameraOnStart READ coplayCameraOnStart WRITE setCoplayCameraOnStart NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayShowCams READ coplayShowCams WRITE setCoplayShowCams NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayChatPreviews READ coplayChatPreviews WRITE setCoplayChatPreviews NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayCamsOnStream READ coplayCamsOnStream WRITE setCoplayCamsOnStream NOTIFY coplayChanged)
    Q_PROPERTY(bool coplayCameraSupported READ coplayCameraSupported CONSTANT)
    // Stream overlay for OBS
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayChanged)
    Q_PROPERTY(int overlayPort READ overlayPort WRITE setOverlayPort NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayPlayers READ overlayPlayers WRITE setOverlayPlayers NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayPads READ overlayPads WRITE setOverlayPads NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayCombined READ overlayCombined WRITE setOverlayCombined NOTIFY overlayChanged)
    Q_PROPERTY(QString overlaySkin READ overlaySkin WRITE setOverlaySkin NOTIFY overlayChanged)
    Q_PROPERTY(int overlayScale READ overlayScale WRITE setOverlayScale NOTIFY overlayChanged)
    Q_PROPERTY(QString overlayLayout READ overlayLayout WRITE setOverlayLayout NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayWebcams READ overlayWebcams WRITE setOverlayWebcams NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayVoice READ overlayVoice WRITE setOverlayVoice NOTIFY overlayChanged)
    Q_PROPERTY(bool overlayOwnVoice READ overlayOwnVoice WRITE setOverlayOwnVoice NOTIFY overlayChanged)
    Q_PROPERTY(QStringList overlaySkins READ overlaySkins CONSTANT)
    Q_PROPERTY(bool dpadTouchEnabled READ dpadTouchEnabled WRITE setDpadTouchEnabled NOTIFY dpadTouchEnabledChanged)
    Q_PROPERTY(bool iDROnFECFailureEnabled READ iDROnFECFailureEnabled WRITE setIDROnFECFailureEnabled NOTIFY iDROnFECFailureEnabledChanged)
    Q_PROPERTY(uint16_t dpadTouchIncrement READ dpadTouchIncrement WRITE setDpadTouchIncrement NOTIFY dpadTouchIncrementChanged)
    Q_PROPERTY(uint dpadTouchShortcut1 READ dpadTouchShortcut1 WRITE setDpadTouchShortcut1 NOTIFY dpadTouchShortcut1Changed)
    Q_PROPERTY(uint dpadTouchShortcut2 READ dpadTouchShortcut2 WRITE setDpadTouchShortcut2 NOTIFY dpadTouchShortcut2Changed)
    Q_PROPERTY(uint dpadTouchShortcut3 READ dpadTouchShortcut3 WRITE setDpadTouchShortcut3 NOTIFY dpadTouchShortcut3Changed)
    Q_PROPERTY(uint dpadTouchShortcut4 READ dpadTouchShortcut4 WRITE setDpadTouchShortcut4 NOTIFY dpadTouchShortcut4Changed)
    Q_PROPERTY(bool streamMenuEnabled READ streamMenuEnabled WRITE setStreamMenuEnabled NOTIFY streamMenuEnabledChanged)
    Q_PROPERTY(uint streamMenuShortcut1 READ streamMenuShortcut1 WRITE setStreamMenuShortcut1 NOTIFY streamMenuShortcut1Changed)
    Q_PROPERTY(uint streamMenuShortcut2 READ streamMenuShortcut2 WRITE setStreamMenuShortcut2 NOTIFY streamMenuShortcut2Changed)
    Q_PROPERTY(uint streamMenuShortcut3 READ streamMenuShortcut3 WRITE setStreamMenuShortcut3 NOTIFY streamMenuShortcut3Changed)
    Q_PROPERTY(uint streamMenuShortcut4 READ streamMenuShortcut4 WRITE setStreamMenuShortcut4 NOTIFY streamMenuShortcut4Changed)
    Q_PROPERTY(int placeboUpscaler READ placeboUpscaler WRITE setPlaceboUpscaler NOTIFY placeboUpscalerChanged)
    Q_PROPERTY(int placeboPlaneUpscaler READ placeboPlaneUpscaler WRITE setPlaceboPlaneUpscaler NOTIFY placeboPlaneUpscalerChanged)
	Q_PROPERTY(int placeboDownscaler READ placeboDownscaler WRITE setPlaceboDownscaler NOTIFY placeboDownscalerChanged)
	Q_PROPERTY(int placeboPlaneDownscaler READ placeboPlaneDownscaler WRITE setPlaceboPlaneDownscaler NOTIFY placeboPlaneDownscalerChanged)
	Q_PROPERTY(int placeboFrameMixer READ placeboFrameMixer WRITE setPlaceboFrameMixer NOTIFY placeboFrameMixerChanged)
	Q_PROPERTY(bool directFrameMapping READ directFrameMapping WRITE setDirectFrameMapping NOTIFY directFrameMappingChanged)
	Q_PROPERTY(bool placeboDeinterlaceEnabled READ placeboDeinterlaceEnabled WRITE setPlaceboDeinterlaceEnabled NOTIFY placeboDeinterlaceEnabledChanged)
	Q_PROPERTY(int placeboDeinterlacePreset READ placeboDeinterlacePreset WRITE setPlaceboDeinterlacePreset NOTIFY placeboDeinterlacePresetChanged)
	Q_PROPERTY(int placeboDeinterlaceAlgorithm READ placeboDeinterlaceAlgorithm WRITE setPlaceboDeinterlaceAlgorithm NOTIFY placeboDeinterlaceAlgorithmChanged)
	Q_PROPERTY(bool placeboDeinterlaceSkipSpatial READ placeboDeinterlaceSkipSpatial WRITE setPlaceboDeinterlaceSkipSpatial NOTIFY placeboDeinterlaceSkipSpatialChanged)
	Q_PROPERTY(float placeboAntiringingStrength READ placeboAntiringingStrength WRITE setPlaceboAntiringingStrength NOTIFY placeboAntiringingStrengthChanged)
    Q_PROPERTY(bool placeboDebandEnabled READ placeboDebandEnabled WRITE setPlaceboDebandEnabled NOTIFY placeboDebandEnabledChanged)
    Q_PROPERTY(int placeboDebandPreset READ placeboDebandPreset WRITE setPlaceboDebandPreset NOTIFY placeboDebandPresetChanged)
    Q_PROPERTY(int placeboDebandIterations READ placeboDebandIterations WRITE setPlaceboDebandIterations NOTIFY placeboDebandIterationsChanged)
    Q_PROPERTY(float placeboDebandThreshold READ placeboDebandThreshold WRITE setPlaceboDebandThreshold NOTIFY placeboDebandThresholdChanged)
    Q_PROPERTY(float placeboDebandRadius READ placeboDebandRadius WRITE setPlaceboDebandRadius NOTIFY placeboDebandRadiusChanged)
    Q_PROPERTY(float placeboDebandGrain READ placeboDebandGrain WRITE setPlaceboDebandGrain NOTIFY placeboDebandGrainChanged)
    Q_PROPERTY(bool placeboSigmoidEnabled READ placeboSigmoidEnabled WRITE setPlaceboSigmoidEnabled NOTIFY placeboSigmoidEnabledChanged)
    Q_PROPERTY(int placeboSigmoidPreset READ placeboSigmoidPreset WRITE setPlaceboSigmoidPreset NOTIFY placeboSigmoidPresetChanged)
    Q_PROPERTY(float placeboSigmoidCenter READ placeboSigmoidCenter WRITE setPlaceboSigmoidCenter NOTIFY placeboSigmoidCenterChanged)
    Q_PROPERTY(float placeboSigmoidSlope READ placeboSigmoidSlope WRITE setPlaceboSigmoidSlope NOTIFY placeboSigmoidSlopeChanged)
    Q_PROPERTY(bool placeboColorAdjustmentEnabled READ placeboColorAdjustmentEnabled WRITE setPlaceboColorAdjustmentEnabled NOTIFY placeboColorAdjustmentEnabledChanged)
    Q_PROPERTY(int placeboColorAdjustmentPreset READ placeboColorAdjustmentPreset WRITE setPlaceboColorAdjustmentPreset NOTIFY placeboColorAdjustmentPresetChanged)
    Q_PROPERTY(float placeboColorAdjustmentBrightness READ placeboColorAdjustmentBrightness WRITE setPlaceboColorAdjustmentBrightness NOTIFY placeboColorAdjustmentBrightnessChanged)
    Q_PROPERTY(float placeboColorAdjustmentContrast READ placeboColorAdjustmentContrast WRITE setPlaceboColorAdjustmentContrast NOTIFY placeboColorAdjustmentContrastChanged)
    Q_PROPERTY(float placeboColorAdjustmentSaturation READ placeboColorAdjustmentSaturation WRITE setPlaceboColorAdjustmentSaturation NOTIFY placeboColorAdjustmentSaturationChanged)
    Q_PROPERTY(float placeboColorAdjustmentHue READ placeboColorAdjustmentHue WRITE setPlaceboColorAdjustmentHue NOTIFY placeboColorAdjustmentHueChanged)
    Q_PROPERTY(float placeboColorAdjustmentGamma READ placeboColorAdjustmentGamma WRITE setPlaceboColorAdjustmentGamma NOTIFY placeboColorAdjustmentGammaChanged)
    Q_PROPERTY(float placeboColorAdjustmentTemperature READ placeboColorAdjustmentTemperature WRITE setPlaceboColorAdjustmentTemperature NOTIFY placeboColorAdjustmentTemperatureChanged)
    Q_PROPERTY(bool placeboPeakDetectionEnabled READ placeboPeakDetectionEnabled WRITE setPlaceboPeakDetectionEnabled NOTIFY placeboPeakDetectionEnabledChanged)
    Q_PROPERTY(int placeboPeakDetectionPreset READ placeboPeakDetectionPreset WRITE setPlaceboPeakDetectionPreset NOTIFY placeboPeakDetectionPresetChanged)
    Q_PROPERTY(float placeboPeakDetectionPeakSmoothingPeriod READ placeboPeakDetectionPeakSmoothingPeriod WRITE setPlaceboPeakDetectionPeakSmoothingPeriod NOTIFY placeboPeakDetectionPeakSmoothingPeriodChanged)
    Q_PROPERTY(float placeboPeakDetectionSceneThresholdLow READ placeboPeakDetectionSceneThresholdLow WRITE setPlaceboPeakDetectionSceneThresholdLow NOTIFY placeboPeakDetectionSceneThresholdLowChanged)
    Q_PROPERTY(float placeboPeakDetectionSceneThresholdHigh READ placeboPeakDetectionSceneThresholdHigh WRITE setPlaceboPeakDetectionSceneThresholdHigh NOTIFY placeboPeakDetectionSceneThresholdHighChanged)
    Q_PROPERTY(float placeboPeakDetectionPeakPercentile READ placeboPeakDetectionPeakPercentile WRITE setPlaceboPeakDetectionPeakPercentile NOTIFY placeboPeakDetectionPeakPercentileChanged)
    Q_PROPERTY(float placeboPeakDetectionBlackCutoff READ placeboPeakDetectionBlackCutoff WRITE setPlaceboPeakDetectionBlackCutoff NOTIFY placeboPeakDetectionBlackCutoffChanged)
    Q_PROPERTY(bool placeboPeakDetectionAllowDelayedPeak READ placeboPeakDetectionAllowDelayedPeak WRITE setPlaceboPeakDetectionAllowDelayedPeak NOTIFY placeboPeakDetectionAllowDelayedPeakChanged)
    Q_PROPERTY(bool placeboColorMappingEnabled READ placeboColorMappingEnabled WRITE setPlaceboColorMappingEnabled NOTIFY placeboColorMappingEnabledChanged)
    Q_PROPERTY(int placeboColorMappingPreset READ placeboColorMappingPreset WRITE setPlaceboColorMappingPreset NOTIFY placeboColorMappingPresetChanged)
    Q_PROPERTY(int placeboGamutMappingFunction READ placeboGamutMappingFunction WRITE setPlaceboGamutMappingFunction NOTIFY placeboGamutMappingFunctionChanged)
    Q_PROPERTY(float placeboGamutMappingPerceptualDeadzone READ placeboGamutMappingPerceptualDeadzone WRITE setPlaceboGamutMappingPerceptualDeadzone NOTIFY placeboGamutMappingPerceptualDeadzoneChanged)
    Q_PROPERTY(float placeboGamutMappingPerceptualStrength READ placeboGamutMappingPerceptualStrength WRITE setPlaceboGamutMappingPerceptualStrength NOTIFY placeboGamutMappingPerceptualStrengthChanged)
    Q_PROPERTY(float placeboGamutMappingColorimetricGamma READ placeboGamutMappingColorimetricGamma WRITE setPlaceboGamutMappingColorimetricGamma NOTIFY placeboGamutMappingColorimetricGammaChanged)
    Q_PROPERTY(float placeboGamutMappingSoftClipKnee READ placeboGamutMappingSoftClipKnee WRITE setPlaceboGamutMappingSoftClipKnee NOTIFY placeboGamutMappingSoftClipKneeChanged)
    Q_PROPERTY(float placeboGamutMappingSoftClipDesat READ placeboGamutMappingSoftClipDesat WRITE setPlaceboGamutMappingSoftClipDesat NOTIFY placeboGamutMappingSoftClipDesatChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeI READ placeboGamutMappingLut3dSizeI WRITE setPlaceboGamutMappingLut3dSizeI NOTIFY placeboGamutMappingLut3dSizeIChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeC READ placeboGamutMappingLut3dSizeC WRITE setPlaceboGamutMappingLut3dSizeC NOTIFY placeboGamutMappingLut3dSizeCChanged)
    Q_PROPERTY(int placeboGamutMappingLut3dSizeH READ placeboGamutMappingLut3dSizeH WRITE setPlaceboGamutMappingLut3dSizeH NOTIFY placeboGamutMappingLut3dSizeHChanged)
    Q_PROPERTY(bool placeboGamutMappingLut3dTricubicEnabled READ placeboGamutMappingLut3dTricubicEnabled WRITE setPlaceboGamutMappingLut3dTricubicEnabled NOTIFY placeboGamutMappingLut3dTricubicEnabledChanged)
    Q_PROPERTY(bool placeboGamutMappingGamutExpansionEnabled READ placeboGamutMappingGamutExpansionEnabled WRITE setPlaceboGamutMappingGamutExpansionEnabled NOTIFY placeboGamutMappingGamutExpansionEnabledChanged)
    Q_PROPERTY(int placeboToneMappingFunction READ placeboToneMappingFunction WRITE setPlaceboToneMappingFunction NOTIFY placeboToneMappingFunctionChanged)
    Q_PROPERTY(float placeboToneMappingKneeAdaptation READ placeboToneMappingKneeAdaptation WRITE setPlaceboToneMappingKneeAdaptation NOTIFY placeboToneMappingKneeAdaptationChanged)
    Q_PROPERTY(float placeboToneMappingKneeMinimum READ placeboToneMappingKneeMinimum WRITE setPlaceboToneMappingKneeMinimum NOTIFY placeboToneMappingKneeMinimumChanged)
    Q_PROPERTY(float placeboToneMappingKneeMaximum READ placeboToneMappingKneeMaximum WRITE setPlaceboToneMappingKneeMaximum NOTIFY placeboToneMappingKneeMaximumChanged)
    Q_PROPERTY(float placeboToneMappingKneeDefault READ placeboToneMappingKneeDefault WRITE setPlaceboToneMappingKneeDefault NOTIFY placeboToneMappingKneeDefaultChanged)
    Q_PROPERTY(float placeboToneMappingKneeOffset READ placeboToneMappingKneeOffset WRITE setPlaceboToneMappingKneeOffset NOTIFY placeboToneMappingKneeOffsetChanged)
    Q_PROPERTY(float placeboToneMappingSlopeTuning READ placeboToneMappingSlopeTuning WRITE setPlaceboToneMappingSlopeTuning NOTIFY placeboToneMappingSlopeTuningChanged)
    Q_PROPERTY(float placeboToneMappingSlopeOffset READ placeboToneMappingSlopeOffset WRITE setPlaceboToneMappingSlopeOffset NOTIFY placeboToneMappingSlopeOffsetChanged)
    Q_PROPERTY(float placeboToneMappingSplineContrast READ placeboToneMappingSplineContrast WRITE setPlaceboToneMappingSplineContrast NOTIFY placeboToneMappingSplineContrastChanged)
    Q_PROPERTY(float placeboToneMappingReinhardContrast READ placeboToneMappingReinhardContrast WRITE setPlaceboToneMappingReinhardContrast NOTIFY placeboToneMappingReinhardContrastChanged)
    Q_PROPERTY(float placeboToneMappingLinearKnee READ placeboToneMappingLinearKnee WRITE setPlaceboToneMappingLinearKnee NOTIFY placeboToneMappingLinearKneeChanged)
    Q_PROPERTY(float placeboToneMappingExposure READ placeboToneMappingExposure WRITE setPlaceboToneMappingExposure NOTIFY placeboToneMappingExposureChanged)
    Q_PROPERTY(bool placeboToneMappingInverseToneMappingEnabled READ placeboToneMappingInverseToneMappingEnabled WRITE setPlaceboToneMappingInverseToneMappingEnabled NOTIFY placeboToneMappingInverseToneMappingEnabledChanged)
    Q_PROPERTY(int placeboToneMappingMetadata READ placeboToneMappingMetadata WRITE setPlaceboToneMappingMetadata NOTIFY placeboToneMappingMetadataChanged)
    Q_PROPERTY(int placeboToneMappingToneLutSize READ placeboToneMappingToneLutSize WRITE setPlaceboToneMappingToneLutSize NOTIFY placeboToneMappingToneLutSizeChanged)
    Q_PROPERTY(float placeboToneMappingContrastRecovery READ placeboToneMappingContrastRecovery WRITE setPlaceboToneMappingContrastRecovery NOTIFY placeboToneMappingContrastRecoveryChanged)
    Q_PROPERTY(float placeboToneMappingContrastSmoothness READ placeboToneMappingContrastSmoothness WRITE setPlaceboToneMappingContrastSmoothness NOTIFY placeboToneMappingContrastSmoothnessChanged)

public:
    QmlSettings(Settings *settings, QObject *parent = nullptr);

    int resolutionLocalPS4() const;
    void setResolutionLocalPS4(int resolution);
    int resolutionRemotePS4() const;
    void setResolutionRemotePS4(int resolution);
    int resolutionLocalPS5() const;
    void setResolutionLocalPS5(int resolution);
    int resolutionRemotePS5() const;
    void setResolutionRemotePS5(int resolution);

    int audioVideoDisabled() const;
    void setAudioVideoDisabled(int disabled);

    bool showStreamStats() const;
    void setShowStreamStats(bool enabled);

    bool streamerMode() const;
    QString introState() const;
    void setIntroState(const QString &state);
    bool introSound() const;
    void setIntroSound(bool on);
    bool updateCheckEnabled() const;
    void setUpdateCheckEnabled(bool enabled);
    void setStreamerMode(bool enabled);

    int displayTargetContrast() const;
    void setDisplayTargetContrast(int contrast);

    int displayTargetPeak() const;
    void setDisplayTargetPeak(int peak);

    int displayTargetPrim() const;
    void setDisplayTargetPrim(int prim);

    int displayTargetTrc() const;
    void setDisplayTargetTrc(int trc);

    int disconnectAction() const;
    void setDisconnectAction(int action);

    int suspendAction() const;
    void setSuspendAction(int action);

    bool logVerbose() const;
    void setLogVerbose(bool verbose);
    bool logSanitize() const;
    void setLogSanitize(bool enabled);
    bool vSyncEnabled() const;
    void setVSyncEnabled(bool enabled);

    int rumbleHapticsIntensity() const;
    void setRumbleHapticsIntensity(int intensity);

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    bool steamDeckHaptics() const;
    void setSteamDeckHaptics(bool steamDeckHaptics);

    bool verticalDeck() const;
    void setVerticalDeck(bool vertical);
#endif

    bool buttonsByPosition() const;
    void setButtonsByPosition(bool buttonsByPosition);

    bool iDROnFECFailureEnabled() const;
    void setIDROnFECFailureEnabled(bool enabled);

    bool allowJoystickBackgroundEvents() const;
    void setAllowJoystickBackgroundEvents(bool allowJoystickBackgroundEvents);

    bool startMicUnmuted() const;
    void setStartMicUnmuted(bool startMicUnmuted);

#ifdef CHIAKI_GUI_ENABLE_SPEEX
    bool speechProcessing() const;
    void setSpeechProcessing(bool processing);

    int noiseSuppressLevel() const;
    void setNoiseSuppressLevel(int level);

    int echoSuppressLevel() const;
    void setEchoSuppressLevel(int level);
#endif
    bool fullscreenDoubleClick() const;
    void setFullscreenDoubleClick(bool enabled);

    bool remotePlayAsk() const;
    void setRemotePlayAsk(bool asked);

    bool addSteamShortcutAsk() const;
    void setAddSteamShortcutAsk(bool asked);

    bool hideCursor() const;
    void setHideCursor(bool enabled);

    float hapticOverride() const;
    void setHapticOverride(float override);

    int fpsLocalPS4() const;
    void setFpsLocalPS4(int fps);
    int fpsRemotePS4() const;
    void setFpsRemotePS4(int fps);
    int fpsLocalPS5() const;
    void setFpsLocalPS5(int fps);
    int fpsRemotePS5() const;
    void setFpsRemotePS5(int fps);

    int bitrateLocalPS4() const;
    void setBitrateLocalPS4(int bitrate);
    int bitrateRemotePS4() const;
    void setBitrateRemotePS4(int bitrate);
    int bitrateLocalPS5() const;
    void setBitrateLocalPS5(int bitrate);
    int bitrateRemotePS5() const;
    void setBitrateRemotePS5(int bitrate);

    int codecLocalPS5() const;
    void setCodecLocalPS5(int codec);
    int codecRemotePS5() const;
    void setCodecRemotePS5(int codec);

    int audioBufferSize() const;
    void setAudioBufferSize(int size);

    int audioVolume() const;
    void setAudioVolume(int volume);

    QString audioInDevice() const;
    void setAudioInDevice(const QString &device);

    QString audioOutDevice() const;
    void setAudioOutDevice(const QString &device);

    QString decoder() const;
    void setDecoder(const QString &decoder);
    bool useZeroCopy() const;
    void setUseZeroCopy(bool enabled);
    bool vulkanDeferredSwap() const;
    void setVulkanDeferredSwap(bool enabled);

    int windowType() const;
    void setWindowType(int type);

    uint customResolutionWidth() const;
    void setCustomResolutionWidth(uint width);

    uint customResolutionHeight() const;
    void setCustomResolutionHeight(uint length);

    float sZoomFactor() const;
    void setSZoomFactor(float factor);

    int packetLossReportedMax() const;
    void setPacketLossReportedMax(int packet_loss_reported_max);

    int videoPreset() const;
    void setVideoPreset(int preset);

    int rendererBackend() const;
    void setRendererBackend(int backend);
    Q_INVOKABLE bool restartApplication();

    QString autoConnectMac() const;
    void setAutoConnectMac(const QString &mac);

    uint wifiDroppedNotif() const;
    void setWifiDroppedNotif(uint percent);
    bool portGuessingEnabled() const;
    void setPortGuessingEnabled(bool enabled);
    int portGuessCount() const;
    void setPortGuessCount(int count);
    int portGuessSocketCount() const;
    void setPortGuessSocketCount(int count);

    int placeboUpscaler() const;
    void setPlaceboUpscaler(int upscaler);

    int placeboPlaneUpscaler() const;
    void setPlaceboPlaneUpscaler(int upscaler);

    int placeboDownscaler() const;
    void setPlaceboDownscaler(int downscaler);

    int placeboPlaneDownscaler() const;
    void setPlaceboPlaneDownscaler(int downscaler);

    int placeboFrameMixer() const;
    bool directFrameMapping() const;
    void setDirectFrameMapping(bool enabled);
    void setPlaceboFrameMixer(int mixer);

    bool placeboDeinterlaceEnabled() const;
    void setPlaceboDeinterlaceEnabled(bool enabled);

    int placeboDeinterlacePreset() const;
    void setPlaceboDeinterlacePreset(int preset);

    int placeboDeinterlaceAlgorithm() const;
    void setPlaceboDeinterlaceAlgorithm(int algorithm);

    bool placeboDeinterlaceSkipSpatial() const;
    void setPlaceboDeinterlaceSkipSpatial(bool skip);

    float placeboAntiringingStrength() const;
    void setPlaceboAntiringingStrength(float strength);

    bool placeboDebandEnabled() const;
    void setPlaceboDebandEnabled(bool enabled);

    int placeboDebandPreset() const;
    void setPlaceboDebandPreset(int preset);

    int placeboDebandIterations() const;
    void setPlaceboDebandIterations(int iterations);

    float placeboDebandThreshold() const;
    void setPlaceboDebandThreshold(float threshold);

    float placeboDebandRadius() const;
    void setPlaceboDebandRadius(float radius);

    float placeboDebandGrain() const;
    void setPlaceboDebandGrain(float grain);

    bool placeboSigmoidEnabled() const;
    void setPlaceboSigmoidEnabled(bool enabled);

    int placeboSigmoidPreset() const;
    void setPlaceboSigmoidPreset(int preset);

    float placeboSigmoidCenter() const;
    void setPlaceboSigmoidCenter(float center);

    float placeboSigmoidSlope() const;
    void setPlaceboSigmoidSlope(float slope);

    bool placeboColorAdjustmentEnabled() const;
    void setPlaceboColorAdjustmentEnabled(bool enabled);

    int placeboColorAdjustmentPreset() const;
    void setPlaceboColorAdjustmentPreset(int preset);

    float placeboColorAdjustmentBrightness() const;
    void setPlaceboColorAdjustmentBrightness(float brightness);

    float placeboColorAdjustmentContrast() const;
    void setPlaceboColorAdjustmentContrast(float contrast);

    float placeboColorAdjustmentSaturation() const;
    void setPlaceboColorAdjustmentSaturation(float saturation);

    float placeboColorAdjustmentHue() const;
    void setPlaceboColorAdjustmentHue(float hue);

    float placeboColorAdjustmentGamma() const;
    void setPlaceboColorAdjustmentGamma(float gamma);

    float placeboColorAdjustmentTemperature() const;
    void setPlaceboColorAdjustmentTemperature(float temperature);

    bool placeboPeakDetectionEnabled() const;
    void setPlaceboPeakDetectionEnabled(bool enabled);

    int placeboPeakDetectionPreset() const;
    void setPlaceboPeakDetectionPreset(int preset);

    float placeboPeakDetectionPeakSmoothingPeriod() const;
    void setPlaceboPeakDetectionPeakSmoothingPeriod(float period);

    float placeboPeakDetectionSceneThresholdLow() const;
    void setPlaceboPeakDetectionSceneThresholdLow(float threshold_low);

    float placeboPeakDetectionSceneThresholdHigh() const;
    void setPlaceboPeakDetectionSceneThresholdHigh(float threshold_high);

    float placeboPeakDetectionPeakPercentile() const;
    void setPlaceboPeakDetectionPeakPercentile(float percentile);

    float placeboPeakDetectionBlackCutoff() const;
    void setPlaceboPeakDetectionBlackCutoff(float cutoff);

    bool placeboPeakDetectionAllowDelayedPeak() const;
    void setPlaceboPeakDetectionAllowDelayedPeak(bool allowed);

    bool placeboColorMappingEnabled() const;
    void setPlaceboColorMappingEnabled(bool enabled);

    int placeboColorMappingPreset() const;
    void setPlaceboColorMappingPreset(int preset);

    int placeboGamutMappingFunction() const;
    void setPlaceboGamutMappingFunction(int function);

    float placeboGamutMappingPerceptualDeadzone() const;
    void setPlaceboGamutMappingPerceptualDeadzone(float deadzone);

    float placeboGamutMappingPerceptualStrength() const;
    void setPlaceboGamutMappingPerceptualStrength(float strength);

    float placeboGamutMappingColorimetricGamma() const;
    void setPlaceboGamutMappingColorimetricGamma(float gamma);

    float placeboGamutMappingSoftClipKnee() const;
    void setPlaceboGamutMappingSoftClipKnee(float knee);

    float placeboGamutMappingSoftClipDesat() const;
    void setPlaceboGamutMappingSoftClipDesat(float desat);

    int placeboGamutMappingLut3dSizeI() const;
    void setPlaceboGamutMappingLut3dSizeI(int size);

    int placeboGamutMappingLut3dSizeC() const;
    void setPlaceboGamutMappingLut3dSizeC(int size);

    int placeboGamutMappingLut3dSizeH() const;
    void setPlaceboGamutMappingLut3dSizeH(int size);

    bool placeboGamutMappingLut3dTricubicEnabled() const;
    void setPlaceboGamutMappingLut3dTricubicEnabled(bool enabled);

    bool placeboGamutMappingGamutExpansionEnabled() const;
    void setPlaceboGamutMappingGamutExpansionEnabled(bool enabled);

    int placeboToneMappingFunction() const;
    void setPlaceboToneMappingFunction(int function);

    float placeboToneMappingKneeAdaptation() const;
    void setPlaceboToneMappingKneeAdaptation(float adaptation);

    float placeboToneMappingKneeMinimum() const;
    void setPlaceboToneMappingKneeMinimum(float min);

    float placeboToneMappingKneeMaximum() const;
    void setPlaceboToneMappingKneeMaximum(float max);

    float placeboToneMappingKneeDefault() const;
    void setPlaceboToneMappingKneeDefault(float default_knee);

    float placeboToneMappingKneeOffset() const;
    void setPlaceboToneMappingKneeOffset(float offset);

    float placeboToneMappingSlopeTuning() const;
    void setPlaceboToneMappingSlopeTuning(float tuning);

    float placeboToneMappingSlopeOffset() const;
    void setPlaceboToneMappingSlopeOffset(float offset);

    float placeboToneMappingSplineContrast() const;
    void setPlaceboToneMappingSplineContrast(float contrast);

    float placeboToneMappingReinhardContrast() const;
    void setPlaceboToneMappingReinhardContrast(float contrast);

    float placeboToneMappingLinearKnee() const;
    void setPlaceboToneMappingLinearKnee(float knee);

    float placeboToneMappingExposure() const;
    void setPlaceboToneMappingExposure(float exposure);

    bool placeboToneMappingInverseToneMappingEnabled() const;
    void setPlaceboToneMappingInverseToneMappingEnabled(bool enabled);

    int placeboToneMappingMetadata() const;
    void setPlaceboToneMappingMetadata(int metadata);

    int placeboToneMappingToneLutSize() const;
    void setPlaceboToneMappingToneLutSize(int size);

    float placeboToneMappingContrastRecovery() const;
    void setPlaceboToneMappingContrastRecovery(float recovery);

    float placeboToneMappingContrastSmoothness() const;
    void setPlaceboToneMappingContrastSmoothness(float smoothness);

    QString psnAuthToken() const;
    void setPsnAuthToken(const QString &auth_token);

    QString psnRefreshToken() const;
    void setPsnRefreshToken(const QString &refresh_token);

    QString psnAuthTokenExpiry() const;
    void setPsnAuthTokenExpiry(const QString &expiry);

    QString psnAccountId() const;
    void setPsnAccountId(const QString &account_id);

    bool mouseTouchEnabled() const;
    void setMouseTouchEnabled(bool enabled);

    bool keyboardEnabled() const;
    void setKeyboardEnabled(bool enabled);

    bool coplayHostEnabled() const;
    void setCoplayHostEnabled(bool enabled);
    int coplayPort() const;
    void setCoplayPort(int port);
    QString coplayAddress() const;
    void setCoplayAddress(const QString &address);
    QString coplayName() const;
    void setCoplayName(const QString &name);
    int coplayMixMode() const;
    int coplayPcHeight() const;
    void setCoplayPcHeight(int height);
    bool coplayPcKeyMouse() const;
    void setCoplayPcKeyMouse(bool on);
    bool coplayCard() const;
    void setCoplayCard(bool on);
    QVariantMap coplayCardTarget() const;
    void setCoplayCardTarget(const QVariantMap &target);
    void setCoplayMixMode(int mode);
    QString coplayLastInvite() const;
    void setCoplayLastInvite(const QString &invite);
    bool coplayControllerOnly() const;
    void setCoplayControllerOnly(bool enabled);
    /** Get a lost connection (to the console, or to the host of a shared session) back without asking. */
    bool coplayAutoReconnect() const;
    void setCoplayAutoReconnect(bool enabled);
    /** The text to give to friends. Creates the session key on first use. */
    QString coplayInvite() const;
    QString coplayLocalAddress() const;
    /** Replaces the session key: invites handed out before stop working. */
    Q_INVOKABLE void coplayNewKey();
    Q_INVOKABLE void coplayCopyInvite();

    // One invite for each way of reaching this computer, see qmlsettings_coplay.cpp
    /** For people in the same home: this computer's address on the local network. */
    QString coplayInviteLan() const;
    /** For friends elsewhere; empty for as long as coplayPublicAddress is. */
    QString coplayInvitePublic() const;
    /** For friends elsewhere over IPv6; empty if this computer has no global IPv6 address. */
    QString coplayInviteIPv6() const;
    /**
     * This computer's address on the internet: what the user wrote into
     * "Address in the invite", otherwise what a lookup has found, otherwise empty.
     */
    QString coplayPublicAddress() const;
    /** 0 = not looked up, 1 = looking, 2 = known (found, or written down by the user), 3 = failed. */
    int coplayPublicLookupState() const;
    /** After a failed lookup: why, in the words of the network code. */
    QString coplayPublicLookupError() const;
    QString coplayIPv6Address() const;
    /** "host:port" of the STUN server to ask; empty = the built-in ones, "off" = never look up. */
    QString coplayStunServer() const;
    void setCoplayStunServer(const QString &server);
    QString coplayMeetServer() const;
    void setCoplayMeetServer(const QString &server);
    /** Whether a meeting point is asked when sharing: the setting is neither "off" nor unusable. */
    bool coplayMeetOn() const;
    QString coplayMeetServerKept() const;
    /** The built-in meeting point, for showing it in the empty field. */
    QString coplayMeetDefault() const;
    /**
     * Looks the public address up without holding up the caller; the result arrives
     * through coplayChanged. This asks a server on the internet, so call it when the
     * user shares sessions or wants this invite, not at every start. An address found
     * within the last minutes is not asked for again.
     */
    Q_INVOKABLE void coplayLookupPublicAddress();
    /** The same, but asks in any case: for a button that says so. */
    Q_INVOKABLE void coplayRefreshPublicAddress();
    Q_INVOKABLE void coplayCopyInviteLan();
    Q_INVOKABLE void coplayCopyInvitePublic();
    Q_INVOKABLE void coplayCopyInviteIPv6();
    bool coplayHostPublic() const;
    void setCoplayHostPublic(bool on);
    QString coplayRoomTitle() const;
    void setCoplayRoomTitle(const QString &title);
    bool coplayRoomPreview() const;
    void setCoplayRoomPreview(bool on);
    QString coplayRoomLanguage() const;
    void setCoplayRoomLanguage(const QString &lang);
    int coplayJoinPolicy() const;
    void setCoplayJoinPolicy(int policy);
    QString coplayRoomsServer() const;
    void setCoplayRoomsServer(const QString &server);
    QString coplayRoomsDefault() const;
    /** Whether there is a list of public rooms at all (the setting is not "off"). */
    bool coplayRoomsOn() const;
    QString coplayRoomsServerKept() const;
    QVariantList coplayLanguages() const;
    QVariantList coplayRooms() const;
    int coplayRoomsState() const;
    QString coplayRoomsError() const;
    /** Reads the list of public rooms again. */
    Q_INVOKABLE void coplayRefreshRooms();
    QVariantList coplayRecent() const;
    /** Asks for each of them whether its host shares now. */
    Q_INVOKABLE void coplayLookRecent();
    Q_INVOKABLE void coplayForgetRecent(const QString &key);
    CoPlayRecent *recent() const { return coplay_recent; }
    /** How many friends may join a session at the same time. */
    int coplayMaxGuests() const;
    void setCoplayMaxGuests(int n);
    /** Guests may put the console to sleep when they leave; otherwise only the host does. */
    bool coplayGuestsMaySleep() const;
    bool coplayKeepRoomAsleep() const;
    void setCoplayKeepRoomAsleep(bool on);
    void setCoplayGuestsMaySleep(bool allowed);
    // Friends who join from a web browser, without the app
    /** Whether this build can take them at all. */
    bool coplayWebSupported() const;
    bool coplayWebGuests() const;
    void setCoplayWebGuests(bool enabled);
    /** The page they open; empty is the built-in one. */
    QString coplayWebUrl() const;
    void setCoplayWebUrl(const QString &url);
    /** The built-in page, for showing it in the empty field. */
    QString coplayWebDefault() const;
    /** The UDP port browsers come in on: 0 = one above the sharing port, -1 = any free one each. */
    int coplayWebPort() const;
    void setCoplayWebPort(int port);
    /**
     * The link for a friend's browser. Empty when browsers cannot join: switched off,
     * not part of this build, or no meeting point (it is what leads a browser to the host).
     */
    QString coplayInviteWeb() const;
    Q_INVOKABLE void coplayCopyInviteWeb();
    /** Address of the picture file for showing it, changes whenever the picture does. */
    QString coplayAvatarUrl() const;
    /** Lets the user pick an image and makes it the player's picture. */
    Q_INVOKABLE void coplayChooseAvatar();
    Q_INVOKABLE void coplayClearAvatar();

    bool coplayVoiceEnabled() const;
    void setCoplayVoiceEnabled(bool on);
    bool coplayVoiceStartMuted() const;
    void setCoplayVoiceStartMuted(bool on);
    int coplayVoiceVolume() const;
    void setCoplayVoiceVolume(int percent);
    QString coplayCameraDevice() const;
    void setCoplayCameraDevice(const QString &device);
    int coplayCameraQuality() const;
    void setCoplayCameraQuality(int quality);
    bool coplayCameraOnStart() const;
    void setCoplayCameraOnStart(bool on);
    bool coplayShowCams() const;
    /** Chat: this app looks at the first link of one's own message and sends a preview along. */
    bool coplayChatPreviews() const;
    void setCoplayChatPreviews(bool on);
    void setCoplayShowCams(bool on);
    bool coplayCamsOnStream() const;
    void setCoplayCamsOnStream(bool on);
    bool coplayCameraSupported() const;
    /** The cameras to choose from, the system's default first. */
    Q_INVOKABLE QStringList coplayCameraDevices() const;

    bool overlayEnabled() const;
    void setOverlayEnabled(bool enabled);
    int overlayPort() const;
    void setOverlayPort(int port);
    bool overlayPlayers() const;
    void setOverlayPlayers(bool on);
    bool overlayPads() const;
    void setOverlayPads(bool on);
    bool overlayCombined() const;
    void setOverlayCombined(bool on);
    QString overlaySkin() const;
    void setOverlaySkin(const QString &skin);
    int overlayScale() const;
    void setOverlayScale(int percent);
    QString overlayLayout() const;
    void setOverlayLayout(const QString &layout);
    QStringList overlaySkins() const;
    bool overlayWebcams() const;
    void setOverlayWebcams(bool on);
    bool overlayVoice() const;
    void setOverlayVoice(bool on);
    bool overlayOwnVoice() const;
    void setOverlayOwnVoice(bool on);

    bool dpadTouchEnabled() const;
    void setDpadTouchEnabled(bool enabled);

    uint16_t dpadTouchIncrement() const;
    void setDpadTouchIncrement(uint16_t increment);

    uint dpadTouchShortcut1() const;
    void setDpadTouchShortcut1(uint button);

    uint dpadTouchShortcut2() const;
    void setDpadTouchShortcut2(uint button);

    uint dpadTouchShortcut3() const;
    void setDpadTouchShortcut3(uint button);

    uint dpadTouchShortcut4() const;
    void setDpadTouchShortcut4(uint button);

    bool streamMenuEnabled() const;
    void setStreamMenuEnabled(bool enabled);

    uint streamMenuShortcut1() const;
    void setStreamMenuShortcut1(uint button);

    uint streamMenuShortcut2() const;
    void setStreamMenuShortcut2(uint button);

    uint streamMenuShortcut3() const;
    void setStreamMenuShortcut3(uint button);

    uint streamMenuShortcut4() const;
    void setStreamMenuShortcut4(uint button);

    QString currentProfile() const;
    void setCurrentProfile(const QString &profile);

    void setSettings(Settings *new_settings);
    void refreshAllKeys();
    void refreshAllPlaceboKeys();

    QString logDirectory() const;
    QStringList availableDecoders() const;
    QStringList availableAudioOutDevices() const;
    QStringList availableAudioInDevices() const;
    QVariantList registeredHosts() const;
    QStringList profiles() const;

    Q_INVOKABLE void deleteRegisteredHost(int index);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void exportSettings();
    Q_INVOKABLE QString chooseSteamBasePath();
    Q_INVOKABLE void exportPlaceboSettings();
    Q_INVOKABLE void importSettings();
    Q_INVOKABLE void importPlaceboSettings();
    Q_INVOKABLE void deleteProfile(QString profile);
    /** False if the name is taken or not allowed; the set in use stays in use. */
    Q_INVOKABLE bool renameProfile(const QString &from, const QString &to);
    /** {consoles, psn, streamerMode} of a set, "" for the default one. */
    Q_INVOKABLE QVariantMap profileSummary(const QString &profile) const;
    Q_INVOKABLE QString stringForDpadShortcut() const;
    Q_INVOKABLE QString stringForStreamMenuShortcut() const;

signals:
    void resolutionLocalPS4Changed();
    void resolutionRemotePS4Changed();
    void resolutionLocalPS5Changed();
    void resolutionRemotePS5Changed();
    void disconnectActionChanged();
    void suspendActionChanged();
    void logVerboseChanged();
    void logSanitizeChanged();
    void vSyncEnabledChanged();
    void rumbleHapticsIntensityChanged();
    void buttonsByPositionChanged();
    void allowJoystickBackgroundEventsChanged();
    void startMicUnmutedChanged();
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
    void verticalDeckChanged();
    void steamDeckHapticsChanged();
#endif
#ifdef CHIAKI_GUI_ENABLE_SPEEX
    void speechProcessingChanged();
    void noiseSuppressLevelChanged();
    void echoSuppressLevelChanged();
#endif
    void fullscreenDoubleClickChanged();
    void remotePlayAskChanged();
    void addSteamShortcutAskChanged();
    void hideCursorChanged();
    void hapticOverrideChanged();
    void audioVideoDisabledChanged();
    void showStreamStatsChanged();
    void streamerModeChanged();
    void introStateChanged();
    void introSoundChanged();
    void updateCheckEnabledChanged();
    void fpsLocalPS4Changed();
    void fpsRemotePS4Changed();
    void fpsLocalPS5Changed();
    void fpsRemotePS5Changed();
    void bitrateLocalPS4Changed();
    void bitrateRemotePS4Changed();
    void bitrateLocalPS5Changed();
    void bitrateRemotePS5Changed();
    void codecLocalPS5Changed();
    void codecRemotePS5Changed();
    void vulkanDeferredSwapChanged();
    void displayTargetPeakChanged();
    void displayTargetContrastChanged();
    void displayTargetPrimChanged();
    void displayTargetTrcChanged();
    void audioBufferSizeChanged();
    void audioVolumeChanged();
    void audioOutDeviceChanged();
    void audioInDeviceChanged();
    void wifiDroppedNotifChanged();
    void portGuessingEnabledChanged();
    void portGuessCountChanged();
    void portGuessSocketCountChanged();
    void decoderChanged();
    void useZeroCopyChanged();
    void windowTypeChanged();
    void customResolutionWidthChanged();
    void customResolutionHeightChanged();
    void sZoomFactorChanged();
    void videoPresetChanged();
    void rendererBackendChanged();
    void autoConnectMacChanged();
    void audioDevicesChanged();
    void registeredHostsChanged();
    void psnAuthTokenChanged();
    void psnRefreshTokenChanged();
    void psnAuthTokenExpiryChanged();
    void psnAccountIdChanged();
    void mouseTouchEnabledChanged();
    void keyboardEnabledChanged();
    void coplayChanged();
    void coplayRoomsChanged();
    void coplayRecentChanged();
    void overlayChanged();
    void dpadTouchEnabledChanged();
    void dpadTouchIncrementChanged();
    void dpadTouchShortcut1Changed();
    void dpadTouchShortcut2Changed();
    void dpadTouchShortcut3Changed();
    void dpadTouchShortcut4Changed();
    void streamMenuEnabledChanged();
    void streamMenuShortcut1Changed();
    void streamMenuShortcut2Changed();
    void streamMenuShortcut3Changed();
    void streamMenuShortcut4Changed();
    void packetLossReportedMaxChanged();
    void iDROnFECFailureEnabledChanged();
    void currentProfileChanged();
    void profilesChanged();
    void placeboUpscalerChanged();
    void placeboPlaneUpscalerChanged();
    void placeboDownscalerChanged();
    void placeboPlaneDownscalerChanged();
    void placeboFrameMixerChanged();
    void directFrameMappingChanged();
    void placeboDeinterlaceEnabledChanged();
    void placeboDeinterlacePresetChanged();
    void placeboDeinterlaceAlgorithmChanged();
    void placeboDeinterlaceSkipSpatialChanged();
    void placeboAntiringingStrengthChanged();
    void placeboDebandEnabledChanged();
    void placeboDebandPresetChanged();
    void placeboDebandIterationsChanged();
    void placeboDebandThresholdChanged();
    void placeboDebandRadiusChanged();
    void placeboDebandGrainChanged();
    void placeboSigmoidEnabledChanged();
    void placeboSigmoidPresetChanged();
    void placeboSigmoidCenterChanged();
    void placeboSigmoidSlopeChanged();
    void placeboColorAdjustmentEnabledChanged();
    void placeboColorAdjustmentPresetChanged();
    void placeboColorAdjustmentBrightnessChanged();
    void placeboColorAdjustmentContrastChanged();
    void placeboColorAdjustmentSaturationChanged();
    void placeboColorAdjustmentHueChanged();
    void placeboColorAdjustmentGammaChanged();
    void placeboColorAdjustmentTemperatureChanged();
    void placeboPeakDetectionEnabledChanged();
    void placeboPeakDetectionPresetChanged();
    void placeboPeakDetectionPeakSmoothingPeriodChanged();
    void placeboPeakDetectionSceneThresholdLowChanged();
    void placeboPeakDetectionSceneThresholdHighChanged();
    void placeboPeakDetectionPeakPercentileChanged();
    void placeboPeakDetectionBlackCutoffChanged();
    void placeboPeakDetectionAllowDelayedPeakChanged();
    void placeboColorMappingEnabledChanged();
    void placeboColorMappingPresetChanged();
    void placeboGamutMappingFunctionChanged();
    void placeboGamutMappingPerceptualDeadzoneChanged();
    void placeboGamutMappingPerceptualStrengthChanged();
    void placeboGamutMappingColorimetricGammaChanged();
    void placeboGamutMappingSoftClipKneeChanged();
    void placeboGamutMappingSoftClipDesatChanged();
    void placeboGamutMappingLut3dSizeIChanged();
    void placeboGamutMappingLut3dSizeCChanged();
    void placeboGamutMappingLut3dSizeHChanged();
    void placeboGamutMappingLut3dTricubicEnabledChanged();
    void placeboGamutMappingGamutExpansionEnabledChanged();
    void placeboToneMappingFunctionChanged();
    void placeboToneMappingKneeAdaptationChanged();
    void placeboToneMappingKneeMinimumChanged();
    void placeboToneMappingKneeMaximumChanged();
    void placeboToneMappingKneeDefaultChanged();
    void placeboToneMappingKneeOffsetChanged();
    void placeboToneMappingSlopeTuningChanged();
    void placeboToneMappingSlopeOffsetChanged();
    void placeboToneMappingSplineContrastChanged();
    void placeboToneMappingReinhardContrastChanged();
    void placeboToneMappingLinearKneeChanged();
    void placeboToneMappingExposureChanged();
    void placeboToneMappingInverseToneMappingEnabledChanged();
    void placeboToneMappingMetadataChanged();
    void placeboToneMappingToneLutSizeChanged();
    void placeboToneMappingContrastRecoveryChanged();
    void placeboToneMappingContrastSmoothnessChanged();
    void placeboChanged();

private:
    QString coplayInviteFor(const QString &host) const;
    /** The key of the kind of session that is set up: the private one, or the public room's. */
    coplay::Secret coplayHostSecret() const;

    Settings *settings = {};
    CoPlayRecent *coplay_recent = {};
    QStringList audio_in_devices;
    QStringList audio_out_devices;
};
