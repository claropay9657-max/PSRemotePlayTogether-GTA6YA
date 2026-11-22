// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_PCSCALE_H
#define PSRPT_PCSCALE_H

// A screen's RGB pictures into the encoder's YUV, scaled on the way (bilinear), for the game
// source on Linux (pcsource_linux.cpp). Its own, so that the app needs no libswscale.

#include <cstdint>
#include <vector>

class PcScaler
{
	public:
		/** Where red, green and blue sit in a 4-byte pixel (BGRx: 2, 1, 0). */
		struct Layout
		{
			int r = 2;
			int g = 1;
			int b = 0;
		};

		/**
		 * Full-range RGB into BT.709 video-range YUV 4:2:0, scaled from src_w x src_h to
		 * dst_w x dst_h (both even). `v` null: NV12 (U and V interleaved in `u`), else I420.
		 */
		void Convert(const uint8_t *src, int src_stride, int src_w, int src_h, Layout layout,
			uint8_t *y, int y_stride, uint8_t *u, int u_stride, uint8_t *v, int v_stride, int dst_w, int dst_h);

	private:
		/** Source row `row` resampled to the target width, as R, G, B. */
		const uint8_t *Row(const uint8_t *src, int src_stride, int row, Layout layout, int slot);

		int cached_src_w = 0, cached_dst_w = 0;
		std::vector<int> x0;
		std::vector<uint8_t> fx; // weight of x0 + 1, 0..255
		std::vector<uint8_t> rows[2];
		int row_of[2] = { -1, -1 };
		std::vector<uint8_t> line[2];
};

#endif // PSRPT_PCSCALE_H
