// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/media.h>

#include <cstring>

namespace coplay {

/** After this long without anything from a sender its numbering may start over. */
static const uint64_t kSequenceRestartMs = 1000;

bool BuildCamFragment(uint32_t frame_seq, const uint8_t *data, size_t size, uint32_t frag_index, std::vector<uint8_t> &payload)
{
	payload.clear();
	if(!data || size == 0 || size > kMaxCamFrameSize)
		return false;
	uint32_t count = CamFragmentCount(size);
	if(frag_index >= count)
		return false;
	size_t offset = static_cast<size_t>(frag_index) * kCamFragmentSize;
	size_t len = size - offset < kCamFragmentSize ? size - offset : kCamFragmentSize;
	payload.reserve(kCamFragmentHeaderSize + len);
	ByteWriter w(payload);
	CamFragmentHeader h;
	h.frame_seq = frame_seq;
	h.frag_index = static_cast<uint16_t>(frag_index);
	h.frag_count = static_cast<uint16_t>(count);
	h.total_size = static_cast<uint32_t>(size);
	WriteCamFragmentHeader(w, h);
	w.Bytes(data + offset, len);
	return true;
}

void CamAssembler::Reset()
{
	collecting = false;
	delivered_any = false;
	got_count = 0;
	buf.clear();
	got.clear();
}

bool CamAssembler::OnFragment(const CamFragmentHeader &h, const uint8_t *data, size_t size, uint64_t now_ms)
{
	if(!data || h.total_size == 0 || h.total_size > kMaxCamFrameSize || h.frag_count != CamFragmentCount(h.total_size)
		|| h.frag_index >= h.frag_count)
		return false;
	size_t offset = static_cast<size_t>(h.frag_index) * kCamFragmentSize;
	if(offset + size > h.total_size)
		return false;

	if(now_ms - last_ms > kSequenceRestartMs)
	{
		// Silent for a while: the sender may have started over with any number
		collecting = false;
		delivered_any = false;
	}
	last_ms = now_ms;
	if(delivered_any && static_cast<int32_t>(h.frame_seq - delivered_seq) <= 0)
		return false; // a straggler of a picture that is already through, or older
	if(collecting && h.frame_seq != seq)
	{
		if(static_cast<int32_t>(h.frame_seq - seq) < 0)
			return false; // older than the one being collected
		collecting = false;
	}
	if(!collecting)
	{
		collecting = true;
		seq = h.frame_seq;
		buf.assign(h.total_size, 0);
		got.assign(h.frag_count, false);
		got_count = 0;
	}
	if(buf.size() != h.total_size || got.size() != h.frag_count)
		return false; // disagrees with the fragments seen before
	if(got[h.frag_index])
		return false;
	std::memcpy(buf.data() + offset, data, size);
	got[h.frag_index] = true;
	if(++got_count < h.frag_count)
		return false;

	frame.swap(buf);
	collecting = false;
	delivered_any = true;
	delivered_seq = seq;
	return true;
}

bool VoiceSequence::Accept(uint32_t seq, uint64_t now_ms)
{
	if(have && now_ms - last_ms <= kSequenceRestartMs && static_cast<int32_t>(seq - last_seq) <= 0)
		return false;
	have = true;
	last_seq = seq;
	last_ms = now_ms;
	return true;
}

} // namespace coplay
