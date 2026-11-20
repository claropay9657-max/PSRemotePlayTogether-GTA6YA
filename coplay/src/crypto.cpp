// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/crypto.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>

namespace coplay {

bool RandomBytes(uint8_t *out, size_t size)
{
	return RAND_bytes(out, static_cast<int>(size)) == 1;
}

static void HmacSha256(const uint8_t *key, size_t key_size, const uint8_t *data, size_t data_size, uint8_t out[32])
{
	unsigned int out_len = 32;
	// HMAC() treats a NULL key as invalid, an empty one needs a valid pointer
	static const uint8_t empty = 0;
	HMAC(EVP_sha256(), key_size ? key : &empty, static_cast<int>(key_size), data, data_size, out, &out_len);
}

Key HkdfSha256(const uint8_t *ikm, size_t ikm_size, const uint8_t *salt, size_t salt_size, const std::string &info)
{
	uint8_t zero_salt[32] = { 0 };
	uint8_t prk[32];
	if(salt_size == 0)
	{
		salt = zero_salt;
		salt_size = sizeof(zero_salt);
	}
	HmacSha256(salt, salt_size, ikm, ikm_size, prk);

	// T(1) = HMAC(PRK, info | 0x01)
	std::vector<uint8_t> block(info.begin(), info.end());
	block.push_back(0x01);
	Key out;
	HmacSha256(prk, sizeof(prk), block.data(), block.size(), out.data());
	return out;
}

Key DeriveHelloKey(const Secret &secret)
{
	return HkdfSha256(secret.data(), secret.size(), nullptr, 0, "psrpt coplay hello");
}

Key DeriveWelcomeKey(const Secret &secret)
{
	return HkdfSha256(secret.data(), secret.size(), nullptr, 0, "psrpt coplay welcome");
}

Key DeriveMeetKey(const Secret &secret)
{
	return HkdfSha256(secret.data(), secret.size(), nullptr, 0, "psrpt coplay meet note");
}

std::array<uint8_t, 16> DeriveMeetRoom(const Secret &secret)
{
	Key k = HkdfSha256(secret.data(), secret.size(), nullptr, 0, "psrpt coplay meet room");
	std::array<uint8_t, 16> room{};
	std::memcpy(room.data(), k.data(), room.size());
	return room;
}

SessionKeys DeriveSessionKeys(const Secret &secret, const HandshakeNonce &guest_nonce, const HandshakeNonce &host_nonce)
{
	uint8_t salt[kHandshakeNonceSize * 2];
	std::memcpy(salt, guest_nonce.data(), kHandshakeNonceSize);
	std::memcpy(salt + kHandshakeNonceSize, host_nonce.data(), kHandshakeNonceSize);
	SessionKeys keys;
	keys.guest_to_host = HkdfSha256(secret.data(), secret.size(), salt, sizeof(salt), "psrpt coplay v1 guest to host");
	keys.host_to_guest = HkdfSha256(secret.data(), secret.size(), salt, sizeof(salt), "psrpt coplay v1 host to guest");
	return keys;
}

bool ParseHeader(const uint8_t *packet, size_t size, PacketHeader &header)
{
	if(size < kHeaderSize + kTagSize)
		return false;
	ByteReader r(packet, kHeaderSize);
	header.type = static_cast<PacketType>(r.U8());
	header.version = r.U8();
	header.conn_id = r.U32();
	header.counter = r.U64();
	return r.ok();
}

static bool SealWithHeader(const Key &key, const uint8_t header[kHeaderSize], const uint8_t *payload,
		size_t payload_size, std::vector<uint8_t> &out)
{
	if(payload_size > kMaxPayload)
		return false;
	out.resize(kHeaderSize + payload_size + kTagSize);
	std::memcpy(out.data(), header, kHeaderSize);

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	do
	{
		if(EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(kNonceSize), nullptr) != 1)
			break;
		if(EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), header + 2) != 1)
			break;
		if(EVP_EncryptUpdate(ctx, nullptr, &len, header, static_cast<int>(kHeaderSize)) != 1)
			break;
		if(payload_size
			&& EVP_EncryptUpdate(ctx, out.data() + kHeaderSize, &len, payload, static_cast<int>(payload_size)) != 1)
			break;
		if(EVP_EncryptFinal_ex(ctx, out.data() + kHeaderSize + payload_size, &len) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, static_cast<int>(kTagSize),
				out.data() + kHeaderSize + payload_size) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	return ok;
}

bool SealPacket(const Key &key, PacketType type, uint32_t conn_id, uint64_t counter,
		const uint8_t *payload, size_t payload_size, std::vector<uint8_t> &out)
{
	std::vector<uint8_t> header;
	header.reserve(kHeaderSize);
	ByteWriter w(header);
	w.U8(static_cast<uint8_t>(type));
	w.U8(kProtocolVersion);
	w.U32(conn_id);
	w.U64(counter);
	return SealWithHeader(key, header.data(), payload, payload_size, out);
}

bool SealHandshakePacket(const Key &key, PacketType type, const uint8_t *payload, size_t payload_size,
		std::vector<uint8_t> &out)
{
	uint8_t header[kHeaderSize];
	header[0] = static_cast<uint8_t>(type);
	header[1] = kProtocolVersion;
	if(!RandomBytes(header + 2, kNonceSize))
		return false;
	return SealWithHeader(key, header, payload, payload_size, out);
}

bool OpenPacket(const Key &key, const uint8_t *packet, size_t size, std::vector<uint8_t> &payload)
{
	if(size < kHeaderSize + kTagSize || size > kMaxDatagram)
		return false;
	size_t payload_size = size - kHeaderSize - kTagSize;
	payload.resize(payload_size);

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	uint8_t tag[kTagSize];
	std::memcpy(tag, packet + kHeaderSize + payload_size, kTagSize);
	uint8_t final_buf[16];
	do
	{
		if(EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(kNonceSize), nullptr) != 1)
			break;
		if(EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), packet + 2) != 1)
			break;
		if(EVP_DecryptUpdate(ctx, nullptr, &len, packet, static_cast<int>(kHeaderSize)) != 1)
			break;
		if(payload_size
			&& EVP_DecryptUpdate(ctx, payload.data(), &len, packet + kHeaderSize, static_cast<int>(payload_size)) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(kTagSize), tag) != 1)
			break;
		if(EVP_DecryptFinal_ex(ctx, final_buf, &len) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		payload.clear();
	return ok;
}

bool ReplayWindow::Accept(uint64_t counter)
{
	if(counter == 0)
		return false;
	const size_t words = bits.size();
	auto bit_index = [](uint64_t c) { return c % kWindow; };

	if(counter > highest)
	{
		uint64_t advance = counter - highest;
		if(advance >= kWindow)
			bits.fill(0);
		else
		{
			// clear the slots of the counters that are skipped over, they now belong to newer values
			for(uint64_t c = highest + 1; c < counter; c++)
			{
				uint64_t i = bit_index(c);
				bits[(i / 64) % words] &= ~(uint64_t(1) << (i % 64));
			}
		}
		highest = counter;
		uint64_t i = bit_index(counter);
		bits[(i / 64) % words] |= uint64_t(1) << (i % 64);
		return true;
	}

	if(highest - counter >= kWindow)
		return false; // too old to tell whether it was seen
	uint64_t i = bit_index(counter);
	uint64_t mask = uint64_t(1) << (i % 64);
	uint64_t &word = bits[(i / 64) % words];
	if(word & mask)
		return false;
	word |= mask;
	return true;
}

static const char kBase64UrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string Base64UrlEncode(const uint8_t *data, size_t size)
{
	std::string out;
	out.reserve((size * 4 + 2) / 3);
	size_t i = 0;
	while(i + 3 <= size)
	{
		uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
		out.push_back(kBase64UrlAlphabet[(v >> 18) & 63]);
		out.push_back(kBase64UrlAlphabet[(v >> 12) & 63]);
		out.push_back(kBase64UrlAlphabet[(v >> 6) & 63]);
		out.push_back(kBase64UrlAlphabet[v & 63]);
		i += 3;
	}
	if(size - i == 1)
	{
		uint32_t v = uint32_t(data[i]) << 16;
		out.push_back(kBase64UrlAlphabet[(v >> 18) & 63]);
		out.push_back(kBase64UrlAlphabet[(v >> 12) & 63]);
	}
	else if(size - i == 2)
	{
		uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
		out.push_back(kBase64UrlAlphabet[(v >> 18) & 63]);
		out.push_back(kBase64UrlAlphabet[(v >> 12) & 63]);
		out.push_back(kBase64UrlAlphabet[(v >> 6) & 63]);
	}
	return out;
}

bool Base64UrlDecode(const std::string &text, std::vector<uint8_t> &out)
{
	out.clear();
	uint32_t acc = 0;
	int bits_in_acc = 0;
	for(char c : text)
	{
		int v;
		if(c >= 'A' && c <= 'Z')
			v = c - 'A';
		else if(c >= 'a' && c <= 'z')
			v = c - 'a' + 26;
		else if(c >= '0' && c <= '9')
			v = c - '0' + 52;
		else if(c == '-' || c == '+')
			v = 62;
		else if(c == '_' || c == '/')
			v = 63;
		else if(c == '=')
			break;
		else
			return false;
		acc = (acc << 6) | static_cast<uint32_t>(v);
		bits_in_acc += 6;
		if(bits_in_acc >= 8)
		{
			bits_in_acc -= 8;
			out.push_back(static_cast<uint8_t>((acc >> bits_in_acc) & 0xff));
		}
	}
	// A valid encoding never leaves 6 dangling bits (that would be a lone character)
	return bits_in_acc != 6;
}

} // namespace coplay
