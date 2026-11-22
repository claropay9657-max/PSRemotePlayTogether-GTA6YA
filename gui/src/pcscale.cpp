// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pcscale.h"

#include <algorithm>

// BT.709 in video range, from full-range RGB, in steps of 1/256:
//   Y = 16 + 0.1826 R + 0.6142 G + 0.0620 B
//   U = 128 - 0.1006 R - 0.3386 G + 0.4392 B
//   V = 128 + 0.4392 R - 0.3989 G - 0.0403 B
static inline uint8_t Luma(int r, int g, int b)
{
	return static_cast<uint8_t>(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
}

static inline uint8_t ChromaU(int r, int g, int b)
{
	return static_cast<uint8_t>((-26 * r - 87 * g + 112 * b + (128 << 8) + 128) >> 8);
}

static inline uint8_t ChromaV(int r, int g, int b)
{
	return static_cast<uint8_t>((112 * r - 102 * g - 10 * b + (128 << 8) + 128) >> 8);
}

/** Where target position `i` of `dst` falls among `src`: the sample before it and the weight of the next. */
static void Map(int i, int src, int dst, int *first, int *weight)
{
	const double s = (i + 0.5) * src / dst - 0.5;
	int at = s <= 0.0 ? 0 : static_cast<int>(s);
	int w = static_cast<int>((s - at) * 256.0 + 0.5);
	if(s <= 0.0)
		w = 0;
	if(at >= src - 1)
	{
		at = src - 1;
		w = 0;
	}
	*first = at;
	*weight = std::clamp(w, 0, 255);
}

const uint8_t *PcScaler::Row(const uint8_t *src, int src_stride, int row, Layout layout, int slot)
{
	if(row_of[slot] == row)
		return rows[slot].data();
	const uint8_t *in = src + static_cast<size_t>(row) * static_cast<size_t>(src_stride);
	uint8_t *out = rows[slot].data();
	const int dst_w = cached_dst_w;
	for(int x = 0; x < dst_w; x++)
	{
		const uint8_t *p0 = in + x0[x] * 4;
		const int f = fx[x];
		if(!f)
		{
			out[x * 3] = p0[layout.r];
			out[x * 3 + 1] = p0[layout.g];
			out[x * 3 + 2] = p0[layout.b];
			continue;
		}
		const uint8_t *p1 = p0 + 4;
		const int g = 256 - f;
		out[x * 3] = static_cast<uint8_t>((p0[layout.r] * g + p1[layout.r] * f + 128) >> 8);
		out[x * 3 + 1] = static_cast<uint8_t>((p0[layout.g] * g + p1[layout.g] * f + 128) >> 8);
		out[x * 3 + 2] = static_cast<uint8_t>((p0[layout.b] * g + p1[layout.b] * f + 128) >> 8);
	}
	row_of[slot] = row;
	return out;
}

void PcScaler::Convert(const uint8_t *src, int src_stride, int src_w, int src_h, Layout layout,
	uint8_t *y, int y_stride, uint8_t *u, int u_stride, uint8_t *v, int v_stride, int dst_w, int dst_h)
{
	if(src_w < 1 || src_h < 1 || dst_w < 2 || dst_h < 2)
		return;
	dst_w &= ~1;
	dst_h &= ~1;
	if(cached_src_w != src_w || cached_dst_w != dst_w)
	{
		x0.resize(static_cast<size_t>(dst_w));
		fx.resize(static_cast<size_t>(dst_w));
		for(int x = 0; x < dst_w; x++)
		{
			int first, weight;
			Map(x, src_w, dst_w, &first, &weight);
			x0[x] = first;
			fx[x] = static_cast<uint8_t>(weight);
		}
		for(auto &r : rows)
			r.resize(static_cast<size_t>(dst_w) * 3);
		for(auto &l : line)
			l.resize(static_cast<size_t>(dst_w) * 3);
		cached_src_w = src_w;
		cached_dst_w = dst_w;
	}
	// A new picture: nothing of the last one is kept
	row_of[0] = row_of[1] = -1;

	for(int dy = 0; dy < dst_h; dy += 2)
	{
		// Two target rows, each blended from two resampled source rows
		for(int k = 0; k < 2; k++)
		{
			int first, f;
			Map(dy + k, src_h, dst_h, &first, &f);
			// Keep what the last row left in the cache: the next one often needs it again
			const int slot_a = row_of[0] == first ? 0 : row_of[1] == first ? 1 : row_of[0] == first + 1 ? 1 : 0;
			const uint8_t *a = Row(src, src_stride, first, layout, slot_a);
			uint8_t *out = line[k].data();
			if(!f)
			{
				std::copy(a, a + dst_w * 3, out);
				continue;
			}
			const uint8_t *b = Row(src, src_stride, first + 1, layout, 1 - slot_a);
			const int g = 256 - f;
			for(int i = 0; i < dst_w * 3; i++)
				out[i] = static_cast<uint8_t>((a[i] * g + b[i] * f + 128) >> 8);
		}
		for(int k = 0; k < 2; k++)
		{
			const uint8_t *rgb = line[k].data();
			uint8_t *luma = y + static_cast<size_t>(dy + k) * static_cast<size_t>(y_stride);
			for(int x = 0; x < dst_w; x++)
				luma[x] = Luma(rgb[x * 3], rgb[x * 3 + 1], rgb[x * 3 + 2]);
		}
		// Colour once for every 2 x 2 pixels, from their average
		const uint8_t *top = line[0].data();
		const uint8_t *bottom = line[1].data();
		uint8_t *cu = u + static_cast<size_t>(dy / 2) * static_cast<size_t>(u_stride);
		uint8_t *cv = v ? v + static_cast<size_t>(dy / 2) * static_cast<size_t>(v_stride) : nullptr;
		for(int cx = 0; cx < dst_w / 2; cx++)
		{
			const int i = cx * 6;
			const int r = (top[i] + top[i + 3] + bottom[i] + bottom[i + 3] + 2) >> 2;
			const int gr = (top[i + 1] + top[i + 4] + bottom[i + 1] + bottom[i + 4] + 2) >> 2;
			const int b = (top[i + 2] + top[i + 5] + bottom[i + 2] + bottom[i + 5] + 2) >> 2;
			if(cv)
			{
				cu[cx] = ChromaU(r, gr, b);
				cv[cx] = ChromaV(r, gr, b);
			}
			else
			{
				cu[cx * 2] = ChromaU(r, gr, b);
				cu[cx * 2 + 1] = ChromaV(r, gr, b);
			}
		}
	}
}
