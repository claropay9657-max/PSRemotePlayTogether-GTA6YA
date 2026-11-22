// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_PROTOCOL_H
#define COPLAY_PROTOCOL_H

/*
 * Co-play wire protocol, version 4.
 *
 * One host is connected to the console through Remote Play. Guests connect to
 * the host over UDP, receive the console's video, audio and controller
 * feedback from it, and send their controller state back. The host merges all
 * controller states into the single Remote Play controller.
 *
 * Everybody can also talk to and see each other: each player's microphone
 * (Opus) and webcam (JPEG pictures) go to the host, which passes them on to
 * the other players. Text chat goes the same way, but every message is
 * acknowledged and sent again until it is.
 *
 * Every datagram is:
 *
 *     0        type        (PacketType)
 *     1        version     (kProtocolVersion)
 *     2..5     conn_id     big endian, assigned by the host in Welcome
 *     6..13    counter     big endian, strictly increasing per direction
 *     14..     ChaCha20-Poly1305 ciphertext, followed by the 16 byte tag
 *
 * The 14 header bytes are authenticated as additional data and bytes 2..13
 * are the AEAD nonce. Hello, Welcome and Reject are sealed with keys derived
 * from the invite secret alone and carry 12 random bytes in place of conn_id
 * and counter. Everything else uses per-connection, per-direction keys
 * derived from the invite secret and both handshake nonces.
 *
 * All integers inside payloads are big endian.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bytes.h"

namespace coplay {

constexpr uint8_t kProtocolVersion = 5;
/** The longest Twitch login a guest may announce (see MediaFlags). */
constexpr size_t kMaxTwitchLogin = 25;
/** A Twitch login: 3 to kMaxTwitchLogin of a-z, 0-9 and _. */
bool ValidTwitchLogin(const std::string &login);
constexpr uint16_t kDefaultPort = 9410;

constexpr size_t kHeaderSize = 14;
constexpr size_t kTagSize = 16;
constexpr size_t kNonceSize = 12;
constexpr size_t kHandshakeNonceSize = 16;

/** Largest datagram ever sent. Fits the IPv6 minimum MTU (1280 - 40 - 8). */
constexpr size_t kMaxDatagram = 1232;
/** Video payload bytes per fragment; every fragment but the last is exactly this long. */
constexpr size_t kVideoFragmentSize = 1160;
constexpr size_t kVideoFragmentHeaderSize = 18;
/** Upper bound for a single encoded video frame. */
constexpr size_t kMaxVideoFrameSize = 2 * 1024 * 1024;
constexpr size_t kMaxPayload = kMaxDatagram - kHeaderSize - kTagSize;

constexpr size_t kMaxNameLength = 32;
constexpr size_t kMaxGuests = 8;

/** Upper bound for one Opus frame of a player's microphone. */
constexpr size_t kMaxVoiceFrameSize = 1000;
/** Webcam picture bytes per fragment; every fragment but the last is exactly this long. */
constexpr size_t kCamFragmentSize = 1150;
constexpr size_t kCamFragmentHeaderSize = 12;
/** Upper bound for a single webcam picture. */
constexpr size_t kMaxCamFrameSize = 256 * 1024;

enum class PacketType : uint8_t
{
	Hello = 0x01,           // guest -> host
	Welcome = 0x02,         // host -> guest
	Reject = 0x03,          // host -> guest
	/**
	 * host -> an address a guest left at the meeting point. Sealed like Welcome; holds
	 * the 8 bytes of that guest's id. Sending it opens the host's router for the guest,
	 * and it shows the guest which address the host's packets come from. Versions that
	 * do not know it ignore it.
	 */
	Punch = 0x04,
	/** Never a datagram: the type in the header of a note left at the meeting point (meet.h). */
	MeetNote = 0x05,
	/**
	 * host -> guest, sealed like Welcome, holds the guest's nonce: the Hello arrived, and
	 * the host has to let this guest in first (JoinPolicy::Ask). The guest keeps sending
	 * its Hello and waits longer. Versions that do not know it ignore it.
	 */
	Waiting = 0x06,

	Input = 0x10,           // guest -> host
	Ping = 0x11,            // guest -> host
	Pong = 0x12,            // host -> guest
	VoiceUp = 0x13,         // guest -> host
	CamUp = 0x14,           // guest -> host
	ChatUp = 0x15,          // guest -> host, answered with ChatUpAck
	ChatAck = 0x16,         // guest -> host
	/** guest -> host: mouse and keyboard for a game on the host's computer (KeyMouseState). */
	KeyMouse = 0x17,
	/** guest -> host, empty: wake the sleeping console (kParticipantMayWake, StreamInfo::console_asleep). */
	WakeUp = 0x18,

	Video = 0x20,           // host -> guest
	Audio = 0x21,           // host -> guest
	Haptics = 0x22,         // host -> guest
	State = 0x23,           // host -> guest
	VideoConfig = 0x24,     // host -> guest
	Inputs = 0x25,          // host -> guest
	Voice = 0x26,           // host -> guest
	Cam = 0x27,             // host -> guest
	Chat = 0x28,            // host -> guest, answered with ChatAck
	ChatUpAck = 0x29,       // host -> guest

	Nack = 0x30,            // guest -> host
	KeyframeRequest = 0x31, // guest -> host

	Bye = 0x40,             // both directions
	BlobRequest = 0x41,     // both directions
	BlobChunk = 0x42,       // both directions
};

enum class RejectReason : uint8_t
{
	Full = 1,
	Version = 2,
	/** The host did not let this player in, or removed and blocked it. */
	Refused = 3,
	/** The host has closed the session to newcomers. */
	Locked = 4,
};

enum class ByeReason : uint8_t
{
	Quit = 0,     // the sender left on purpose
	Kicked = 1,   // host removed the guest
	HostEnded = 2, // the host's console session ended
	/** The host lost its console and is getting it back: keep trying to join again. */
	Reconnecting = 3,
	/**
	 * A guest left and asks the host to put the console to sleep. Only a guest whose
	 * entry in the session state has kParticipantMaySleep sends it, and only a host that
	 * gave that flag acts on it; to every other host it is a guest that left.
	 */
	Sleep = 4
};

/**
 * Charge of a player's controller: 0..100 percent, or one of these.
 */
constexpr uint8_t kBatteryUnknown = 0xff; // no controller, or it does not say
constexpr uint8_t kBatteryWired = 0xfe;   // on a cable

/** Streams a guest asks the host to send. Controller feedback is always sent. */
enum WantFlags : uint8_t
{
	kWantVideo = 1 << 0,
	kWantAudio = 1 << 1,
	kWantHaptics = 1 << 2,
	/** What every player is pressing right now, for a stream overlay. */
	kWantInputs = 1 << 3,
	/** The other players' microphones. */
	kWantVoice = 1 << 4,
	/** The other players' webcams. */
	kWantCams = 1 << 5,
	kWantAll = kWantVideo | kWantAudio | kWantHaptics,
	kWantMask = kWantAll | kWantInputs | kWantVoice | kWantCams
};

/** What a player contributes to the conversation. */
enum MediaFlags : uint8_t
{
	kMediaVoice = 1 << 0,    // takes part in the voice chat (has a microphone open)
	kMediaMuted = 1 << 1,    // ... but has muted it
	kMediaCamera = 1 << 2,   // webcam is on
	/** Set by the host only: this player's microphone is not passed on to the others. */
	kMediaSilenced = 1 << 3,
	/**
	 * This player streams the session live (to Twitch, say) right now. Everybody sees it
	 * next to the name. Versions that do not know it ignore it, and hosts that do not
	 * know it leave it out of the session state.
	 */
	kMediaLive = 1 << 4,
	/**
	 * This player asks to be left out of everybody else's live streams: their voice,
	 * webcam and name do not go into them. Ignored like kMediaLive.
	 */
	kMediaNoStreams = 1 << 5,
	// A guest that is live on Twitch and wants it shown in the public room list adds its
	// channel's login (Str8, a-z0-9_, at most kMaxTwitchLogin) after the battery in its
	// Ping. Hosts that do not know it never read that far.
	/** What a player may say about itself. */
	kMediaOwnMask = kMediaVoice | kMediaMuted | kMediaCamera | kMediaLive | kMediaNoStreams
};

/**
 * A player's picture is identified by the first bytes of its SHA-256 and
 * fetched in chunks by whoever does not have it yet.
 */
constexpr size_t kBlobHashSize = 8;
constexpr size_t kBlobChunkSize = 1024;
constexpr size_t kMaxAvatarSize = 48 * 1024;
using BlobHash = std::array<uint8_t, kBlobHashSize>;

struct BlobRef
{
	BlobHash hash{};
	uint16_t size = 0; // 0 = none

	bool Valid() const { return size != 0; }
	bool operator==(const BlobRef &o) const { return size == o.size && hash == o.hash; }
	bool operator!=(const BlobRef &o) const { return !(*this == o); }
};

/*
 * Text chat. Limits are bytes of UTF-8; longer text is cut at a character
 * boundary by the sender.
 */
constexpr size_t kMaxChatText = 400;
constexpr size_t kMaxChatUrl = 256;
constexpr size_t kMaxChatSite = 48;
constexpr size_t kMaxChatTitle = 100;
constexpr size_t kMaxChatDescription = 160;
/** The picture of a link preview travels like a player's picture, see BlobRef. */
constexpr size_t kMaxChatImageSize = kMaxAvatarSize;
/** Messages a player who joins gets from before. */
constexpr size_t kChatHistorySize = 30;

enum ChatFlags : uint8_t
{
	/** Written before the receiver joined. */
	kChatHistory = 1 << 0
};

/** A preview of the first link in a message, made by the sender. */
struct ChatEmbed
{
	std::string url;
	std::string site;
	std::string title;
	std::string description;
	BlobRef image;

	bool Valid() const { return !url.empty(); }
};

struct ChatMessage
{
	/** Chat: the host's numbering. ChatUp: the sender's own. */
	uint32_t seq = 0;
	/** Chat: the number the writer gave it in its ChatUp (0 for the host's own), so it knows its message. */
	uint32_t up_id = 0;
	/** Chat: who wrote it. */
	uint8_t slot = 0;
	uint8_t flags = 0; // ChatFlags
	/** Chat: when the host got it, milliseconds since 1970 by the host's clock. */
	uint64_t time_ms = 0;
	/** Chat: the writer's name at the time, which stays when they have left. */
	std::string name;
	std::string text;
	ChatEmbed embed;
};

/** At most max bytes of s, never ending in the middle of a UTF-8 character. */
std::string TruncateUtf8(const std::string &s, size_t max);

void WriteChatMessage(ByteWriter &w, const ChatMessage &m);
bool ReadChatMessage(ByteReader &r, ChatMessage &m);

enum class ControllerKind : uint8_t
{
	None = 0,
	DualSense = 1,
	Xbox = 2,
	Other = 3,
	DualShock4 = 4
};

/** Maps a byte from the wire to a known kind. */
inline ControllerKind ControllerKindFromWire(uint8_t v)
{
	return v > static_cast<uint8_t>(ControllerKind::DualShock4) ? ControllerKind::Other : static_cast<ControllerKind>(v);
}

/** Same values as ChiakiCodec. */
enum class VideoCodec : uint8_t
{
	H264 = 0,
	H265 = 1,
	H265Hdr = 2
};

/* Same bit values as ChiakiControllerButton. */
enum PadButton : uint32_t
{
	kButtonCross = 1u << 0,
	kButtonCircle = 1u << 1,
	kButtonSquare = 1u << 2,
	kButtonTriangle = 1u << 3,
	kButtonDpadLeft = 1u << 4,
	kButtonDpadRight = 1u << 5,
	kButtonDpadUp = 1u << 6,
	kButtonDpadDown = 1u << 7,
	kButtonL1 = 1u << 8,
	kButtonR1 = 1u << 9,
	kButtonL3 = 1u << 10,
	kButtonR3 = 1u << 11,
	kButtonOptions = 1u << 12,
	kButtonShare = 1u << 13,
	kButtonTouchpad = 1u << 14,
	kButtonPs = 1u << 15
};

struct PadTouch
{
	uint16_t x = 0;
	uint16_t y = 0;
	int8_t id = -1; // -1 = not touching

	bool operator==(const PadTouch &o) const { return x == o.x && y == o.y && id == o.id; }
};

/** A controller state. Mirrors ChiakiControllerState. */
struct PadState
{
	uint32_t buttons = 0;
	uint8_t l2 = 0;
	uint8_t r2 = 0;
	int16_t left_x = 0;
	int16_t left_y = 0;
	int16_t right_x = 0;
	int16_t right_y = 0;
	uint8_t touch_id_next = 0;
	PadTouch touches[2];
	float gyro[3] = { 0.0f, 0.0f, 0.0f };
	float accel[3] = { 0.0f, 1.0f, 0.0f };
	float orient[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	bool operator==(const PadState &o) const;
	bool operator!=(const PadState &o) const { return !(*this == o); }
	/** True if gyro or accel differ from the resting defaults. */
	bool HasMotion() const;
};

void WritePadState(ByteWriter &w, const PadState &s);
bool ReadPadState(ByteReader &r, PadState &s);

/** Mouse buttons in KeyMouseState::buttons. */
enum MouseButton : uint8_t
{
	kMouseLeft = 1 << 0,
	kMouseRight = 1 << 1,
	kMouseMiddle = 1 << 2,
	kMouseBack = 1 << 3,
	kMouseForward = 1 << 4,
};

/**
 * A guest's mouse and keyboard. Movement and the wheel are running totals, so that a lost
 * packet loses nothing and a repeated one adds nothing; the host moves by the difference.
 */
struct KeyMouseState
{
	uint8_t buttons = 0; // MouseButton
	int32_t move_x = 0;
	int32_t move_y = 0;
	/** In steps of 1/120 of a notch, as Windows counts; up and right are positive. */
	int32_t wheel_y = 0;
	int32_t wheel_x = 0;
	/** The keys held down, as USB HID usages (keyboard page), at most kMaxKeys. */
	std::vector<uint8_t> keys;
	static constexpr size_t kMaxKeys = 16;
};

void WriteKeyMouseState(ByteWriter &w, const KeyMouseState &s);
bool ReadKeyMouseState(ByteReader &r, KeyMouseState &s);

struct AudioHeader
{
	bool valid = false;
	uint8_t channels = 0;
	uint8_t bits = 0;
	uint32_t rate = 0;
	uint32_t frame_size = 0;
	uint32_t unknown = 0;

	bool operator==(const AudioHeader &o) const
	{
		return valid == o.valid && channels == o.channels && bits == o.bits && rate == o.rate
			&& frame_size == o.frame_size && unknown == o.unknown;
	}
};

/** Where the game the guests see runs. */
enum class GameSource : uint8_t
{
	Console = 0,  // a PS4 or PS5 (StreamInfo::ps5 says which)
	Computer = 1, // the host's own computer: a window or a screen of it
	XboxApp = 2   // the Xbox app on the host's computer, with Remote Play or cloud gaming
};

struct StreamInfo
{
	VideoCodec codec = VideoCodec::H264;
	uint16_t width = 0;
	uint16_t height = 0;
	uint8_t fps = 0;
	bool ps5 = true;
	bool dualsense = false; // the console session has DualSense features enabled
	GameSource source = GameSource::Console;
	/** The console sleeps and the room stays: no picture until somebody wakes it. */
	bool console_asleep = false;

	bool operator==(const StreamInfo &o) const
	{
		return codec == o.codec && width == o.width && height == o.height && fps == o.fps && ps5 == o.ps5
			&& dualsense == o.dualsense && source == o.source && console_asleep == o.console_asleep;
	}
};

/**
 * Controller feedback coming from the console. Each group has a sequence
 * number that changes whenever the console sends a new value, so a receiver
 * applies a group only when its number differs from the last one it applied.
 * A sequence number of 0 means the console has not sent that group yet.
 */
struct Feedback
{
	uint16_t rumble_seq = 0;
	uint8_t rumble_left = 0;
	uint8_t rumble_right = 0;

	uint16_t trigger_seq = 0;
	uint8_t trigger_type_left = 0;
	uint8_t trigger_type_right = 0;
	std::array<uint8_t, 10> trigger_left{};
	std::array<uint8_t, 10> trigger_right{};

	uint16_t led_seq = 0;
	std::array<uint8_t, 3> led{};

	uint16_t player_index_seq = 0;
	uint8_t player_index = 0;

	uint16_t haptic_intensity_seq = 0;
	uint8_t haptic_intensity = 0;

	uint16_t trigger_intensity_seq = 0;
	uint8_t trigger_intensity = 0;

	uint16_t motion_reset_seq = 0;
};

enum ParticipantFlags : uint8_t
{
	kParticipantHost = 1 << 0,
	kParticipantCanControl = 1 << 1,
	kParticipantInControl = 1 << 2, // currently contributes input to the console
	/** May put the console to sleep when leaving (ByeReason::Sleep). The host always may. */
	kParticipantMaySleep = 1 << 3,
	/** The host lets this player use mouse and keyboard in the shared game (a game on its computer). */
	kParticipantKeyMouse = 1 << 4,
	/** The room stays while the console sleeps, and this player may wake it (PacketType::WakeUp). */
	kParticipantMayWake = 1 << 5
};

struct Participant
{
	uint8_t slot = 0; // 0 = host, guests start at 1
	uint8_t flags = 0;
	std::string name;
	BlobRef avatar;
	ControllerKind controller = ControllerKind::None;
	uint8_t media = 0; // MediaFlags
	uint8_t battery = kBatteryUnknown;
	/** MixMode::EachOwn: the player the game sees (1 the host, 2.. the guests); 0 if none (watching). */
	uint8_t player = 0;
};

/** Buttons, triggers and sticks of one player, without touch and motion. */
struct PlayerInput
{
	uint8_t slot = 0;
	uint32_t buttons = 0;
	uint8_t l2 = 0;
	uint8_t r2 = 0;
	int16_t left_x = 0;
	int16_t left_y = 0;
	int16_t right_x = 0;
	int16_t right_y = 0;

	bool operator==(const PlayerInput &o) const
	{
		return slot == o.slot && buttons == o.buttons && l2 == o.l2 && r2 == o.r2 && left_x == o.left_x
			&& left_y == o.left_y && right_x == o.right_x && right_y == o.right_y;
	}
};

PlayerInput ToPlayerInput(uint8_t slot, const PadState &state);

/** What everybody is pressing, and what the console gets out of it. */
struct InputsSnapshot
{
	uint32_t seq = 0;
	std::vector<PlayerInput> players;
	PlayerInput combined;

	bool SameInputs(const InputsSnapshot &o) const { return players == o.players && combined == o.combined; }
};

void WriteInputs(ByteWriter &w, const InputsSnapshot &s);
bool ReadInputs(ByteReader &r, InputsSnapshot &s);

struct BlobChunkHeader
{
	BlobHash hash{};
	uint16_t index = 0;
	uint16_t count = 0;
	uint32_t total_size = 0;
};

inline uint32_t BlobChunkCount(size_t size)
{
	return size == 0 ? 0 : static_cast<uint32_t>((size + kBlobChunkSize - 1) / kBlobChunkSize);
}

/** How the host combines everyone's controllers. */
enum class MixMode : uint8_t
{
	Together = 0,   // everyone controls at the same time
	LastActive = 1, // one player at a time, control passes to whoever plays while the others rest
	HostOnly = 2,   // guests watch
	/**
	 * Every player on a controller of their own: the host is player 1, the guests 2, 3, 4.
	 * Only for a game on the host's computer (GameSource::Computer), which takes several
	 * controllers; a console takes one.
	 */
	EachOwn = 3
};

/** Snapshot the host sends to each guest on change and periodically. */
struct SessionState
{
	uint32_t state_seq = 0;
	StreamInfo stream;
	AudioHeader audio;
	uint16_t video_config_id = 0; // 0 = none yet
	Feedback feedback;
	MixMode mix_mode = MixMode::Together;
	uint8_t your_slot = 0;
	std::vector<Participant> participants;
};

void WriteSessionState(ByteWriter &w, const SessionState &s);
bool ReadSessionState(ByteReader &r, SessionState &s);

struct HelloPayload
{
	std::array<uint8_t, kHandshakeNonceSize> guest_nonce{};
	uint8_t want = kWantAll;
	ControllerKind controller = ControllerKind::None;
	std::string name;
	BlobRef avatar;
	uint8_t media = 0; // MediaFlags the guest starts with
	uint8_t battery = kBatteryUnknown;
	/**
	 * Random, the same for as long as the app runs. A guest that joins again
	 * (after a connection loss, say) takes the place it had instead of
	 * appearing twice until the old one times out. 0 = none.
	 */
	uint64_t player_id = 0;
};

void WriteHello(ByteWriter &w, const HelloPayload &h);
bool ReadHello(ByteReader &r, HelloPayload &h);

struct WelcomePayload
{
	std::array<uint8_t, kHandshakeNonceSize> guest_nonce{};
	std::array<uint8_t, kHandshakeNonceSize> host_nonce{};
	uint32_t conn_id = 0;
	uint8_t slot = 0;
	std::string host_name;
	StreamInfo stream;
	AudioHeader audio;
};

void WriteWelcome(ByteWriter &w, const WelcomePayload &p);
bool ReadWelcome(ByteReader &r, WelcomePayload &p);

enum VideoFragmentFlags : uint8_t
{
	kVideoKey = 1 << 0,        // frame can be decoded without earlier frames
	kVideoRecovered = 1 << 1,  // host's own stream recovered from loss at this frame
	kVideoRetransmit = 1 << 2  // sent in answer to a Nack
};

struct VideoFragmentHeader
{
	uint32_t frame_seq = 0;
	uint16_t frag_index = 0;
	uint16_t frag_count = 0;
	uint8_t flags = 0;
	uint8_t frames_lost = 0; // frames the host itself lost right before this one
	uint32_t total_size = 0;
	/** Host clock (milliseconds, wraps) when the frame came from the console. */
	uint32_t capture_ms = 0;
};

void WriteVideoFragmentHeader(ByteWriter &w, const VideoFragmentHeader &h);
bool ReadVideoFragmentHeader(ByteReader &r, VideoFragmentHeader &h);

/** Number of fragments a frame of the given size is split into. */
inline uint32_t VideoFragmentCount(size_t total_size)
{
	return total_size == 0 ? 0 : static_cast<uint32_t>((total_size + kVideoFragmentSize - 1) / kVideoFragmentSize);
}

/** One entry of a Nack packet. An empty list of fragments asks for the whole frame. */
struct NackEntry
{
	uint32_t frame_seq = 0;
	std::vector<uint16_t> fragments;
};

constexpr uint16_t kNackWholeFrame = 0xffff;

/** Appends as many entries as fit into one packet; returns how many were written. */
size_t WriteNack(ByteWriter &w, const std::vector<NackEntry> &entries, size_t first, size_t max_payload);
bool ReadNack(ByteReader &r, std::vector<NackEntry> &entries);

/**
 * One piece of a webcam picture. Pictures do not depend on each other, so a
 * lost one is simply skipped; nothing is sent again.
 */
struct CamFragmentHeader
{
	uint32_t frame_seq = 0;
	uint16_t frag_index = 0;
	uint16_t frag_count = 0;
	uint32_t total_size = 0;
};

void WriteCamFragmentHeader(ByteWriter &w, const CamFragmentHeader &h);
bool ReadCamFragmentHeader(ByteReader &r, CamFragmentHeader &h);

inline uint32_t CamFragmentCount(size_t total_size)
{
	return total_size == 0 ? 0 : static_cast<uint32_t>((total_size + kCamFragmentSize - 1) / kCamFragmentSize);
}

} // namespace coplay

#endif // COPLAY_PROTOCOL_H
