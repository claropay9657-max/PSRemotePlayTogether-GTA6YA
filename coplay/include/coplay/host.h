// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_HOST_H
#define COPLAY_HOST_H

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "blob.h"
#include "crypto.h"
#include "media.h"
#include "meet.h"
#include "mixer.h"
#include "protocol.h"
#include "socket.h"
#include "video.h"
#include "web.h"

namespace coplay {

/** Who gets in: what a host chooses for a session anybody can find (a public room). */
enum class JoinPolicy : uint8_t
{
	/** Everybody with the invite gets in and plays. */
	Open = 0,
	/** Everybody gets in, but watches until the host lets them play. */
	WatchFirst = 1,
	/** The host lets each newcomer in, or not (Host::Pending(), Host::Admit()). */
	Ask = 2,
};

/** Somebody who knocked and waits for the host to let them in (JoinPolicy::Ask). */
struct PendingGuest
{
	uint64_t id = 0;
	std::string name;
	/** Knocked from a web browser. */
	bool web = false;
};

struct HostConfig
{
	uint16_t port = kDefaultPort;
	Secret secret{};
	std::string name = "Host";
	/** The host's picture (an image file's bytes, at most kMaxAvatarSize), or empty. */
	std::vector<uint8_t> avatar;
	ControllerKind controller = ControllerKind::None;
	/** Charge of the host's controller: 0..100, kBatteryWired or kBatteryUnknown. */
	uint8_t battery = kBatteryUnknown;
	/** MediaFlags the host starts with; see Host::SetLocalMedia(). */
	uint8_t media = 0;
	size_t max_guests = 4;
	/** A guest that was silent this long is dropped. */
	uint32_t guest_timeout_ms = 6000;
	uint32_t state_interval_ms = 250;
	/** on_keyframe_needed fires at most this often. */
	uint32_t keyframe_min_interval_ms = 400;
	InputMixer::Config mixer;
	/** Guests may put the console to sleep when they leave; see Host::SetGuestsMaySleep(). */
	bool guests_may_sleep = false;
	/** Who gets in; see Host::SetJoinPolicy(). */
	JoinPolicy join_policy = JoinPolicy::Open;
	/** A guest that knocked and stopped asking is forgotten after this long. */
	uint32_t pending_timeout_ms = 8000;
	/** Lets friends in without a forwarded port (meet.h). Off unless a URL and a way to ask it are given. */
	MeetConfig meet;
	/** Lets friends in who join from a web browser (web.h). Needs the meeting point. */
	WebConfig web;
	/** Makes the WebRTC side instead of CreateWebGateway(). For tests. */
	WebGatewayFactory web_factory;
};

/** How friends elsewhere can reach this host, as far as the host can tell. */
struct HostReach
{
	/** Off when no meeting point is used. */
	MeetState meet = MeetState::Off;
	/** Why the meeting point could not be asked; empty unless meet is Failed. */
	std::string meet_error;
	/** The session's address and port on the internet as a STUN server saw them ("203.0.113.7:9410"); empty until one has answered. */
	std::string public_address;
	/** No STUN server has answered although several were asked several times. */
	bool stun_silent = false;
	/**
	 * Stable: friends can be told where to send, hole punching can work.
	 * PerDestination: this router gives every connection a new port; only friends
	 * whose own router is easy-going get in without a forwarded port.
	 */
	PortMapping mapping = PortMapping::Unknown;

	bool operator==(const HostReach &o) const
	{
		return meet == o.meet && meet_error == o.meet_error && public_address == o.public_address
			&& stun_silent == o.stun_silent && mapping == o.mapping;
	}
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
	/** Valid once the guest's picture has arrived completely; see Host::Avatar(). */
	BlobRef avatar;
	/** MediaFlags: voice chat, muted, webcam, and kMediaSilenced if the host cut its microphone off. */
	uint8_t media = 0;
	/** 0..100, kBatteryWired or kBatteryUnknown. */
	uint8_t battery = kBatteryUnknown;
	/** Joined from a web browser. */
	bool web = false;
	/** The guest is live on Twitch and wants it shown in the public room list: its login, else empty. */
	std::string twitch;
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
	/** One Opus frame of a guest's microphone. */
	std::function<void(uint8_t slot, const uint8_t *data, size_t size)> on_voice;
	/** A complete picture of a guest's webcam. */
	std::function<void(uint8_t slot, const std::vector<uint8_t> &jpeg)> on_cam_frame;
	/** A guest wrote in the chat (the host's own messages come back from SendChat()). */
	std::function<void(const ChatMessage &)> on_chat;
	/** The picture of a link preview has arrived; fetch it with Host::ChatImage(). */
	std::function<void(const BlobRef &)> on_chat_image;
	/** What Host::Reach() says has changed. */
	std::function<void()> on_reach_changed;
	/**
	 * A guest left and asked for the console to be put to sleep, and guests are allowed to.
	 * The guest is gone from Guests() by now; nothing else has happened yet.
	 */
	std::function<void(uint8_t slot, const std::string &name)> on_sleep_requested;
	/** Somebody knocked, or stopped waiting; see Host::Pending(). */
	std::function<void()> on_pending_changed;
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
		/**
		 * Stops like Stop(), but tells the guests that the host is getting its
		 * console back and will share again in a moment, so that they wait and
		 * join again instead of leaving.
		 */
		void StopForReconnect();
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

		/** One Opus frame of the host's own microphone, for the guests. */
		void PushVoice(const uint8_t *data, size_t size);
		/** One picture of the host's own webcam, for the guests. */
		void PushCamFrame(const uint8_t *data, size_t size);
		/** What the host itself contributes: kMediaVoice, kMediaMuted, kMediaCamera. */
		void SetLocalMedia(uint8_t media);
		uint8_t LocalMedia();
		/** Stops (or resumes) passing a guest's microphone on to everybody else. */
		void SetGuestSilenced(uint8_t slot, bool silenced);

		/**
		 * Writes in the chat as the host. image is the preview picture of the
		 * embed (JPEG, at most kMaxChatImageSize) or empty. @return the message as
		 * everybody gets it; seq is 0 if it could not be sent
		 */
		ChatMessage SendChat(const std::string &text, const ChatEmbed &embed, const std::vector<uint8_t> &image);
		/** The picture of a link preview, or null if it is not (yet) here. */
		BlobData ChatImage(const BlobHash &hash);

		void SetRumble(uint8_t left, uint8_t right);
		void SetTriggerEffects(uint8_t type_left, uint8_t type_right, const uint8_t left[10], const uint8_t right[10]);
		void SetLed(const uint8_t rgb[3]);
		void SetPlayerIndex(uint8_t index);
		void SetHapticIntensity(uint8_t intensity);
		void SetTriggerIntensity(uint8_t intensity);
		void NotifyMotionReset();

		/** The host's own (already combined) local controllers. */
		void SetLocalInput(const PadState &state);
		/** What to send to the console right now (MixMode::EachOwn: player 1, the host). */
		PadState MixedInput();
		/**
		 * MixMode::EachOwn: what a guest's own controller gets right now (idle for a guest that
		 * may not play or went quiet). on_input_changed also fires when only this changes.
		 */
		PadState GuestInput(uint8_t slot);
		/**
		 * What the guests play on the one controller they share, without the host's own input:
		 * for a game on the host's computer, which the host's own controller reaches directly.
		 */
		PadState GuestsInput();

		/**
		 * MixMode::EachOwn: rumble for one guest only, from the game's controller of that
		 * player; the others keep what SetRumble() says.
		 */
		void SetGuestRumble(uint8_t slot, uint8_t left, uint8_t right);
		/**
		 * MixMode::EachOwn: the player a guest is in the game (0 is player 1, the host), which its
		 * controller's lights show and everybody's list of players; kNoPlayer: none, it watches.
		 */
		void SetGuestPlayerIndex(uint8_t slot, uint8_t index);
		static constexpr uint8_t kNoPlayer = 0xff;

		/** The kind of controller the host itself holds. */
		void SetController(ControllerKind kind);
		/** Charge of the host's own controller: 0..100, kBatteryWired or kBatteryUnknown. */
		void SetBattery(uint8_t battery);
		void SetMixMode(MixMode mode);
		MixMode GetMixMode();
		void SetGuestCanControl(uint8_t slot, bool allowed);
		/**
		 * Whether a guest that leaves may ask for the console to be put to sleep. Guests are
		 * told, so that their app only offers it when it will work.
		 */
		void SetGuestsMaySleep(bool allowed);
		bool GuestsMaySleep();
		/**
		 * Who gets in from now on. Players who are in stay in; a player who was let in or
		 * refused once keeps that answer for the rest of the session.
		 */
		void SetJoinPolicy(JoinPolicy policy);
		JoinPolicy GetJoinPolicy();
		/** Who waits to be let in (JoinPolicy::Ask). */
		std::vector<PendingGuest> Pending();
		/** Lets a waiting player in, or not; it is told at its next Hello. */
		void Admit(uint64_t pending_id, bool allow);
		/** Closed: nobody new gets in, but players who are in, or were, may come back. */
		void SetLocked(bool locked);
		bool Locked();
		/** @param block also refuses this player for the rest of the session */
		void KickGuest(uint8_t slot, bool block = false);
		std::vector<GuestInfo> Guests();
		/** Whether the host's own controllers currently reach the console (see MixMode). */
		bool HostInControl();
		bool AnyGuestWantsVideo();

		/** What every player is pressing right now, and the combined result. */
		InputsSnapshot Inputs();
		BlobRef OwnAvatar();
		/** A player's picture, or null if it is not (yet) here. */
		BlobData Avatar(const BlobHash &hash);

		/** How friends elsewhere can reach this host; see HostCallbacks::on_reach_changed. */
		HostReach Reach();
		/** Whether friends can join from a web browser right now. */
		bool WebGuestsOn() const { return web_on; }

		/** Outgoing packet loss for tests and the simulator. */
		void SetSimulatedLoss(double probability, uint32_t seed = 1) { socket.SetSimulatedLoss(probability, seed); }
		/** A router without a forwarded port in front of this host, for tests and the simulator. */
		void SetSimulatedFilter(UdpSocket::Filter filter) { socket.SetSimulatedFilter(filter); }

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
			uint8_t battery = kBatteryUnknown;
			uint64_t player_id = 0;
			bool can_control = true;
			bool in_control = false;
			bool awaiting_key = true;
			bool have_input = false;
			uint32_t last_input_seq = 0;
			uint32_t rtt_ms = 0;
			BlobRef avatar;
			std::vector<uint8_t> welcome_packet;
			uint8_t media = 0;
			bool silenced = false;
			/** The guest's Twitch login for the public room list, or empty. */
			std::string twitch;
			VoiceSequence voice_seq;
			CamAssembler cam;
			/** Chat messages it has not acknowledged yet. */
			struct PendingChat
			{
				uint64_t first_ms = 0;
				uint64_t last_ms = 0;
				bool history = false;
			};
			std::map<uint32_t, PendingChat> chat_pending; // by seq
			std::deque<uint32_t> chat_up_seen; // its own numbers of what it wrote lately
			/** Not 0: a guest in a web browser, reached through the WebRTC gateway under this id. */
			uint64_t web_peer = 0;
			/** Where the browser is, for Guests(). */
			std::string web_address;
			/** Since when packets for it have been piling up; 0 = they are not. */
			uint64_t web_backlog_ms = 0;
			/** Its connection cannot keep up: video is held back until the pile is gone. */
			bool web_starved = false;
		};

		/** Things to do once the lock is released. */
		struct Deferred
		{
			bool input_changed = false;
			bool keyframe_needed = false;
			bool guests_changed = false;
			bool reach_changed = false;
			/** A guest asked for the console to be put to sleep: its slot and name. */
			bool sleep_requested = false;
			bool pending_changed = false;
			uint8_t sleep_slot = 0;
			std::string sleep_name;
			std::vector<std::string> logs;
			std::vector<std::pair<uint8_t, std::vector<uint8_t>>> voice; // slot, Opus frame
			std::vector<std::pair<uint8_t, std::vector<uint8_t>>> cams;  // slot, picture
			std::vector<ChatMessage> chats;
			std::vector<BlobRef> chat_images;
		};

		/** Whether a Hello gets a Welcome; see CheckAdmission(). */
		enum class Admission { In, Wait, Refused, Locked };
		/**
		 * What the join policy, the lock and the host's answers say about this Hello. A
		 * player is told apart by its player id, or by its nonce if it sends none.
		 */
		Admission CheckAdmission(const HelloPayload &hello, bool web, uint64_t now_ms, Deferred &d);
		/** Whether a newcomer starts out with control, by the host's earlier choice or the policy. */
		bool StartsInControl(uint64_t player_id) const;
		void TickPending(uint64_t now_ms, Deferred &d);
		struct Knock
		{
			uint64_t id = 0;
			uint64_t player = 0; // player id, or a value made from the nonce
			std::string name;
			bool web = false;
			uint64_t last_ms = 0;
		};
		std::vector<Knock> knocks;
		uint64_t next_knock_id = 1;
		/** Players the host let in (or who were in) and refused or blocked, for this session. */
		std::set<uint64_t> admitted_players;
		std::set<uint64_t> refused_players;
		/** The host's own choice of who plays and who watches, so that it holds when they come back. */
		std::map<uint64_t, bool> control_choice;
		bool locked = false;

		void StopWith(ByeReason reason);
		void ThreadMain();
		void HandlePacket(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms, Deferred &d);
		void HandleHello(const uint8_t *data, size_t size, const PacketHeader &header, const Address &from, uint64_t now_ms, Deferred &d);
		/** A packet of a guest that is known to come from it. @return false if the guest is gone afterwards */
		bool HandleGuestPacket(Guest &g, PacketType type, const uint8_t *payload, size_t size, uint64_t now_ms, Deferred &d);
		/** The guest is in: it counts from now on. */
		void ConfirmGuest(Guest &g, uint64_t now_ms, Deferred &d);
		/** The lowest free place, or the one asked for if it is free; 0 if there is none. */
		uint8_t FreeSlot(uint8_t preferred);
		void HandleNack(Guest &g, const uint8_t *payload, size_t size);
		// Guests in a web browser. The three On... are called on the gateway's thread.
		void OnWebMessage(uint64_t peer_id, const uint8_t *data, size_t size);
		void OnWebClosed(uint64_t peer_id);
		void OnWebAnswers();
		void HandleWebHello(uint64_t peer_id, const uint8_t *payload, size_t size, uint64_t now_ms, Deferred &d);
		/** On the meeting point's thread: notes that are not the app's own. */
		void OnOtherNotes(const std::vector<std::string> &texts);
		Guest *FindByWebPeer(uint64_t peer_id);
		/** Whether video should be held back from this browser guest right now. */
		bool WebHoldVideo(Guest &g, uint64_t now_ms);
		void TickWeb(uint64_t now_ms, Deferred &d);
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
		void BuildInputs(uint64_t now_ms, InputsSnapshot &s);
		void SendInputs(uint64_t now_ms);
		void PollBlobs(uint64_t now_ms);
		void HandleChatUp(Guest &g, ByteReader &r, uint64_t now_ms, Deferred &d);
		/** Adds a message to the log and sends it to every guest. */
		void PostChat(ChatMessage &m, uint64_t now_ms);
		void SendChatTo(Guest &g, uint32_t seq, uint64_t now_ms, bool history);
		void ResendChats(uint64_t now_ms);
		/** Keeps the pictures of link previews to a few. */
		void TrackChatImage(const BlobHash &hash);
		void BumpSeq(uint16_t &seq);
		Guest *FindBySlot(uint8_t slot);
		void TickMeet(uint64_t now_ms, Deferred &d);
		/** On the meeting point's thread: guests that want in. */
		void OnMeetNotes(const std::vector<MeetNote> &notes);
		HostReach BuildReach();

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
		/** What one guest gets instead of feedback's rumble and player number (MixMode::EachOwn). */
		struct GuestFeedback
		{
			bool rumble = false;
			uint16_t rumble_seq = 0;
			uint8_t rumble_left = 0;
			uint8_t rumble_right = 0;
			bool player_index = false;
			uint16_t player_index_seq = 0;
			uint8_t index = 0;
		};
		std::map<uint8_t, GuestFeedback> guest_feedback; // by slot
		uint32_t state_seq = 1;
		uint64_t fast_state_until_ms = 0;

		std::vector<uint8_t> video_config;
		uint16_t video_config_id = 0;
		FrameCache cache;
		uint32_t next_frame_seq = 1;
		uint32_t audio_seq = 0;
		uint32_t haptics_seq = 0;
		uint32_t voice_seq = 0;
		uint32_t cam_seq = 0;
		uint64_t last_keyframe_request_ms = 0;
		bool keyframe_pending = false;
		bool keyframe_outstanding = false;

		BlobStore blobs;
		BlobRef own_avatar;

		std::deque<ChatMessage> chat_log;
		uint32_t chat_seq = 0;
		BlobStore chat_blobs;
		std::deque<BlobHash> chat_blob_order;
		std::map<BlobHash, uint32_t> chat_blob_from; // picture -> conn_id of the guest that has it
		uint64_t last_chat_resend_ms = 0;
		InputsSnapshot last_inputs;
		uint64_t last_inputs_ms = 0;
		uint64_t last_blob_poll_ms = 0;

		// Joining without a forwarded port. All of it with the mutex, but meet_on and
		// meet_worker, which are set before the network thread starts.
		bool meet_on = false;
		std::unique_ptr<MeetWorker> meet_worker;
		StunResolver stun_resolver;
		StunProbe stun;
		uint64_t meet_id = 0;
		uint64_t meet_started_ms = 0;
		bool have_note = false;
		MeetNote last_note;
		uint32_t note_stun_generation = 0;
		uint64_t note_checked_ms = 0;
		/** An address of a guest that is being sent to, so that this host's router lets the guest in. */
		struct Punching
		{
			Address to;
			uint64_t peer_id = 0;
			uint64_t until_ms = 0;
			uint64_t next_ms = 0;
		};
		std::vector<Punching> punches;
		std::map<uint64_t, uint32_t> punched; // guest's id -> the counter of its note that was acted on
		HostReach reach;
		uint64_t reach_checked_ms = 0;

		// Guests in a web browser. web is set before the network thread starts and
		// lives until it has ended; the rest is with the mutex.
		std::unique_ptr<WebGateway> web;
		std::atomic<bool> web_on{false};
		/** Connections that are closed a moment after the last packet for them went out. */
		std::vector<std::pair<uint64_t, uint64_t>> web_closing; // peer, when
		/** Browsers whose note has been acted on: a note stays at the meeting point for a while. */
		std::deque<uint64_t> web_seen;
};

} // namespace coplay

#endif // COPLAY_HOST_H
