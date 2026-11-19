// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_BYTES_H
#define COPLAY_BYTES_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace coplay {

/** Appends big-endian values to a byte vector. */
class ByteWriter
{
	public:
		explicit ByteWriter(std::vector<uint8_t> &out) : out(out) {}

		void U8(uint8_t v) { out.push_back(v); }
		void I8(int8_t v) { U8(static_cast<uint8_t>(v)); }
		void U16(uint16_t v)
		{
			U8(static_cast<uint8_t>(v >> 8));
			U8(static_cast<uint8_t>(v));
		}
		void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }
		void U32(uint32_t v)
		{
			U16(static_cast<uint16_t>(v >> 16));
			U16(static_cast<uint16_t>(v));
		}
		void U64(uint64_t v)
		{
			U32(static_cast<uint32_t>(v >> 32));
			U32(static_cast<uint32_t>(v));
		}
		void F32(float v)
		{
			uint32_t u;
			std::memcpy(&u, &v, sizeof(u));
			U32(u);
		}
		void Bytes(const uint8_t *p, size_t n)
		{
			if(n)
				out.insert(out.end(), p, p + n);
		}
		/** Length-prefixed (one byte) string, truncated to 255 bytes. */
		void Str8(const std::string &s)
		{
			size_t n = s.size() > 255 ? 255 : s.size();
			U8(static_cast<uint8_t>(n));
			Bytes(reinterpret_cast<const uint8_t *>(s.data()), n);
		}

	private:
		std::vector<uint8_t> &out;
};

/**
 * Reads big-endian values. Reading past the end yields zeros and clears ok(),
 * so a parser can read everything and check ok() once at the end.
 */
class ByteReader
{
	public:
		ByteReader(const uint8_t *p, size_t n) : p(p), n(n) {}

		bool ok() const { return good; }
		size_t remaining() const { return good ? n - pos : 0; }
		const uint8_t *cursor() const { return p + pos; }

		uint8_t U8()
		{
			if(!Need(1))
				return 0;
			return p[pos++];
		}
		int8_t I8() { return static_cast<int8_t>(U8()); }
		uint16_t U16()
		{
			if(!Need(2))
				return 0;
			uint16_t v = static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
			pos += 2;
			return v;
		}
		int16_t I16() { return static_cast<int16_t>(U16()); }
		uint32_t U32()
		{
			if(!Need(4))
				return 0;
			uint32_t v = (static_cast<uint32_t>(p[pos]) << 24) | (static_cast<uint32_t>(p[pos + 1]) << 16)
				| (static_cast<uint32_t>(p[pos + 2]) << 8) | static_cast<uint32_t>(p[pos + 3]);
			pos += 4;
			return v;
		}
		uint64_t U64()
		{
			uint64_t hi = U32();
			uint64_t lo = U32();
			return (hi << 32) | lo;
		}
		float F32()
		{
			uint32_t u = U32();
			float v;
			std::memcpy(&v, &u, sizeof(v));
			return v;
		}
		bool Bytes(uint8_t *dst, size_t count)
		{
			if(!Need(count))
				return false;
			if(count)
				std::memcpy(dst, p + pos, count);
			pos += count;
			return true;
		}
		bool Skip(size_t count)
		{
			if(!Need(count))
				return false;
			pos += count;
			return true;
		}
		std::string Str8()
		{
			size_t len = U8();
			if(!Need(len))
				return std::string();
			std::string s(reinterpret_cast<const char *>(p + pos), len);
			pos += len;
			return s;
		}

	private:
		bool Need(size_t count)
		{
			if(!good || count > n - pos)
			{
				good = false;
				return false;
			}
			return true;
		}

		const uint8_t *p;
		size_t n;
		size_t pos = 0;
		bool good = true;
};

} // namespace coplay

#endif // COPLAY_BYTES_H
