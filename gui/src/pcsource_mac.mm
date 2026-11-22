// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// This computer as the game source on a Mac (docs/pc-source.md): ScreenCaptureKit gives the
// picture of a window or a screen, already scaled into the picture size and in NV12, and the
// sound of one program (a window) or of everything but this app and voice chats (a screen).
// macOS has no virtual game controllers for apps: friends watch, the host plays.
// Built with ARC (gui/CMakeLists.txt).

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

// FFmpeg's enum AVMediaType and AVFoundation's AVMediaType share a name: FFmpeg's is renamed here
#define AVMediaType PsrptFFmpegMediaType
#include "pcsource_p.h"
#undef AVMediaType

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#include <libproc.h>
#include <unistd.h>

// ------------------------------------------------------------------ what can be shared

/** Programs whose sound never goes to the guests: voice chats (they would hear themselves). */
static const char *const kVoiceChats[] = {
	"discord", "teamspeak", "mumble", "skype", "zoom", "telegram", "signal", "element", "slack", "teams",
	"whatsapp", "facetime", "webex", "guilded",
};

static bool IsVoiceChat(NSRunningApplication *app)
{
	const QString id = QString::fromNSString(app.bundleIdentifier ?: @"").toLower();
	const QString name = QString::fromNSString(app.localizedName ?: @"").toLower();
	for(const char *chat : kVoiceChats)
	{
		if(id.contains(QLatin1String(chat)) || name.contains(QLatin1String(chat)))
			return true;
	}
	return false;
}

static QString PathOf(pid_t pid)
{
	char path[PROC_PIDPATHINFO_MAXSIZE] = {};
	if(proc_pidpath(pid, path, sizeof(path)) <= 0)
		return QString();
	return QString::fromUtf8(path);
}

/** A game: a Mac app that says it is one (its App Store category), or installed where game stores put games. */
static bool IsGame(NSRunningApplication *app, const QString &path)
{
	if(app && app.processIdentifier == getpid())
		return false;
	if(app.bundleURL)
	{
		NSBundle *bundle = [NSBundle bundleWithURL:app.bundleURL];
		id category = bundle.infoDictionary[@"LSApplicationCategoryType"];
		if([category isKindOfClass:[NSString class]] && [(NSString *)category containsString:@"games"])
			return true;
	}
	return PcSource::LooksLikeGame(path);
}

static QString GameName(NSRunningApplication *app, const QString &path)
{
	const QString steam = PcSource::SteamName(path);
	if(!steam.isEmpty())
		return steam;
	if(app.localizedName)
		return QString::fromNSString(app.localizedName).left(80);
	return QFileInfo(path).completeBaseName();
}

QString PcMac::Unsupported()
{
	if(@available(macOS 13.0, *))
		return QString();
	return QCoreApplication::translate("PcSource", "Sharing a game on this Mac needs macOS 13 or newer.");
}

QString PcMac::Permission()
{
	if(CGPreflightScreenCaptureAccess())
		return QString();
	return QCoreApplication::translate("PcSource", "macOS asks you first: allow PSRemotePlayTogether under System Settings → Privacy & Security → Screen & System Audio Recording, then quit the app and open it again.");
}

void PcMac::AskPermission()
{
	// The system's question comes once; after that only its settings page helps
	if(!CGRequestScreenCaptureAccess())
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

QList<PcSource::Target> PcMac::Targets()
{
	QList<PcSource::Target> windows, screens;
	const double scale = NSScreen.mainScreen ? NSScreen.mainScreen.backingScaleFactor : 2.0;
	CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	if(list)
	{
		NSArray *infos = CFBridgingRelease(list);
		for(NSDictionary *info in infos)
		{
			if([info[(id)kCGWindowLayer] intValue] != 0 || [info[(id)kCGWindowAlpha] doubleValue] <= 0.0)
				continue;
			const pid_t pid = [info[(id)kCGWindowOwnerPID] intValue];
			if(pid == getpid())
				continue;
			CGRect bounds = CGRectZero;
			if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(id)kCGWindowBounds], &bounds)
				|| bounds.size.width < 120 || bounds.size.height < 90)
				continue;
			NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
			PcSource::Target t;
			t.kind = QStringLiteral("window");
			t.handle = [info[(id)kCGWindowNumber] unsignedLongLongValue];
			t.pid = static_cast<quint32>(pid);
			// Window titles need the Screen Recording permission; the program's name otherwise
			const QString owner = QString::fromNSString(info[(id)kCGWindowOwnerName] ?: @"");
			t.title = QString::fromNSString(info[(id)kCGWindowName] ?: @"").trimmed();
			if(t.title.isEmpty())
				t.title = owner;
			if(t.title.isEmpty())
				continue;
			const QString path = PathOf(pid);
			t.app = app.localizedName ? QString::fromNSString(app.localizedName) : QFileInfo(path).fileName();
			t.width = static_cast<int>(bounds.size.width * scale);
			t.height = static_cast<int>(bounds.size.height * scale);
			t.game = IsGame(app, path);
			windows.append(t);
		}
	}
	// Games first, then the other windows as they are stacked
	std::stable_sort(windows.begin(), windows.end(), [](const PcSource::Target &a, const PcSource::Target &b) { return a.game && !b.game; });

	CGDirectDisplayID displays[16];
	uint32_t count = 0;
	if(CGGetActiveDisplayList(16, displays, &count) == kCGErrorSuccess)
	{
		int number = 1;
		for(uint32_t i = 0; i < count; i++)
		{
			PcSource::Target t;
			t.kind = QStringLiteral("screen");
			t.handle = displays[i];
			CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displays[i]);
			t.width = mode ? static_cast<int>(CGDisplayModeGetPixelWidth(mode)) : static_cast<int>(CGDisplayPixelsWide(displays[i]));
			t.height = mode ? static_cast<int>(CGDisplayModeGetPixelHeight(mode)) : static_cast<int>(CGDisplayPixelsHigh(displays[i]));
			if(mode)
				CGDisplayModeRelease(mode);
			if(CGDisplayIsMain(displays[i]))
			{
				t.title = QCoreApplication::translate("PcSource", "Main screen");
				screens.prepend(t);
			}
			else
			{
				t.title = QCoreApplication::translate("PcSource", "Screen %1").arg(++number);
				screens.append(t);
			}
		}
	}
	return windows + screens;
}

PcSource::Game PcMac::DetectGame(const PcSource::Target &target)
{
	PcSource::Game game;
	if(target.kind == QLatin1String("window") && target.pid)
	{
		NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(target.pid)];
		const QString path = PathOf(static_cast<pid_t>(target.pid));
		game.path = path;
		if(IsGame(app, path))
			game.title = GameName(app, path);
		else if(app.localizedName)
			game.title = QString::fromNSString(app.localizedName); // an emulator, a cloud gaming app
		return game;
	}
	// A screen: the game started last
	NSDate *newest = nil;
	for(NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications)
	{
		if(app.activationPolicy != NSApplicationActivationPolicyRegular)
			continue;
		const QString path = PathOf(app.processIdentifier);
		if(!IsGame(app, path))
			continue;
		if(newest && app.launchDate && [app.launchDate compare:newest] == NSOrderedAscending)
			continue;
		newest = app.launchDate ?: newest;
		game.path = path;
		game.title = GameName(app, path);
	}
	return game;
}

// ------------------------------------------------------------------ ScreenCaptureKit

/** What ScreenCaptureKit can capture now, waited for (it answers on its own queue). */
static SCShareableContent *Content(std::string *error) API_AVAILABLE(macos(13.0))
{
	__block SCShareableContent *content = nil;
	__block NSError *failure = nil;
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	[SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:NO
		completionHandler:^(SCShareableContent *c, NSError *e) {
			content = c;
			failure = e;
			dispatch_semaphore_signal(done);
		}];
	if(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) != 0)
	{
		*error = "macOS did not say what can be shared";
		return nil;
	}
	if(!content)
		*error = failure ? std::string("macOS does not allow it: ") + failure.localizedDescription.UTF8String
			+ " (allow the app under Privacy & Security → Screen & System Audio Recording)" : "macOS gave nothing to share";
	return content;
}

class MacStream;

API_AVAILABLE(macos(13.0))
@interface PsrptStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
{
	@public
	MacStream *owner;
}
@end

/** One SCStream and what it hands over. */
class MacStream
{
	public:
		virtual ~MacStream() = default;
		virtual void OnSample(CMSampleBufferRef sample, SCStreamOutputType type) API_AVAILABLE(macos(13.0)) = 0;
		virtual void OnStopped(const std::string &why) = 0;

	protected:
		bool StartStream(SCContentFilter *filter, SCStreamConfiguration *config, bool audio, std::string *error) API_AVAILABLE(macos(13.0))
		{
			output = [[PsrptStreamOutput alloc] init];
			output->owner = this;
			queue = dispatch_queue_create(audio ? "psrpt.sound" : "psrpt.picture", DISPATCH_QUEUE_SERIAL);
			stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output];
			NSError *failure = nil;
			// A picture output always: without one ScreenCaptureKit complains about every frame
			if(![stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&failure]
				|| (audio && ![stream addStreamOutput:output type:SCStreamOutputTypeAudio sampleHandlerQueue:queue error:&failure]))
			{
				*error = std::string("ScreenCaptureKit: ") + (failure ? failure.localizedDescription.UTF8String : "no output");
				return false;
			}
			__block NSError *start_error = nil;
			dispatch_semaphore_t started = dispatch_semaphore_create(0);
			[stream startCaptureWithCompletionHandler:^(NSError *e) {
				start_error = e;
				dispatch_semaphore_signal(started);
			}];
			if(dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) != 0)
			{
				*error = "ScreenCaptureKit did not start";
				return false;
			}
			if(start_error)
			{
				*error = std::string("ScreenCaptureKit: ") + start_error.localizedDescription.UTF8String;
				stream = nil;
				return false;
			}
			return true;
		}

		void StopStream()
		{
			if(@available(macOS 13.0, *))
			{
				if(stream)
				{
					dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
					[stream stopCaptureWithCompletionHandler:^(NSError *) { dispatch_semaphore_signal(stopped); }];
					dispatch_semaphore_wait(stopped, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
					stream = nil;
				}
				// No calls into a gone owner from samples still queued
				if(queue)
					dispatch_sync(queue, ^{});
				if(output)
					output->owner = nullptr;
			}
		}

		SCStream *stream API_AVAILABLE(macos(13.0)) = nil;
		PsrptStreamOutput *output API_AVAILABLE(macos(13.0)) = nil;
		dispatch_queue_t queue = nil;
};

@implementation PsrptStreamOutput
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type
{
	(void)stream;
	if(owner && CMSampleBufferIsValid(sample))
		owner->OnSample(sample, type);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
	(void)stream;
	if(owner)
		owner->OnStopped(error ? error.localizedDescription.UTF8String : "the capture stopped");
}
@end

/** The window (or the screen) to capture, and the display it is on. */
struct MacFound
{
	SCWindow *window API_AVAILABLE(macos(13.0)) = nil;
	SCDisplay *display API_AVAILABLE(macos(13.0)) = nil;
	SCRunningApplication *app API_AVAILABLE(macos(13.0)) = nil;
	NSArray *excluded API_AVAILABLE(macos(13.0)) = nil;
};

static bool Find(SCShareableContent *content, const PcSource::Target &target, MacFound *found, std::string *error) API_AVAILABLE(macos(13.0))
{
	NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
	for(SCRunningApplication *app in content.applications)
	{
		NSRunningApplication *running = [NSRunningApplication runningApplicationWithProcessIdentifier:app.processID];
		// This app (it shows what the guests get) and voice chats stay out of a screen
		if(app.processID == getpid() || (running && IsVoiceChat(running)))
			[excluded addObject:app];
	}
	found->excluded = excluded;
	if(target.kind == QLatin1String("window"))
	{
		for(SCWindow *w in content.windows)
		{
			if(w.windowID == target.handle)
				found->window = w;
		}
		if(!found->window)
		{
			*error = "the window was closed";
			return false;
		}
		found->app = found->window.owningApplication;
		// The display the window is on, for its sound
		const CGRect frame = found->window.frame;
		const CGPoint middle = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
		for(SCDisplay *d in content.displays)
		{
			if(CGRectContainsPoint(d.frame, middle))
				found->display = d;
		}
		if(!found->display)
			found->display = content.displays.firstObject;
		return found->display != nil;
	}
	for(SCDisplay *d in content.displays)
	{
		if(d.displayID == target.handle)
			found->display = d;
	}
	if(!found->display)
	{
		*error = "the screen is gone";
		return false;
	}
	return true;
}

namespace {

class MacVideo : public PcFrameSource, public MacStream
{
	public:
		~MacVideo() override
		{
			StopStream();
			std::lock_guard<std::mutex> lock(mutex);
			if(latest)
				av_frame_free(&latest);
		}

		bool Open(const PcCapture::Config &config, AVPixelFormat format, std::string *error) API_AVAILABLE(macos(13.0))
		{
			if(format != AV_PIX_FMT_NV12 && format != AV_PIX_FMT_YUV420P)
			{
				*error = "the picture is made NV12 or YUV420P";
				return false;
			}
			out_format = format;
			width = config.width;
			height = config.height;
			window_id = config.target.kind == QLatin1String("window") ? static_cast<CGWindowID>(config.target.handle) : 0;
			SCShareableContent *content = Content(error);
			MacFound found;
			if(!content || !Find(content, config.target, &found, error))
				return false;
			SCContentFilter *filter = found.window
				? [[SCContentFilter alloc] initWithDesktopIndependentWindow:found.window]
				: [[SCContentFilter alloc] initWithDisplay:found.display excludingApplications:found.excluded exceptingWindows:@[]];
			SCStreamConfiguration *conf = [[SCStreamConfiguration alloc] init];
			// Scaled by the system into the picture size, keeping the shape, in the encoder's NV12
			conf.width = static_cast<size_t>(width);
			conf.height = static_cast<size_t>(height);
			conf.scalesToFit = YES;
			if(@available(macOS 14.0, *))
				conf.preservesAspectRatio = YES;
			conf.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
			conf.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
			conf.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
			conf.minimumFrameInterval = CMTimeMake(1, std::max(10, config.fps));
			conf.queueDepth = 5;
			conf.showsCursor = YES;
			conf.capturesAudio = NO;
			return StartStream(filter, conf, false, error);
		}

		AVFrame *Next(bool *ended_out, std::string *error) override
		{
			std::unique_lock<std::mutex> lock(mutex);
			changed.wait_for(lock, std::chrono::seconds(1), [this] { return latest || ended; });
			if(latest)
			{
				AVFrame *f = latest;
				latest = nullptr;
				return f;
			}
			if(!ended && window_id)
			{
				// A still window sends nothing: is it still there?
				lock.unlock();
				const bool gone = !WindowExists();
				lock.lock();
				if(gone && !ended)
				{
					ended = true;
					why = "the window was closed";
				}
			}
			if(ended)
			{
				*ended_out = true;
				*error = why;
			}
			return nullptr;
		}

		void OnSample(CMSampleBufferRef sample, SCStreamOutputType type) override API_AVAILABLE(macos(13.0))
		{
			if(type != SCStreamOutputTypeScreen)
				return;
			// Only frames with a new picture (others say the picture is idle or the window hidden)
			CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
			if(attachments && CFArrayGetCount(attachments) > 0)
			{
				NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
				NSNumber *status = info[SCStreamFrameInfoStatus];
				if(status && status.integerValue != SCFrameStatusComplete)
					return;
			}
			CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
			if(!pixels || CVPixelBufferGetPlaneCount(pixels) < 2)
				return;
			AVFrame *f = av_frame_alloc();
			f->format = out_format;
			f->width = width;
			f->height = height;
			if(av_frame_get_buffer(f, 0) < 0)
			{
				av_frame_free(&f);
				return;
			}
			CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
			const int w = std::min(width, static_cast<int>(CVPixelBufferGetWidth(pixels)));
			const int h = std::min(height, static_cast<int>(CVPixelBufferGetHeight(pixels)));
			const bool planar = out_format == AV_PIX_FMT_YUV420P;
			// Black where the system's picture is smaller than ours
			if(w < width || h < height)
			{
				for(int row = 0; row < height; row++)
					memset(f->data[0] + row * f->linesize[0], 16, static_cast<size_t>(width));
				for(int row = 0; row < height / 2; row++)
				{
					memset(f->data[1] + row * f->linesize[1], 128, static_cast<size_t>(planar ? width / 2 : width));
					if(planar)
						memset(f->data[2] + row * f->linesize[2], 128, static_cast<size_t>(width / 2));
				}
			}
			const uint8_t *y = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixels, 0));
			const size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixels, 0);
			const uint8_t *uv = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixels, 1));
			const size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixels, 1);
			for(int row = 0; row < h; row++)
				memcpy(f->data[0] + row * f->linesize[0], y + row * y_stride, static_cast<size_t>(w));
			for(int row = 0; row < h / 2; row++)
			{
				const uint8_t *src = uv + row * uv_stride;
				if(!planar)
				{
					memcpy(f->data[1] + row * f->linesize[1], src, static_cast<size_t>(w & ~1));
					continue;
				}
				uint8_t *u = f->data[1] + row * f->linesize[1];
				uint8_t *v = f->data[2] + row * f->linesize[2];
				for(int x = 0; x < w / 2; x++)
				{
					u[x] = src[x * 2];
					v[x] = src[x * 2 + 1];
				}
			}
			CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
			{
				std::lock_guard<std::mutex> lock(mutex);
				if(latest)
					av_frame_free(&latest);
				latest = f;
			}
			changed.notify_one();
		}

		void OnStopped(const std::string &reason) override
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				if(ended)
					return;
				ended = true;
				why = reason;
			}
			changed.notify_all();
		}

	private:
		bool WindowExists() const
		{
			CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, window_id);
			if(!list)
				return false;
			const bool there = CFArrayGetCount(list) > 0;
			CFRelease(list);
			return there;
		}

		int width = 1920, height = 1080;
		AVPixelFormat out_format = AV_PIX_FMT_NV12;
		CGWindowID window_id = 0;
		std::mutex mutex;
		std::condition_variable changed;
		AVFrame *latest = nullptr;
		bool ended = false;
		std::string why;
};

class MacSound : public PcSoundSource, public MacStream
{
	public:
		~MacSound() override { StopStream(); }

		bool Open(const PcCapture::Config &config, PcSamples out, PcSoundInfo *info, std::string *error) API_AVAILABLE(macos(13.0))
		{
			samples = std::move(out);
			SCShareableContent *content = Content(error);
			MacFound found;
			if(!content || !Find(content, config.target, &found, error))
				return false;
			// A window: its program's sound alone. A screen: everything but this app's and voice chats'
			SCContentFilter *filter = found.window && found.app
				? [[SCContentFilter alloc] initWithDisplay:found.display includingApplications:@[ found.app ] exceptingWindows:@[]]
				: [[SCContentFilter alloc] initWithDisplay:found.display excludingApplications:found.excluded exceptingWindows:@[]];
			info->what = found.window && found.app ? "the program's own sound" : "everything but this app's and voice chats' sound";
			SCStreamConfiguration *conf = [[SCStreamConfiguration alloc] init];
			conf.capturesAudio = YES;
			conf.excludesCurrentProcessAudio = YES;
			conf.sampleRate = PcCapture::kAudioRate;
			conf.channelCount = PcCapture::kAudioChannels;
			// The picture of this stream is not used: as small and rare as it gets
			conf.width = 2;
			conf.height = 2;
			conf.minimumFrameInterval = CMTimeMake(1, 1);
			conf.queueDepth = 3;
			return StartStream(filter, conf, true, error);
		}

		void OnSample(CMSampleBufferRef sample, SCStreamOutputType type) override API_AVAILABLE(macos(13.0))
		{
			if(type != SCStreamOutputTypeAudio || !samples)
				return;
			CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
			const AudioStreamBasicDescription *asbd = format ? CMAudioFormatDescriptionGetStreamBasicDescription(format) : nullptr;
			if(!asbd)
				return;
			size_t list_size = 0;
			CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample, &list_size, nullptr, 0, nullptr, nullptr, 0, nullptr);
			std::vector<uint8_t> list_memory(std::max(list_size, sizeof(AudioBufferList)));
			auto list = reinterpret_cast<AudioBufferList *>(list_memory.data());
			CMBlockBufferRef block = nullptr;
			if(CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample, nullptr, list, list_memory.size(), nullptr, nullptr,
				kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr)
				return;
			const size_t frames = static_cast<size_t>(CMSampleBufferGetNumSamples(sample));
			const bool is_float = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
			const bool non_interleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
			const unsigned channels = std::max(1u, asbd->mChannelsPerFrame);
			converted.resize(frames * PcCapture::kAudioChannels);
			auto sample_at = [&](size_t frame, unsigned channel) -> float {
				channel = std::min(channel, channels - 1);
				if(non_interleaved)
				{
					const AudioBuffer &b = list->mBuffers[std::min<UInt32>(channel, list->mNumberBuffers - 1)];
					return is_float ? static_cast<const float *>(b.mData)[frame]
						: static_cast<const int16_t *>(b.mData)[frame] / 32768.0f;
				}
				const AudioBuffer &b = list->mBuffers[0];
				return is_float ? static_cast<const float *>(b.mData)[frame * channels + channel]
					: static_cast<const int16_t *>(b.mData)[frame * channels + channel] / 32768.0f;
			};
			if(list->mNumberBuffers > 0)
			{
				for(size_t i = 0; i < frames; i++)
				{
					for(unsigned c = 0; c < PcCapture::kAudioChannels; c++)
					{
						const float v = std::clamp(sample_at(i, c), -1.0f, 1.0f);
						converted[i * PcCapture::kAudioChannels + c] = static_cast<int16_t>(v * 32767.0f);
					}
				}
				samples(converted.data(), frames);
			}
			if(block)
				CFRelease(block);
		}

		void OnStopped(const std::string &) override
		{
		}

	private:
		PcSamples samples;
		std::vector<int16_t> converted;
};

} // namespace

std::unique_ptr<PcFrameSource> PcMac::OpenVideo(const PcCapture::Config &config, AVPixelFormat format, std::string *error)
{
	if(@available(macOS 13.0, *))
	{
		auto video = std::make_unique<MacVideo>();
		if(!video->Open(config, format, error))
			return nullptr;
		return video;
	}
	*error = "macOS 13 or newer is needed";
	return nullptr;
}

std::unique_ptr<PcSoundSource> PcMac::OpenSound(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error)
{
	if(@available(macOS 13.0, *))
	{
		auto sound = std::make_unique<MacSound>();
		if(!sound->Open(config, std::move(samples), info, error))
			return nullptr;
		return sound;
	}
	*error = "macOS 13 or newer is needed";
	return nullptr;
}
