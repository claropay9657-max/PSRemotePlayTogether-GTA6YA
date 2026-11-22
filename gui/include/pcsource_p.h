// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_PCSOURCE_P_H
#define PSRPT_PCSOURCE_P_H

// What pcsource.cpp (the encoders, Windows) and the other systems' parts
// (pcsource_linux.cpp, pcsource_mac.mm) share. Not for the rest of the app.

#include "pcsource.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

/** Where the pictures come from. Next() runs on the capture thread only. */
class PcFrameSource
{
	public:
		virtual ~PcFrameSource() = default;
		/**
		 * The next picture, or null if none came for a while (the picture did not change).
		 * Sets *ended (and *error) once no picture will ever come again.
		 */
		virtual AVFrame *Next(bool *ended, std::string *error) = 0;
		/** The graphics card's frames the pictures are in, or null for pictures in memory. */
		virtual AVBufferRef *HwFrames() const { return nullptr; }
};

/** Takes 16-bit stereo samples at 48 kHz, from any thread. */
using PcSamples = std::function<void(const int16_t *samples, size_t frames)>;

/** The sound of what is shared, while it lives. */
class PcSoundSource
{
	public:
		virtual ~PcSoundSource() = default;
};

/** Whose sound is shared, in words for the log ("the game's own sound"). */
struct PcSoundInfo
{
	std::string what;
};

namespace PcSource {

/** Whether a program is a game, by where it is installed (the GamerNova client's rules). */
bool LooksLikeGame(const QString &program);
/** Steam's name for a game it installed, from the manifest of its install folder; empty if none of Steam's. */
QString SteamName(const QString &program);
/** Steam's name for a game by its app id, from the manifests of all Steam libraries; empty if not found. */
QString SteamNameById(const QString &steam_root, const QString &app_id);
/** A game's window title as a name, without what games add in brackets. */
QString TitleAsName(const QString &title);

} // namespace PcSource

#if defined(__linux__)
namespace PcLinux {
QString Unsupported();
QList<PcSource::Target> Targets();
PcSource::Game DetectGame(const PcSource::Target &target);
/** The picture of a stream the system's screen sharing gave (Config::portal_fd and portal_node). */
std::unique_ptr<PcFrameSource> OpenVideo(const PcCapture::Config &config, AVPixelFormat format, std::string *error);
/** The game's own sound, else every program's but this app's and voice chats'. */
std::unique_ptr<PcSoundSource> OpenSound(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error);
} // namespace PcLinux
#endif

#if defined(__APPLE__)
namespace PcMac {
QString Unsupported();
QString Permission();
void AskPermission();
QList<PcSource::Target> Targets();
PcSource::Game DetectGame(const PcSource::Target &target);
/** A window or a screen through ScreenCaptureKit, scaled by the system, in NV12. */
std::unique_ptr<PcFrameSource> OpenVideo(const PcCapture::Config &config, AVPixelFormat format, std::string *error);
/** The program's own sound (a window), or everything but this app's (a screen). */
std::unique_ptr<PcSoundSource> OpenSound(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error);
} // namespace PcMac
#endif

#endif // PSRPT_PCSOURCE_P_H
