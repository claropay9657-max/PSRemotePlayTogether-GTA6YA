// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_MEDIA_H
#define COPLAY_MEDIA_H

/*
 * The players' own voices and webcams, as opposed to what the console sends.
 *
 * Voice is one Opus frame per packet. A webcam sends complete pictures (JPEG)
 * that do not depend on each other, cut into fragments. Both are passed on by
 * the host as they are and never sent again: a late voice frame or picture is
 * worth less than the next one.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocol.h"

namespace coplay {

/**
 * Builds the payload of one CamUp packet; a Cam packet is the same with the
 * sender's slot in front.
 * @return false if frag_index is out of range or the picture is too large
 */
bool BuildCamFragment(uint32_t frame_seq, const uint8_t *data, size_t size, uint32_t frag_index, std::vector<uint8_t> &payload);

/**
 * Puts one sender's webcam pictures back together. Only the newest picture is
 * collected: as soon as a fragment of a newer one arrives, an unfinished older
 * one is dropped.
 */
class CamAssembler
{
	public:
		/**
		 * @param data the fragment's bytes, as long as ReadCamFragmentHeader() checked
		 * @return true if this fragment completed a picture, which is then in Frame()
		 */
		bool OnFragment(const CamFragmentHeader &header, const uint8_t *data, size_t size, uint64_t now_ms);

		/** The last complete picture. */
		const std::vector<uint8_t> &Frame() const { return frame; }
		void Reset();

	private:
		bool collecting = false;
		uint32_t seq = 0;
		std::vector<uint8_t> buf;
		std::vector<bool> got;
		uint32_t got_count = 0;
		uint64_t last_ms = 0;
		bool delivered_any = false;
		uint32_t delivered_seq = 0;
		std::vector<uint8_t> frame;
};

/**
 * Tells new voice frames of one speaker from late and duplicate ones. A
 * speaker that was silent for a while may start over with any number (it left
 * and somebody else got its place, or its app restarted).
 */
class VoiceSequence
{
	public:
		bool Accept(uint32_t seq, uint64_t now_ms);
		void Reset() { have = false; }

	private:
		bool have = false;
		uint32_t last_seq = 0;
		uint64_t last_ms = 0;
};

} // namespace coplay

#endif // COPLAY_MEDIA_H
