// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_VIDEO_H
#define COPLAY_VIDEO_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "protocol.h"

namespace coplay {

enum class FrameKind
{
	Config, // parameter sets only (SPS/PPS/VPS), no picture
	Key,    // IDR / IRAP picture, decodable on its own
	Delta   // depends on earlier pictures
};

/** Looks at the NAL unit types of an Annex B buffer as delivered by the console. */
FrameKind ClassifyFrame(VideoCodec codec, const uint8_t *data, size_t size);

/** Host side: keeps recently sent frames so lost fragments can be sent again. */
class FrameCache
{
	public:
		struct Entry
		{
			uint32_t frame_seq = 0;
			uint8_t flags = 0;
			uint8_t frames_lost = 0;
			uint32_t capture_ms = 0;
			std::shared_ptr<const std::vector<uint8_t>> data;
		};

		explicit FrameCache(size_t max_frames = 120, size_t max_bytes = 32 * 1024 * 1024);

		void Put(const Entry &entry);
		const Entry *Find(uint32_t frame_seq) const;
		void Clear();

	private:
		size_t max_frames;
		size_t max_bytes;
		size_t bytes = 0;
		std::deque<Entry> entries;
};

/**
 * Builds the payload of one Video packet.
 * @return false if frag_index is out of range
 */
bool BuildVideoFragment(const FrameCache::Entry &entry, uint32_t frag_index, bool retransmit, std::vector<uint8_t> &payload);

struct AssembledFrame
{
	uint32_t frame_seq = 0;
	bool key = false;
	bool recovered = false;
	uint32_t frames_lost = 0;
	/** Host clock (milliseconds, wraps) when the frame came from the console. */
	uint32_t capture_ms = 0;
	std::vector<uint8_t> data;
};

/**
 * Guest side: puts fragments back together and hands out frames strictly in
 * order. A decoder cannot use a frame whose predecessor is missing, so after
 * an unrecoverable loss nothing is handed out until the next key frame.
 */
class FrameAssembler
{
	public:
		struct Config
		{
			/** Wait this long after the last sign of life of a frame before asking for its missing parts. */
			uint32_t nack_delay_ms = 5;
			/** Ask again for the same parts at most this often. */
			uint32_t nack_interval_ms = 40;
			/**
			 * Give up on the frame that blocks delivery once it is this far behind the
			 * newest frame, and wait for a key frame instead.
			 */
			uint32_t max_wait_ms = 150;
			/** While waiting for a key frame, ask for one at most this often. */
			uint32_t keyframe_interval_ms = 500;
			size_t max_pending_frames = 64;
		};

		struct Stats
		{
			uint64_t fragments = 0;
			uint64_t duplicate_fragments = 0;
			uint64_t frames_delivered = 0;
			uint64_t frames_dropped = 0;
			uint64_t nack_fragments = 0;
			uint64_t keyframe_requests = 0;
		};

		FrameAssembler();
		explicit FrameAssembler(const Config &config);

		/** Drops everything and waits for a key frame. */
		void Reset();

		/** @return false if the fragment is malformed */
		bool OnFragment(const VideoFragmentHeader &header, const uint8_t *data, size_t size, uint64_t now_ms);

		/** Next frame for the decoder, if one is ready. */
		bool PopReady(AssembledFrame &frame);

		/**
		 * Call regularly. Fills in what to ask the host to send again and
		 * whether to ask it for a new key frame.
		 */
		void Poll(uint64_t now_ms, std::vector<NackEntry> &nacks, bool &need_keyframe);

		/** Lets retry timing follow the measured round trip time. */
		void SetRttMs(uint32_t rtt_ms);

		bool WaitingForKey() const { return waiting_for_key; }
		const Stats &GetStats() const { return stats; }

	private:
		struct Pending
		{
			VideoFragmentHeader header;
			std::vector<uint8_t> data;
			std::vector<bool> received;
			uint32_t received_count = 0;
			uint64_t first_ms = 0;
			uint64_t last_rx_ms = 0;
			uint64_t last_nack_ms = 0;

			bool Complete() const { return received_count == header.frag_count; }
		};

		void Deliver();
		void StartWaitingForKey();
		void CollectNack(uint32_t seq, Pending &p, uint64_t now_ms, std::vector<NackEntry> &nacks);
		uint32_t NackIntervalMs() const;
		uint32_t MaxWaitMs() const;

		Config config;
		Stats stats;
		std::map<uint32_t, Pending> pending;
		std::deque<AssembledFrame> ready;
		bool waiting_for_key = true;
		uint32_t next_seq = 0;
		uint32_t lost_since_delivery = 0;
		bool recovered_pending = false;
		bool delivered_any = false;
		uint32_t last_delivered_capture_ms = 0;
		uint64_t last_keyframe_request_ms = 0;
		bool keyframe_request_due = true;
		uint32_t rtt_ms = 0;
		/** When a whole-frame Nack was last sent for a frame of which nothing has arrived. */
		std::map<uint32_t, uint64_t> missing_nack_ms;
};

} // namespace coplay

#endif // COPLAY_VIDEO_H
