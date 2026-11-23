// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_PCSOURCE_H
#define PSRPT_PCSOURCE_H

// This computer as the game source (docs/pc-source.md): the host shares a window or a
// screen of its own computer instead of a console. What the guests get is the same as
// from a console: H.264 access units and Opus frames, which the session passes on through
// coplay::Host exactly like the console's.

#include <QList>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace PcSource {

/** Something on this computer whose picture can be shared. */
struct Target
{
	/**
	 * "window", "screen", "portal" (Linux: the system asks which window or screen, see
	 * PcPortal), or "test" (a moving test picture and a tone, for trying it out).
	 */
	QString kind;
	/** The window (HWND) or the screen (HMONITOR) on Windows; 0 otherwise. */
	quint64 handle = 0;
	/** The window's title, or the screen's name. */
	QString title;
	/** The program's file name ("XboxPcApp.exe"); empty for a screen. */
	QString app;
	/** The program, for sharing only its sound; 0 for a screen: then everything but this app. */
	quint32 pid = 0;
	int width = 0;
	int height = 0;
	/** The Xbox app: the room says Xbox instead of PC. */
	bool xbox = false;
	/** The window belongs to a game (see DetectGame()); games come first in the list. */
	bool game = false;
	/** "portal": what lets the system share the same window or screen again without asking. */
	QString token;

	bool Valid() const { return !kind.isEmpty(); }
	QVariantMap ToMap() const;
	static Target FromMap(const QVariantMap &map);
};

/**
 * Why this computer cannot share a game, in words for the Host page; empty if it can.
 * Windows, Linux (xdg-desktop-portal and PipeWire) and macOS (ScreenCaptureKit).
 */
QString Unsupported();

/**
 * A permission the system wants before anything can be shared (macOS: Screen Recording), in
 * words for the Host page; empty if there is none to give.
 */
QString Permission();
/** Asks for that permission: the system's question, else its settings page. */
void AskPermission();

/** What the Host page says about sharing on this system, under the list. */
QString Note();

/**
 * The windows and screens that can be shared, the screens last. Leaves out this app's own
 * windows and windows nobody sees. With PSRPT_PC_SOURCE_TEST set, a test picture comes first,
 * on every system.
 */
QList<Target> Targets();

/**
 * Only the capture devices (capture cards, and cameras: the system cannot tell them apart),
 * for a console whose picture the guests get from a capture card.
 */
QList<Target> Devices();

/**
 * The picture the guests get of a window or screen: the shape of the window (an ultrawide
 * game stays ultrawide, 4:3 to 32:9), at most `height` lines high and never larger than the
 * window itself.
 */
void PictureSize(const Target &target, int height, int *width_out, int *height_out);

/** The bit rate for a picture of that size, in kbit/s: 15 Mbit/s for 1080p at 60, more for more. */
int BitrateFor(int width, int height, int fps);

/** The window or screen again, by what was chosen earlier: the same handle, or the same program and title. */
Target Find(const Target &chosen);

/** A game that runs on this computer. */
struct Game
{
	/** As the room and the others see it; empty if none was found. */
	QString title;
	/** Its program. */
	QString path;
};

/**
 * Which game is shared, found the way the GamerNova client finds it: a program installed
 * where game stores put games (Steam, Epic, EA, Ubisoft, GOG, Battle.net, Riot, Xbox), or in
 * WindowsApps with the marks of a game (an Xbox GDK config, Unity's data folder, Unreal's
 * Binaries folder), and none of the launchers, browsers and tools that run beside games. The
 * name is Steam's own where Steam installed it, else the game's window title.
 *
 * A window: its own program if that is a game, else the program's name (never the title of
 * a window that is no game: that could be a document's name). A screen: the game started last.
 */
Game DetectGame(const Target &target);

} // namespace PcSource

/**
 * Captures the picture and the sound of a Target and encodes them for the guests: H.264
 * from the graphics card's encoder (no B-frames, steady bit rate, a key frame when asked),
 * Opus at 48 kHz stereo in frames of 10 ms, as a console sends it.
 */
class PcCapture
{
	public:
		struct Config
		{
			PcSource::Target target;
			/** The picture the guests get; the window is scaled into it, keeping its shape. */
			int width = 1920;
			int height = 1080;
			int fps = 60;
			int video_kbps = 15000;
			bool sound = true;
			/** Linux: the stream the system's screen sharing gave (PcPortal::Stream); the fd stays the caller's. */
			int portal_fd = -1;
			quint32 portal_node = 0;
		};

		/** Called from the capture's own threads. */
		struct Callbacks
		{
			/** One access unit, Annex B, with SPS and PPS in front of every key frame. */
			std::function<void(const uint8_t *data, size_t size)> on_video;
			/** One Opus frame (480 samples, stereo, 48 kHz). */
			std::function<void(const uint8_t *data, size_t size)> on_audio;
			/** The picture stopped for good (the window closed, the encoder failed). */
			std::function<void(const std::string &why)> on_error;
			std::function<void(const std::string &line)> on_log;
		};

		/** The sound the guests get: Opus as AudioHeader describes it. */
		static constexpr unsigned kAudioRate = 48000;
		static constexpr unsigned kAudioChannels = 2;
		static constexpr unsigned kAudioFrameSamples = 480;

		PcCapture(const Config &config, const Callbacks &callbacks);
		~PcCapture();
		PcCapture(const PcCapture &) = delete;
		PcCapture &operator=(const PcCapture &) = delete;

		/** Opens capture and encoder and starts the threads. */
		bool Start(std::string *error);
		void Stop();
		/** The next picture is a key frame, for a guest who has to start decoding. */
		void RequestKeyframe();
		/** The encoder in use ("h264_nvenc"), once started. */
		std::string Encoder() const;
		/** Whether the sound is the program's alone, or everything but this app's. */
		std::string SoundSource() const;

	private:
		struct Private;
		std::unique_ptr<Private> d;
};

/**
 * Linux: asks the system which window or screen to share (xdg-desktop-portal's ScreenCast),
 * in the background. The sharing lasts as long as this object. Elsewhere it does nothing.
 */
class PcPortal
{
	public:
		struct Stream
		{
			/** PipeWire's connection for the stream, for PcCapture::Config::portal_fd. */
			int fd = -1;
			quint32 node = 0;
			int width = 0;
			int height = 0;
			/** A window, not a whole screen. */
			bool window = false;
			/** For sharing the same again next time without asking (PcSource::Target::token). */
			QString token;
		};

		PcPortal();
		~PcPortal();
		PcPortal(const PcPortal &) = delete;
		PcPortal &operator=(const PcPortal &) = delete;

		/**
		 * Asks (or shares what `token` names again) and calls `done` on the main thread, with
		 * the stream or with why there is none (empty if the person cancelled).
		 */
		void Open(const QString &token, std::function<void(const Stream &stream, const QString &error)> done);

	private:
		struct Private;
		std::unique_ptr<Private> d;
};

#endif // PSRPT_PCSOURCE_H
