// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/video.h>

#include <algorithm>
#include <cstring>

namespace coplay {

FrameKind ClassifyFrame(VideoCodec codec, const uint8_t *data, size_t size)
{
	bool has_parameter_set = false;
	size_t i = 0;
	while(i + 3 < size)
	{
		// Annex B start code: 00 00 01 (a 4 byte 00 00 00 01 ends in the same three bytes)
		if(!(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1))
		{
			i++;
			continue;
		}
		uint8_t nal = data[i + 3];
		i += 4;
		if(codec == VideoCodec::H264)
		{
			unsigned type = nal & 0x1f;
			if(type == 5)
				return FrameKind::Key;
			if(type >= 1 && type <= 4)
				return FrameKind::Delta;
			if(type == 7 || type == 8)
				has_parameter_set = true;
		}
		else
		{
			unsigned type = (nal >> 1) & 0x3f;
			if(type >= 16 && type <= 23)
				return FrameKind::Key;
			if(type <= 15)
				return FrameKind::Delta;
			if(type >= 32 && type <= 34)
				has_parameter_set = true;
		}
	}
	return has_parameter_set ? FrameKind::Config : FrameKind::Delta;
}

FrameCache::FrameCache(size_t max_frames, size_t max_bytes) : max_frames(max_frames), max_bytes(max_bytes) {}

void FrameCache::Put(const Entry &entry)
{
	// Find() relies on consecutive sequence numbers
	if(!entries.empty() && entry.frame_seq != entries.back().frame_seq + 1)
		Clear();
	entries.push_back(entry);
	bytes += entry.data ? entry.data->size() : 0;
	while(entries.size() > 1 && (entries.size() > max_frames || bytes > max_bytes))
	{
		bytes -= entries.front().data ? entries.front().data->size() : 0;
		entries.pop_front();
	}
}

const FrameCache::Entry *FrameCache::Find(uint32_t frame_seq) const
{
	if(entries.empty())
		return nullptr;
	uint32_t first = entries.front().frame_seq;
	if(frame_seq < first || frame_seq - first >= entries.size())
		return nullptr;
	return &entries[frame_seq - first];
}

void FrameCache::Clear()
{
	entries.clear();
	bytes = 0;
}

bool BuildVideoFragment(const FrameCache::Entry &entry, uint32_t frag_index, bool retransmit, std::vector<uint8_t> &payload)
{
	if(!entry.data)
		return false;
	size_t total = entry.data->size();
	uint32_t count = VideoFragmentCount(total);
	if(frag_index >= count || count > 0xffff)
		return false;
	size_t offset = static_cast<size_t>(frag_index) * kVideoFragmentSize;
	size_t len = std::min(kVideoFragmentSize, total - offset);

	VideoFragmentHeader h;
	h.frame_seq = entry.frame_seq;
	h.frag_index = static_cast<uint16_t>(frag_index);
	h.frag_count = static_cast<uint16_t>(count);
	h.flags = static_cast<uint8_t>(entry.flags | (retransmit ? kVideoRetransmit : 0));
	h.frames_lost = entry.frames_lost;
	h.total_size = static_cast<uint32_t>(total);
	h.capture_ms = entry.capture_ms;

	payload.clear();
	payload.reserve(kVideoFragmentHeaderSize + len);
	ByteWriter w(payload);
	WriteVideoFragmentHeader(w, h);
	w.Bytes(entry.data->data() + offset, len);
	return true;
}

FrameAssembler::FrameAssembler() : FrameAssembler(Config()) {}

FrameAssembler::FrameAssembler(const Config &config) : config(config) {}

void FrameAssembler::Reset()
{
	pending.clear();
	ready.clear();
	missing_nack_ms.clear();
	waiting_for_key = true;
	keyframe_request_due = true;
	lost_since_delivery = 0;
	recovered_pending = false;
}

void FrameAssembler::SetRttMs(uint32_t rtt)
{
	rtt_ms = rtt;
}

uint32_t FrameAssembler::NackIntervalMs() const
{
	// Asking again before the answer to the last request can have arrived only wastes bandwidth
	return std::max(config.nack_interval_ms, rtt_ms + rtt_ms / 2 + 10);
}

uint32_t FrameAssembler::MaxWaitMs() const
{
	// Leave room for at least two rounds of retransmission
	return std::max(config.max_wait_ms, rtt_ms * 3 + 60);
}

bool FrameAssembler::OnFragment(const VideoFragmentHeader &h, const uint8_t *data, size_t size, uint64_t now_ms)
{
	if(h.total_size == 0 || h.total_size > kMaxVideoFrameSize)
		return false;
	if(h.frag_count != VideoFragmentCount(h.total_size) || h.frag_index >= h.frag_count)
		return false;
	size_t expected = (static_cast<uint32_t>(h.frag_index) + 1 < h.frag_count)
		? kVideoFragmentSize
		: h.total_size - static_cast<size_t>(h.frag_count - 1) * kVideoFragmentSize;
	if(size != expected)
		return false;

	stats.fragments++;

	if(!waiting_for_key && h.frame_seq < next_seq)
	{
		stats.duplicate_fragments++; // already delivered or given up on
		return true;
	}

	auto it = pending.find(h.frame_seq);
	if(it == pending.end())
	{
		if(pending.size() >= config.max_pending_frames)
		{
			if(h.frame_seq < pending.begin()->first)
				return true; // older than everything we keep
			// Hopelessly behind: frames pile up faster than they complete
			if(!waiting_for_key)
				StartWaitingForKey();
			if(pending.size() >= config.max_pending_frames)
			{
				stats.frames_dropped++;
				pending.erase(pending.begin());
			}
		}
		Pending p;
		p.header = h;
		p.header.flags = static_cast<uint8_t>(h.flags & (kVideoKey | kVideoRecovered));
		p.data.resize(h.total_size);
		p.received.assign(h.frag_count, false);
		p.first_ms = now_ms;
		it = pending.emplace(h.frame_seq, std::move(p)).first;
		missing_nack_ms.erase(h.frame_seq);
	}
	else if(it->second.header.total_size != h.total_size || it->second.header.frag_count != h.frag_count)
		return false;

	Pending &p = it->second;
	if(p.received[h.frag_index])
	{
		stats.duplicate_fragments++;
		return true;
	}
	std::memcpy(p.data.data() + static_cast<size_t>(h.frag_index) * kVideoFragmentSize, data, size);
	p.received[h.frag_index] = true;
	p.received_count++;
	p.last_rx_ms = now_ms;

	if(p.Complete())
		Deliver();
	return true;
}

void FrameAssembler::StartWaitingForKey()
{
	waiting_for_key = true;
	keyframe_request_due = true;
	recovered_pending = true;
	missing_nack_ms.clear();

	// Only a key frame and what follows it is still useful
	auto first_key = pending.begin();
	while(first_key != pending.end() && !(first_key->second.header.flags & kVideoKey))
		++first_key;
	size_t dropped = static_cast<size_t>(std::distance(pending.begin(), first_key));
	stats.frames_dropped += dropped;
	lost_since_delivery += static_cast<uint32_t>(dropped);
	pending.erase(pending.begin(), first_key);
}

void FrameAssembler::Deliver()
{
	if(waiting_for_key)
	{
		auto key = pending.end();
		for(auto it = pending.begin(); it != pending.end(); ++it)
		{
			if(it->second.Complete() && (it->second.header.flags & kVideoKey))
				key = it; // keep the newest
		}
		if(key == pending.end())
			return;
		size_t dropped = static_cast<size_t>(std::distance(pending.begin(), key));
		stats.frames_dropped += dropped;
		lost_since_delivery += static_cast<uint32_t>(dropped);
		pending.erase(pending.begin(), key);
		next_seq = key->first;
		waiting_for_key = false;
		keyframe_request_due = false;
		missing_nack_ms.clear();
	}

	for(;;)
	{
		auto head = pending.find(next_seq);
		if(head != pending.end() && head->second.Complete())
		{
			Pending &p = head->second;
			AssembledFrame f;
			f.frame_seq = next_seq;
			f.key = (p.header.flags & kVideoKey) != 0;
			f.recovered = (p.header.flags & kVideoRecovered) != 0 || (recovered_pending && delivered_any);
			f.frames_lost = static_cast<uint32_t>(p.header.frames_lost) + lost_since_delivery;
			f.capture_ms = p.header.capture_ms;
			last_delivered_capture_ms = p.header.capture_ms;
			f.data = std::move(p.data);
			ready.push_back(std::move(f));
			pending.erase(head);
			missing_nack_ms.erase(next_seq);
			next_seq++;
			lost_since_delivery = 0;
			recovered_pending = false;
			delivered_any = true;
			stats.frames_delivered++;
			continue;
		}

		// The head is not ready. A complete key frame further ahead makes everything before it unnecessary.
		auto key = pending.end();
		for(auto it = pending.upper_bound(next_seq); it != pending.end(); ++it)
		{
			if(it->second.Complete() && (it->second.header.flags & kVideoKey))
				key = it;
		}
		if(key == pending.end())
			break;
		uint32_t skipped = key->first - next_seq;
		stats.frames_dropped += skipped;
		lost_since_delivery += skipped;
		recovered_pending = true;
		pending.erase(pending.begin(), key);
		next_seq = key->first;
		missing_nack_ms.clear();
	}
}

bool FrameAssembler::PopReady(AssembledFrame &frame)
{
	if(ready.empty())
		return false;
	frame = std::move(ready.front());
	ready.pop_front();
	return true;
}

void FrameAssembler::CollectNack(uint32_t seq, Pending &p, uint64_t now_ms, std::vector<NackEntry> &nacks)
{
	// Fragments are still coming in: give them a moment before calling the gaps lost
	if(now_ms - p.last_rx_ms < config.nack_delay_ms)
		return;
	if(p.last_nack_ms && now_ms - p.last_nack_ms < NackIntervalMs())
		return;
	NackEntry e;
	e.frame_seq = seq;
	size_t missing = p.header.frag_count - p.received_count;
	const size_t max_listed = 512;
	if(missing <= max_listed)
	{
		e.fragments.reserve(missing);
		for(uint32_t i = 0; i < p.header.frag_count; i++)
		{
			if(!p.received[i])
				e.fragments.push_back(static_cast<uint16_t>(i));
		}
	}
	// else: empty list = send the whole frame again
	stats.nack_fragments += missing;
	p.last_nack_ms = now_ms;
	nacks.push_back(std::move(e));
}

void FrameAssembler::Poll(uint64_t now_ms, std::vector<NackEntry> &nacks, bool &need_keyframe)
{
	nacks.clear();
	need_keyframe = false;

	if(!waiting_for_key && !pending.empty())
	{
		// The head frame blocks delivery. Is it still worth waiting for?
		auto head = pending.find(next_seq);
		uint64_t blocked_since = head != pending.end() ? head->second.first_ms : pending.begin()->second.first_ms;

		// How far the head is behind the newest frame, on the host's clock. If nothing of the
		// head arrived its time is unknown, but it is later than the last frame shown. After a
		// stall of the connection this is large at once, and nothing is requested again: by the
		// time all of it had been resent it would be stale.
		uint32_t newest_capture = pending.rbegin()->second.header.capture_ms;
		uint32_t head_capture = head != pending.end() ? head->second.header.capture_ms
			: (delivered_any ? last_delivered_capture_ms : pending.begin()->second.header.capture_ms);
		int32_t behind_ms = static_cast<int32_t>(newest_capture - head_capture);

		const uint32_t max_gap = 1024; // sanity limit, far beyond anything recoverable
		if(now_ms - blocked_since >= MaxWaitMs() || behind_ms > static_cast<int32_t>(MaxWaitMs())
			|| pending.rbegin()->first - next_seq > max_gap)
			StartWaitingForKey();
	}

	if(waiting_for_key)
	{
		if(keyframe_request_due || now_ms - last_keyframe_request_ms >= config.keyframe_interval_ms)
		{
			need_keyframe = true;
			keyframe_request_due = false;
			last_keyframe_request_ms = now_ms;
			stats.keyframe_requests++;
		}
		// A key frame that is partly here is the fastest way back to a picture
		for(auto &entry : pending)
		{
			if((entry.second.header.flags & kVideoKey) && !entry.second.Complete())
				CollectNack(entry.first, entry.second, now_ms, nacks);
		}
		return;
	}

	if(pending.empty())
		return;

	uint32_t highest = pending.rbegin()->first;
	for(uint32_t seq = next_seq; seq <= highest; seq++)
	{
		auto it = pending.find(seq);
		if(it != pending.end())
		{
			if(!it->second.Complete())
				CollectNack(seq, it->second, now_ms, nacks);
			continue;
		}
		// Nothing of this frame arrived, but a later frame did
		auto later = pending.upper_bound(seq);
		if(later == pending.end() || now_ms - later->second.first_ms < config.nack_delay_ms)
			continue;
		uint64_t &last = missing_nack_ms[seq];
		if(last && now_ms - last < NackIntervalMs())
			continue;
		last = now_ms;
		NackEntry e;
		e.frame_seq = seq;
		nacks.push_back(std::move(e));
		stats.nack_fragments++;
	}
}

} // namespace coplay
