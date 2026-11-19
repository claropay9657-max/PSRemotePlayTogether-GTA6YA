// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_CLIENT_H
#define COPLAY_CLIENT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "blob.h"
#include "crypto.h"
#include "invite.h"
#include "media.h"
#include "protocol.h"
#include "socket.h"
#include "video.h"

namespace coplay {

struct ClientConfig
{
	Invite invite;
	std::string name;
	/** Our picture (an image file's bytes, at most kMaxAvatarSize), or empty. */
	std::vector<uint8_t> avatar;
	uint8_t want = kWantAll;
	ControllerKind controller = ControllerKind::None;
	/** MediaFlags we start with; see Client::SetMedia(). */
	uint8_t media = 0;
	uint32_t connect_timeout_ms = 5000;
	/** The session counts as lost after this long without a packet from the host. */
	uint32_t host_timeout_ms = 6000;
	FrameAssembler::Config assembler;
};

enum class DisconnectReason
{
	HostEnded, // the host stopped sharing or its console session ended
	Kicked,
	Timeout
};

/**
 * All callbacks are optional and are called from the client's network
 * thread, in the order the data arrived. Buffers are only valid during the
 * call.
 */
struct ClientCallbacks
{
	std::function<void(const SessionState &)> on_state;
	/** Parameter sets (SPS/PPS/VPS); always arrives before the first frame that needs it. */
	std::function<void(const uint8_t *, size_t)> on_video_config;
	std::function<void(const AssembledFrame &)> on_video_frame;
	std::function<void(const uint8_t *, size_t)> on_audio;
	std::function<void(const uint8_t *, size_t)> on_haptics;
	/** Needs kWantInputs. */
	std::function<void(const InputsSnapshot &)> on_inputs;
	/** A player's picture has arrived; fetch it with Client::Avatar(). */
	std::function<void(const BlobRef &)> on_avatar;
	/** One Opus frame of another player's microphone. Needs kWantVoice. */
	std::function<void(uint8_t slot, const uint8_t *, size_t)> on_voice;
	/** A complete picture of another player's webcam. Needs kWantCams. */
	std::function<void(uint8_t slot, const std::vector<uint8_t> &jpeg)> on_cam_frame;
	/** Not called after Stop(). */
	std::function<void(DisconnectReason)> on_disconnected;
};

struct ClientStats
{
	uint32_t rtt_ms = 0;
	uint64_t packets_received = 0;
	uint64_t bytes_received = 0;
	uint64_t audio_frames = 0;
	uint64_t haptics_frames = 0;
	uint64_t voice_frames = 0;
	uint64_t cam_frames = 0;
	FrameAssembler::Stats video;
};

/** The joining side of a co-play session. */
class Client
{
	public:
		explicit Client(const ClientConfig &config);
		~Client();
		Client(const Client &) = delete;
		Client &operator=(const Client &) = delete;

		/**
		 * Finds the host and performs the handshake. Blocks for up to
		 * connect_timeout_ms; CancelConnect() from another thread ends it early.
		 */
		bool Connect(std::string *error);
		void CancelConnect() { cancel_connect = true; }
		bool IsConnected() const { return connected; }
		/** What the host said about the session. Valid after Connect() succeeded. */
		const WelcomePayload &Welcome() const { return welcome; }
		std::string HostAddress() const;

		/** Starts receiving. Call once, after Connect(). */
		void Start(const ClientCallbacks &callbacks);
		/** Says goodbye to the host and stops. */
		void Stop();

		void SetInput(const PadState &state);
		/** Changes which streams the host sends, for example to switch to controller-only. */
		void SetWant(uint8_t want);
		uint8_t Want() const { return want; }
		/** Tells the others which kind of controller we hold (it can change during a session). */
		void SetController(ControllerKind kind);
		/** What we contribute: kMediaVoice, kMediaMuted, kMediaCamera. */
		void SetMedia(uint8_t media);
		uint8_t Media() const { return media; }
		/** One Opus frame of our microphone. */
		void SendVoice(const uint8_t *data, size_t size);
		/** One picture of our webcam. */
		void SendCamFrame(const uint8_t *data, size_t size);
		ClientStats Stats();
		BlobRef OwnAvatar() const { return own_avatar; }
		/** A player's picture, or null if it is not (yet) here. */
		BlobData Avatar(const BlobHash &hash);

		/** Outgoing packet loss for tests and the simulator. */
		void SetSimulatedLoss(double probability, uint32_t seed = 1);

	private:
		void ThreadMain();
		void HandlePacket(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms);
		void Tick(uint64_t now_ms);
		bool Send(PacketType type, const std::vector<uint8_t> &payload);
		void SendInput(uint64_t now_ms);
		void SendPing(uint64_t now_ms);

		ClientConfig config;
		ClientCallbacks callbacks;
		std::unique_ptr<UdpSocket> socket;
		Address host_addr;
		std::thread thread;
		std::atomic<bool> connected{false};
		std::atomic<bool> started{false};
		std::atomic<bool> stop_requested{false};
		std::atomic<bool> cancel_connect{false};
		std::atomic<bool> lost{false};

		Key hello_key;
		Key welcome_key;
		WelcomePayload welcome;
		SessionKeys keys;
		ReplayWindow replay;

		std::mutex send_mutex; // send_counter and host_addr
		uint64_t send_counter = 0;

		std::mutex input_mutex;
		PadState input;
		uint32_t input_seq = 0;
		int input_burst = 0;
		uint64_t last_input_send_ms = 0;

		std::atomic<uint8_t> want{kWantAll};
		std::atomic<bool> ping_now{false};
		std::atomic<uint8_t> controller{0};
		std::atomic<uint8_t> media{0};
		std::atomic<uint32_t> voice_seq{0};
		std::atomic<uint32_t> cam_seq{0};
		std::atomic<uint32_t> rtt_ms{0};
		double loss_probability = 0.0;
		uint32_t loss_seed = 1;

		// Network thread only
		FrameAssembler assembler;
		uint8_t applied_want = 0;
		bool have_video_config = false;
		uint16_t video_config_id = 0;
		bool have_state = false;
		uint32_t last_state_seq = 0;
		bool have_audio = false;
		uint32_t last_audio_seq = 0;
		bool have_haptics = false;
		uint32_t last_haptics_seq = 0;
		uint64_t last_rx_ms = 0;
		uint64_t last_ping_ms = 0;
		uint32_t ping_id = 0;
		uint32_t pings_sent = 0;

		std::mutex stats_mutex;
		ClientStats stats;

		std::mutex blob_mutex;
		BlobStore blobs;
		BlobRef own_avatar;
		bool have_inputs = false;
		uint32_t last_inputs_seq = 0;
		uint64_t last_blob_poll_ms = 0;

		// Network thread only; one entry per slot
		VoiceSequence voice_in[kMaxGuests + 1];
		CamAssembler cam_in[kMaxGuests + 1];
};

} // namespace coplay

#endif // COPLAY_CLIENT_H
