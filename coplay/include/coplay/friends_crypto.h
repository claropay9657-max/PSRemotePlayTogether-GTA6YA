// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_FRIENDS_CRYPTO_H
#define COPLAY_FRIENDS_CRYPTO_H

/*
 * The cryptography of friends, chats and status (docs/friends.md, docs/friends-protocol.md):
 * P-256 keys, Signal's X3DH and Double Ratchet, Sender Keys for groups, the card, the status
 * note, linking a device and the backup file. Byte for byte what web/js/friends/crypto.js does;
 * both are held to test/web/friends-vectors.json.
 *
 * Nothing here talks to a network or keeps anything: the app does that with what comes out.
 * Every function that needs randomness takes it from RandomBytes() unless it is given a source,
 * so that the test vectors can be made with fixed bytes.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace coplay {
namespace friends {

using Bytes = std::vector<uint8_t>;
using Key32 = std::array<uint8_t, 32>;
using Pub = std::array<uint8_t, 65>;
using Sig = std::array<uint8_t, 64>;
using Id16 = std::array<uint8_t, 16>;
using DeviceId = std::array<uint8_t, 8>;

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMaxDevices = 10;
constexpr size_t kMaxSkippedKeys = 1000;
constexpr uint64_t kSkippedKeyMaxAgeMs = 30ull * 24 * 3600 * 1000;
constexpr uint32_t kBackupIterations = 600000;
/** The most a payload may be: the envelope gives the sealed text (16 bytes more) two length bytes. */
constexpr size_t kMaxPlain = 65535 - 16;

/** A P-256 key pair: the private scalar and the uncompressed public point. */
struct KeyPair
{
	Key32 d{};
	Pub pub{};
	bool operator==(const KeyPair &o) const { return d == o.d && pub == o.pub; }
};

/** Where new key pairs come from: GenerateKeyPair() unless a test gives fixed ones. */
using KeySource = std::function<bool(KeyPair &out)>;
/** Where random bytes come from: RandomBytes() unless a test gives fixed ones. */
using RandomSource = std::function<bool(uint8_t *out, size_t size)>;

// ------------------------------------------------------------------ primitives

bool GenerateKeyPair(KeyPair &out);
/** The key pair of a private scalar; false if it is no valid scalar. */
bool KeyPairFromPrivate(const Key32 &d, KeyPair &out);
/** Whether a public key is a point on P-256 (and not the point at infinity). */
bool ValidPublic(const Pub &pub);
/** ECDH: the x coordinate of d·pub. */
bool Dh(const KeyPair &own, const Pub &other, Key32 &out);
/** ECDSA with SHA-256, as r || s. */
bool Sign(const KeyPair &key, const uint8_t *msg, size_t size, Sig &out);
bool Sign(const KeyPair &key, const Bytes &msg, Sig &out);
bool Verify(const Pub &pub, const uint8_t *msg, size_t size, const Sig &sig);
bool Verify(const Pub &pub, const Bytes &msg, const Sig &sig);
Key32 Sha256(const uint8_t *data, size_t size);
Key32 Sha256(const Bytes &data);
Key32 Hmac(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size);
/** HKDF-SHA256; an empty salt counts as 32 zero bytes. */
Bytes Hkdf(const Bytes &salt, const Bytes &ikm, const std::string &info, size_t size);
/** AES-256-GCM: the sealed text with the 16 tag bytes at the end. */
bool Seal(const uint8_t key[32], const uint8_t nonce[12], const Bytes &ad, const Bytes &plain, Bytes &out);
bool Open(const uint8_t key[32], const uint8_t nonce[12], const Bytes &ad, const Bytes &sealed, Bytes &out);
Key32 Pbkdf2(const std::string &passphrase, const uint8_t *salt, size_t salt_size, uint32_t iterations);

/** Joins byte strings and labels: Cat({ Label("psrpt card"), card_bytes }). */
Bytes Label(const std::string &text);
Bytes Cat(std::initializer_list<Bytes> parts);
template<size_t N> Bytes B(const std::array<uint8_t, N> &a) { return Bytes(a.begin(), a.end()); }

// ------------------------------------------------------------------ names

/** FP = H("psrpt id" || IKs || IKd)[0..16] */
Id16 Fingerprint(const Pub &iks, const Pub &ikd);
/** RB = H("psrpt requests" || LK)[0..16] */
Id16 RequestBox(const Id16 &link_key);
/** IB = H("psrpt inbox" || FP || device)[0..16] */
Id16 InboxName(const Id16 &fp, const DeviceId &device);
/** GB = H("psrpt group" || group_id)[0..16] */
Id16 GroupBox(const Id16 &group_id);
/** LB = H("psrpt link" || L)[0..16] */
Id16 LinkBox(const Id16 &link_secret);
/** SB = H("psrpt status box" || FK_epoch || u32 utc_day)[0..16]: where a status is kept that day,
 *  a name only friends (who have the friends key) can work out. utc_day = unix ms / 86400000. */
Id16 StatusBox(const Key32 &epoch_key, uint32_t utc_day);
/** Five groups of five digits, the same for both friends: "12345 67890 ..." */
std::string SafetyCode(const Id16 &fp_a, const Id16 &fp_b);

// ------------------------------------------------------------------ identity and card

struct Identity
{
	KeyPair iks; // signs
	KeyPair ikd; // agrees on keys
	Sig ikd_sig{};
	Id16 fp{};
};

/** A new identity; ikd_sig and fp filled in. */
bool MakeIdentity(Identity &out, const KeySource &keys = nullptr);
/** Fills ikd_sig and fp for two given key pairs. */
bool CompleteIdentity(Identity &id);

struct CardDevice
{
	DeviceId id{};
	uint8_t kind = 1; // 1 the app, 2 a web browser
	uint32_t spk_id = 0;
	Pub spk{};
	Sig spk_sig{};
};

struct Card
{
	Pub iks{};
	Pub ikd{};
	Sig ikd_sig{};
	uint32_t seq = 0;
	std::vector<CardDevice> devices;
	Bytes profile; // sealed with the link key; may be empty
	Sig card_sig{};
	Id16 fp{}; // worked out, not stored
};

/** spk_sig for a device's signed prekey. */
bool SignPrekey(const Identity &id, const DeviceId &device, uint32_t spk_id, const Pub &spk, Sig &out);
/** The card as bytes, signed (card_sig is filled in). */
bool EncodeCard(const Identity &id, Card &card, Bytes &out);
/** Reads a card and checks every signature in it; card.fp is filled in. */
bool DecodeCard(const Bytes &data, Card &card);

/** The sealed profile for a card: nonce || Seal(HKDF(-, LK, "psrpt profile"), nonce, FP, json). */
bool SealProfile(const Id16 &link_key, const Id16 &fp, const std::string &json, Bytes &out, const RandomSource &rnd = nullptr);
bool OpenProfile(const Id16 &link_key, const Id16 &fp, const Bytes &sealed, std::string &json);

// ------------------------------------------------------------------ X3DH

struct PrekeyBundle
{
	Id16 fp{};
	Pub ikd{};
	DeviceId device{};
	uint32_t spk_id = 0;
	Pub spk{};
	uint32_t opk_id = 0; // 0: none
	Pub opk{};
};

struct X3dhStart
{
	Key32 sk{};
	Bytes ad;
	KeyPair ek;
};

/** The one who starts: SK from its identity, a fresh ephemeral key and the other's bundle. */
bool X3dhInitiate(const Identity &own, const DeviceId &own_device, const PrekeyBundle &other, X3dhStart &out,
	const KeySource &keys = nullptr);
/** The one who answers: SK from its identity, its prekeys and the first message's keys. */
bool X3dhRespond(const Identity &own, const DeviceId &own_device, const KeyPair &spk, const KeyPair *opk,
	const Id16 &other_fp, const Pub &other_ikd, const DeviceId &other_device, const Pub &other_ek, Key32 &sk, Bytes &ad);

// ------------------------------------------------------------------ the Double Ratchet

struct RatchetHeader
{
	Pub dh{};
	uint32_t pn = 0;
	uint32_t n = 0;
	Bytes Encode() const;
	static bool Decode(const uint8_t *data, size_t size, RatchetHeader &out);
	static constexpr size_t kSize = 65 + 4 + 4;
};

struct Ratchet
{
	KeyPair dhs;
	bool has_dhr = false;
	Pub dhr{};
	Key32 rk{};
	bool has_cks = false;
	Key32 cks{};
	bool has_ckr = false;
	Key32 ckr{};
	uint32_t ns = 0;
	uint32_t nr = 0;
	uint32_t pn = 0;
	struct Skipped
	{
		Key32 mk{};
		uint64_t at_ms = 0;
	};
	/** Message keys of messages that have not arrived yet: (ratchet key, number). */
	std::map<std::pair<Pub, uint32_t>, Skipped> skipped;

	/** The one who starts, after X3DH: towards the other's signed prekey. */
	bool InitInitiator(const Key32 &sk, const Pub &other_spk, const KeySource &keys = nullptr);
	/** The one who answers, after X3DH: with its signed prekey's key pair. */
	void InitResponder(const Key32 &sk, const KeyPair &spk);

	/** Seals a payload; header and sealed text for the envelope. */
	bool Encrypt(const Bytes &ad, const Bytes &plain, RatchetHeader &header, Bytes &sealed);
	/** Opens a payload. The state changes only if it opens. */
	bool Decrypt(const Bytes &ad, const RatchetHeader &header, const Bytes &sealed, Bytes &plain, uint64_t now_ms,
		const KeySource &keys = nullptr);

	Bytes Save() const;
	static bool Load(const Bytes &data, Ratchet &out);
};

// ------------------------------------------------------------------ envelopes

struct Envelope
{
	uint8_t kind = 2; // 1 first message, 2 message
	Id16 from_fp{};
	DeviceId from_device{};
	DeviceId to_device{};
	// kind 1
	Pub ek{};
	uint32_t spk_id = 0;
	uint32_t opk_id = 0;
	RatchetHeader header;
	Bytes sealed;

	Bytes Encode() const;
	static bool Decode(const Bytes &data, Envelope &out);
};

// ------------------------------------------------------------------ groups (Sender Keys)

struct SenderChain
{
	uint32_t chain = 0;
	uint32_t iter = 0;
	Key32 ck{};
	KeyPair sign;   // own chain: the whole key pair
	Pub sign_pub{}; // somebody else's: only the public half
	std::map<uint32_t, Key32> skipped; // somebody else's: message keys of messages not yet seen

	bool New(uint32_t chain_number, const KeySource &keys = nullptr, const RandomSource &rnd = nullptr);
};

struct GroupMessage
{
	Id16 group{};
	Id16 fp{};
	DeviceId device{};
	uint32_t chain = 0;
	uint32_t iter = 0;
	Bytes sealed;
	Sig sig{};

	Bytes Encode() const;
	static bool Decode(const Bytes &data, GroupMessage &out);
};

/** Seals with the own chain and moves it on. */
bool GroupSeal(SenderChain &own, const Id16 &group, const Id16 &fp, const DeviceId &device, const Bytes &plain, Bytes &out);
/** Opens with the writer's chain (which moves on, keeping keys of skipped messages, at most 2000). */
bool GroupOpen(SenderChain &theirs, const GroupMessage &message, Bytes &plain);

// ------------------------------------------------------------------ status

/** FK_day(d) from the epoch's key: d steps of HMAC(k, "psrpt next day"). */
Key32 FriendsKeyForDay(const Key32 &epoch_key, uint32_t day);
/** u32 epoch || u32 day || nonce || Seal(FK_day, nonce, FP, json) */
bool SealStatus(const Key32 &epoch_key, uint32_t epoch, uint32_t day, const Id16 &fp, const std::string &json, Bytes &out,
	const RandomSource &rnd = nullptr);
/** Opens a status note with the epoch key the friend gave; epoch and day come out of the note. */
bool OpenStatus(const Key32 &epoch_key, uint32_t epoch, const Id16 &fp, const Bytes &note, std::string &json, uint32_t *day = nullptr);

// ------------------------------------------------------------------ linking a device, backups

Key32 LinkKey(const Id16 &link_secret, const Key32 &dh);
/** "123 456" */
std::string LinkCode(const Key32 &link_key);

/** PSRPT-BACKUP 1 and the sealed bundle. */
bool SealBackup(const std::string &passphrase, const std::string &json, std::string &out, uint32_t iterations = kBackupIterations,
	const RandomSource &rnd = nullptr);
bool OpenBackup(const std::string &passphrase, const std::string &text, std::string &json);

// ------------------------------------------------------------------ the server's signed requests

/** "psrpt friends 1\n" op "\n" t "\n" and every other field, sorted: name "=" value "\n" */
Bytes RequestMessage(const std::string &op, const std::string &t, const std::map<std::string, std::string> &fields);

} // namespace friends
} // namespace coplay

#endif // COPLAY_FRIENDS_CRYPTO_H
