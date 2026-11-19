// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_HOST_H
#define COPLAY_HOST_H

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "crypto.h"
#include "mixer.h"
#include "protocol.h"
#include "socket.h"
#include "video.h"

namespace coplay {

struct HostConfig
{
	uint16_t port = kDefaultPort;
	Secret secret{};
	std::string name = "Host";
	size_t max_guests = 4;
	/** A guest that was silent this long is dropped. */
	uint32_t guest_timeout_ms = 6000;
	uint32_t state_interval_ms = 250;
	/** on_keyframe_needed fires at most this often. */
	uint32_t keyframe_min_interval_ms = 400;
	InputMixer::Config mixer;
};

struct GuestInfo
{
	uint8_t slot = 0;
	std::string name;
	std::string address;
	uint8_t want = 0;
	ControllerKind controller = ControllerKind::None;
	bool can_control = true;
	bool in_control = false;
	uint32_t rtt_ms = 0;
};

/**
 * All callbacks are optional and are called from the host's own threads,
 * never while an internal lock is held. They must not block for long.
 */
struct HostCallbacks
{
	/** The combined controller state changed; fetch it with MixedInput(). */
	std::function<void()> on_input_changed;
	/** A guest cannot decode until the console sends a new key frame. */
	std::function<void()> on_keyframe_needed;
	/** Someone joined, left or changed; see Guests(). */
	std::function<void()> on_guests_changed;
	std::function<void(const std::string &)> on_log;
};

/**
 * The sharing side of a co-play session. Feed it what the console sends
 * (video, audio, haptics, controller feedback) and the local controller
 * state; it relays the former to the guests and combines the latter with the
 * guests' controllers.
 */
class Host
{
	public:
		Host(const HostConfig &config, const HostCallbacks &callbacks);
		~Host();
		Host(const Host &) = delete;
		Host &operator=(const Host &) = delete;

		bool Start(std::string *error);
		/** Tells the guests the session is over and stops. */
		void Stop();
		bool IsRunning() const { return running; }
		uint16_t Port() const;

		void SetStreamInfo(const StreamInfo &info);
		void SetAudioHeader(const AudioHeader &header);

		/** One buffer exactly as the console's video receiver delivers it. */
		void PushVideo(const uint8_t *data, size_t size, int32_t frames_lost, bool recovered);
		/** One Opus frame. */
		void PushAudio(const uint8_t *data, size_t size);
		/** One frame of raw haptics PCM. */
		void PushHaptics(const uint8_t *data, size_t size);

		void SetRumble(uint8_t left, uint8_t right);
		void SetTriggerEffects(uint8_t type_left, uint8_t type_right, const uint8_t left[10], const uint8_t right[10]);
		void SetLed(const uint8_t rgb[3]);
		void SetPlayerIndex(uint8_t index);
		void SetHapticIntensity(uint8_t intensity);
		void SetTriggerIntensity(uint8_t intensity);
		void NotifyMotionReset();

		/** The host's own (already combined) local controllers. */
		void SetLocalInput(const PadState &state);
		/** What to send to the console right now. */
		PadState MixedInput();

		void SetMixMode(MixMode mode);
		MixMode GetMixMode();
		void SetGuestCanControl(uint8_t slot, bool allowed);
		void KickGuest(uint8_t slot);
		std::vector<GuestInfo> Guests();
		/** Whether the host's own controllers currently reach the console (see MixMode). */
		bool HostInControl();
		bool AnyGuestWantsVideo();

		/** Outgoing packet loss for tests and the simulator. */
		void SetSimulatedLoss(double probability, uint32_t seed = 1) { socket.SetSimulatedLoss(probability, seed); }

	private:
		struct Guest
		{
			uint32_t conn_id = 0;
			uint8_t slot = 0;
			Address addr;
			std::string name;
			HandshakeNonce guest_nonce{};
			SessionKeys keys;
			uint64_t send_counter = 0;
			ReplayWindow replay;
			bool confirmed = false;
			uint64_t created_ms = 0;
			uint64_t last_rx_ms = 0;
			uint64_t last_state_ms = 0;
			uint8_t want = kWantAll;
			ControllerKind controller = ControllerKind::None;
			bool can_control = true;
			bool in_control = false;
			bool awaiting_key = true;
			bool have_input = false;
			uint32_t last_input_seq = 0;
			uint32_t rtt_ms = 0;
			std::vector<uint8_t> welcome_packet;
		};

		/** Things to do once the lock is released. */
		struct Deferred
		{
			bool input_changed = false;
			bool keyframe_needed = false;
			bool guests_changed = false;
			std::vector<std::string> logs;
		};

		void ThreadMain();
		void HandlePacket(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms, Deferred &d);
		void HandleHello(const uint8_t *data, size_t size, const PacketHeader &header, const Address &from, uint64_t now_ms, Deferred &d);
		void HandleNack(Guest &g, const std::vector<uint8_t> &payload);
		void Tick(uint64_t now_ms, Deferred &d);
		void Fire(Deferred &d);

		bool SendToGuest(Guest &g, PacketType type, const std::vector<uint8_t> &payload);
		void SendState(Guest &g, uint64_t now_ms);
		void BroadcastState(uint64_t now_ms);
		void SendVideoConfig(Guest &g);
		void SendFrame(Guest &g, const FrameCache::Entry &entry);
		void SendBye(Guest &g, ByeReason reason);
		void RemoveGuest(uint32_t conn_id, Deferred &d, const std::string &why);
		void RequestKeyframe(uint64_t now_ms, Deferred &d);
		void UpdateControl(uint64_t now_ms, Deferred &d);
		void BumpSeq(uint16_t &seq);
		Guest *FindBySlot(uint8_t slot);

		HostConfig config;
		HostCallbacks callbacks;
		UdpSocket socket;
		std::thread thread;
		std::atomic<bool> running{false};
		std::atomic<bool> stop_requested{false};

		std::mutex mutex;
		Key hello_key;
		Key welcome_key;
		std::map<uint32_t, std::unique_ptr<Guest>> guests; // by conn_id
		std::deque<HandshakeNonce> seen_nonces;
		InputMixer mixer;
		PadState last_reported_mix;
		bool host_in_control = true;

		StreamInfo stream;
		AudioHeader audio;
		Feedback feedback;
		uint32_t state_seq = 1;
		uint64_t fast_state_until_ms = 0;

		std::vector<uint8_t> video_config;
		uint16_t video_config_id = 0;
		FrameCache cache;
		uint32_t next_frame_seq = 1;
		uint32_t audio_seq = 0;
		uint32_t haptics_seq = 0;
		uint64_t last_keyframe_request_ms = 0;
		bool keyframe_pending = false;
		bool keyframe_outstanding = false;
};

} // namespace coplay

#endif // COPLAY_HOST_H
