// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_CRYPTO_H
#define COPLAY_CRYPTO_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "protocol.h"

namespace coplay {

constexpr size_t kSecretSize = 16;
constexpr size_t kKeySize = 32;

/** The random value inside an invite. Whoever knows it can join the session. */
using Secret = std::array<uint8_t, kSecretSize>;
using Key = std::array<uint8_t, kKeySize>;
using HandshakeNonce = std::array<uint8_t, kHandshakeNonceSize>;

bool RandomBytes(uint8_t *out, size_t size);

/** HKDF-SHA256 (RFC 5869) limited to a single 32 byte output block. */
Key HkdfSha256(const uint8_t *ikm, size_t ikm_size, const uint8_t *salt, size_t salt_size, const std::string &info);

/**
 * Key that seals Hello packets (guest -> host). The handshake keys do not
 * depend on the protocol version, so that a host can still tell a guest with
 * a different version why it is turned away.
 */
Key DeriveHelloKey(const Secret &secret);
/** Key that seals Welcome, Reject and Punch packets (host -> guest). */
Key DeriveWelcomeKey(const Secret &secret);
/** Key that seals the notes left at the meeting point (meet.h). */
Key DeriveMeetKey(const Secret &secret);
/**
 * What names a session at the meeting point. Derived from the secret in one
 * direction only, so the meeting point learns nothing it could join with.
 */
std::array<uint8_t, 16> DeriveMeetRoom(const Secret &secret);

struct SessionKeys
{
	Key guest_to_host;
	Key host_to_guest;
};

SessionKeys DeriveSessionKeys(const Secret &secret, const HandshakeNonce &guest_nonce, const HandshakeNonce &host_nonce);

struct PacketHeader
{
	PacketType type = PacketType::Bye;
	uint8_t version = 0;
	uint32_t conn_id = 0;
	uint64_t counter = 0;
};

bool ParseHeader(const uint8_t *packet, size_t size, PacketHeader &header);

/**
 * Builds a complete datagram: header, ciphertext and tag.
 * @return false if the payload is too large or encryption failed
 */
bool SealPacket(const Key &key, PacketType type, uint32_t conn_id, uint64_t counter,
		const uint8_t *payload, size_t payload_size, std::vector<uint8_t> &out);

/** Like SealPacket, with 12 random bytes in place of conn_id and counter. */
bool SealHandshakePacket(const Key &key, PacketType type, const uint8_t *payload, size_t payload_size,
		std::vector<uint8_t> &out);

/**
 * Verifies and decrypts a datagram.
 * @return false if it was not sealed with this key or was modified
 */
bool OpenPacket(const Key &key, const uint8_t *packet, size_t size, std::vector<uint8_t> &payload);

/**
 * Rejects packet counters that were already seen or are too old.
 * Counters start at 1; packets may arrive out of order within the window.
 */
class ReplayWindow
{
	public:
		static constexpr uint64_t kWindow = 2048;

		/** @return true if the counter is fresh; it is then recorded as seen */
		bool Accept(uint64_t counter);

	private:
		uint64_t highest = 0;
		std::array<uint64_t, kWindow / 64> bits{};
};

std::string Base64UrlEncode(const uint8_t *data, size_t size);
bool Base64UrlDecode(const std::string &text, std::vector<uint8_t> &out);

} // namespace coplay

#endif // COPLAY_CRYPTO_H
