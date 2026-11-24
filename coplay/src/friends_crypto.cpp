// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/friends_crypto.h>

#include <coplay/bytes.h>
#include <coplay/crypto.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

// OpenSSL 1.1.1 (some builds) and 3 (others) alike: the EC_KEY functions exist in both,
// deprecated in 3 but working
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

namespace coplay {
namespace friends {

// ------------------------------------------------------------------ small helpers

namespace {

struct EcKeyDel { void operator()(EC_KEY *k) const { EC_KEY_free(k); } };
struct BnDel { void operator()(BIGNUM *b) const { BN_free(b); } };
struct PointDel { void operator()(EC_POINT *p) const { EC_POINT_free(p); } };
struct SigDel { void operator()(ECDSA_SIG *s) const { ECDSA_SIG_free(s); } };
struct BnCtxDel { void operator()(BN_CTX *c) const { BN_CTX_free(c); } };
using EcKeyPtr = std::unique_ptr<EC_KEY, EcKeyDel>;
using BnPtr = std::unique_ptr<BIGNUM, BnDel>;
using PointPtr = std::unique_ptr<EC_POINT, PointDel>;

EcKeyPtr NewKey()
{
	return EcKeyPtr(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
}

/** The point of a public key, checked to be on the curve and not at infinity. */
PointPtr ReadPoint(const EC_GROUP *group, const Pub &pub)
{
	if(pub[0] != 0x04)
		return nullptr;
	PointPtr point(EC_POINT_new(group));
	if(!point || EC_POINT_oct2point(group, point.get(), pub.data(), pub.size(), nullptr) != 1)
		return nullptr;
	if(EC_POINT_is_at_infinity(group, point.get()) || EC_POINT_is_on_curve(group, point.get(), nullptr) != 1)
		return nullptr;
	return point;
}

bool WritePoint(const EC_GROUP *group, const EC_POINT *point, Pub &pub)
{
	return EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, pub.data(), pub.size(), nullptr) == pub.size();
}

/** An EC_KEY holding the private scalar and its public point. */
EcKeyPtr PrivateKey(const KeyPair &kp)
{
	EcKeyPtr key = NewKey();
	if(!key)
		return nullptr;
	BnPtr d(BN_bin2bn(kp.d.data(), static_cast<int>(kp.d.size()), nullptr));
	if(!d || EC_KEY_set_private_key(key.get(), d.get()) != 1)
		return nullptr;
	PointPtr point = ReadPoint(EC_KEY_get0_group(key.get()), kp.pub);
	if(!point || EC_KEY_set_public_key(key.get(), point.get()) != 1)
		return nullptr;
	return key;
}

void PutU32(Bytes &out, uint32_t v)
{
	ByteWriter w(out);
	w.U32(v);
}

template<size_t N> bool ReadFixed(ByteReader &r, std::array<uint8_t, N> &out)
{
	return r.Bytes(out.data(), N) && r.ok();
}

bool ReadBytes(ByteReader &r, size_t n, Bytes &out)
{
	out.assign(n, 0);
	return r.Bytes(out.data(), n) && r.ok();
}

bool DefaultRandom(uint8_t *out, size_t size)
{
	return RandomBytes(out, size);
}

bool NewPair(const KeySource &keys, KeyPair &out)
{
	return keys ? keys(out) : GenerateKeyPair(out);
}

bool Rnd(const RandomSource &rnd, uint8_t *out, size_t size)
{
	return rnd ? rnd(out, size) : DefaultRandom(out, size);
}

/** The key and nonce of a message key: HKDF(-, mk, info, 44). */
void MessageKey(const Key32 &mk, const char *info, uint8_t key[32], uint8_t nonce[12])
{
	Bytes x = Hkdf(Bytes(), B(mk), info, 44);
	std::memcpy(key, x.data(), 32);
	std::memcpy(nonce, x.data() + 32, 12);
}

/** KDF_CK: the next chain key and the message key. */
void ChainStep(Key32 &ck, Key32 &mk)
{
	const uint8_t one = 0x01, two = 0x02;
	mk = Hmac(ck.data(), ck.size(), &one, 1);
	ck = Hmac(ck.data(), ck.size(), &two, 1);
}

/** KDF_RK: the new root key and a chain key. */
void RootStep(Key32 &rk, const Key32 &dh, Key32 &ck)
{
	Bytes x = Hkdf(B(rk), B(dh), "psrpt ratchet", 64);
	std::memcpy(rk.data(), x.data(), 32);
	std::memcpy(ck.data(), x.data() + 32, 32);
}

template<size_t N> std::array<uint8_t, N> Prefix(const Key32 &h)
{
	std::array<uint8_t, N> out{};
	std::memcpy(out.data(), h.data(), N);
	return out;
}

uint32_t U32At(const uint8_t *p)
{
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

} // namespace

// ------------------------------------------------------------------ primitives

bool GenerateKeyPair(KeyPair &out)
{
	EcKeyPtr key = NewKey();
	if(!key || EC_KEY_generate_key(key.get()) != 1)
		return false;
	const BIGNUM *d = EC_KEY_get0_private_key(key.get());
	if(!d || BN_bn2binpad(d, out.d.data(), static_cast<int>(out.d.size())) != static_cast<int>(out.d.size()))
		return false;
	return WritePoint(EC_KEY_get0_group(key.get()), EC_KEY_get0_public_key(key.get()), out.pub);
}

bool KeyPairFromPrivate(const Key32 &d, KeyPair &out)
{
	EcKeyPtr key = NewKey();
	if(!key)
		return false;
	const EC_GROUP *group = EC_KEY_get0_group(key.get());
	BnPtr bn(BN_bin2bn(d.data(), static_cast<int>(d.size()), nullptr));
	BnPtr order(BN_new());
	if(!bn || !order || EC_GROUP_get_order(group, order.get(), nullptr) != 1)
		return false;
	// 0 < d < n
	if(BN_is_zero(bn.get()) || BN_cmp(bn.get(), order.get()) >= 0)
		return false;
	PointPtr point(EC_POINT_new(group));
	if(!point || EC_POINT_mul(group, point.get(), bn.get(), nullptr, nullptr, nullptr) != 1)
		return false;
	out.d = d;
	return WritePoint(group, point.get(), out.pub);
}

bool ValidPublic(const Pub &pub)
{
	EcKeyPtr key = NewKey();
	return key && ReadPoint(EC_KEY_get0_group(key.get()), pub) != nullptr;
}

bool Dh(const KeyPair &own, const Pub &other, Key32 &out)
{
	EcKeyPtr key = PrivateKey(own);
	if(!key)
		return false;
	PointPtr point = ReadPoint(EC_KEY_get0_group(key.get()), other);
	if(!point)
		return false;
	return ECDH_compute_key(out.data(), out.size(), point.get(), key.get(), nullptr) == static_cast<int>(out.size());
}

bool Sign(const KeyPair &kp, const uint8_t *msg, size_t size, Sig &out)
{
	EcKeyPtr key = PrivateKey(kp);
	if(!key)
		return false;
	Key32 digest = Sha256(msg, size);
	std::unique_ptr<ECDSA_SIG, SigDel> sig(ECDSA_do_sign(digest.data(), static_cast<int>(digest.size()), key.get()));
	if(!sig)
		return false;
	const BIGNUM *r = nullptr, *s = nullptr;
	ECDSA_SIG_get0(sig.get(), &r, &s);
	return BN_bn2binpad(r, out.data(), 32) == 32 && BN_bn2binpad(s, out.data() + 32, 32) == 32;
}

bool Sign(const KeyPair &key, const Bytes &msg, Sig &out)
{
	return Sign(key, msg.data(), msg.size(), out);
}

bool Verify(const Pub &pub, const uint8_t *msg, size_t size, const Sig &sig)
{
	EcKeyPtr key = NewKey();
	if(!key)
		return false;
	PointPtr point = ReadPoint(EC_KEY_get0_group(key.get()), pub);
	if(!point || EC_KEY_set_public_key(key.get(), point.get()) != 1)
		return false;
	std::unique_ptr<ECDSA_SIG, SigDel> s(ECDSA_SIG_new());
	BIGNUM *r_bn = BN_bin2bn(sig.data(), 32, nullptr);
	BIGNUM *s_bn = BN_bin2bn(sig.data() + 32, 32, nullptr);
	if(!s || !r_bn || !s_bn || ECDSA_SIG_set0(s.get(), r_bn, s_bn) != 1)
	{
		BN_free(r_bn);
		BN_free(s_bn);
		return false;
	}
	Key32 digest = Sha256(msg, size);
	return ECDSA_do_verify(digest.data(), static_cast<int>(digest.size()), s.get(), key.get()) == 1;
}

bool Verify(const Pub &pub, const Bytes &msg, const Sig &sig)
{
	return Verify(pub, msg.data(), msg.size(), sig);
}

Key32 Sha256(const uint8_t *data, size_t size)
{
	Key32 out{};
	SHA256(data, size, out.data());
	return out;
}

Key32 Sha256(const Bytes &data)
{
	return Sha256(data.data(), data.size());
}

Key32 Hmac(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size)
{
	Key32 out{};
	unsigned int len = 0;
	static const uint8_t empty = 0;
	HMAC(EVP_sha256(), key_size ? key : &empty, static_cast<int>(key_size), data, size, out.data(), &len);
	return out;
}

Bytes Hkdf(const Bytes &salt_in, const Bytes &ikm, const std::string &info, size_t size)
{
	Bytes salt = salt_in.empty() ? Bytes(32, 0) : salt_in;
	Key32 prk = Hmac(salt.data(), salt.size(), ikm.data(), ikm.size());
	Bytes out;
	Bytes t;
	for(uint8_t i = 1; out.size() < size; i++)
	{
		Bytes block = t;
		block.insert(block.end(), info.begin(), info.end());
		block.push_back(i);
		Key32 h = Hmac(prk.data(), prk.size(), block.data(), block.size());
		t.assign(h.begin(), h.end());
		out.insert(out.end(), t.begin(), t.end());
	}
	out.resize(size);
	return out;
}

bool Seal(const uint8_t key[32], const uint8_t nonce[12], const Bytes &ad, const Bytes &plain, Bytes &out)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	out.assign(plain.size() + 16, 0);
	do
	{
		if(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1)
			break;
		if(EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1)
			break;
		if(!ad.empty() && EVP_EncryptUpdate(ctx, nullptr, &len, ad.data(), static_cast<int>(ad.size())) != 1)
			break;
		if(!plain.empty() && EVP_EncryptUpdate(ctx, out.data(), &len, plain.data(), static_cast<int>(plain.size())) != 1)
			break;
		if(EVP_EncryptFinal_ex(ctx, out.data() + plain.size(), &len) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + plain.size()) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		out.clear();
	return ok;
}

bool Open(const uint8_t key[32], const uint8_t nonce[12], const Bytes &ad, const Bytes &sealed, Bytes &out)
{
	if(sealed.size() < 16)
		return false;
	const size_t n = sealed.size() - 16;
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	out.assign(n, 0);
	uint8_t tag[16];
	std::memcpy(tag, sealed.data() + n, 16);
	uint8_t scratch[16];
	do
	{
		if(EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1)
			break;
		if(EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1)
			break;
		if(!ad.empty() && EVP_DecryptUpdate(ctx, nullptr, &len, ad.data(), static_cast<int>(ad.size())) != 1)
			break;
		if(n && EVP_DecryptUpdate(ctx, out.data(), &len, sealed.data(), static_cast<int>(n)) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1)
			break;
		if(EVP_DecryptFinal_ex(ctx, scratch, &len) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		out.clear();
	return ok;
}

Key32 Pbkdf2(const std::string &passphrase, const uint8_t *salt, size_t salt_size, uint32_t iterations)
{
	Key32 out{};
	PKCS5_PBKDF2_HMAC(passphrase.data(), static_cast<int>(passphrase.size()), salt, static_cast<int>(salt_size),
		static_cast<int>(iterations), EVP_sha256(), static_cast<int>(out.size()), out.data());
	return out;
}

Bytes Label(const std::string &text)
{
	return Bytes(text.begin(), text.end());
}

Bytes Cat(std::initializer_list<Bytes> parts)
{
	Bytes out;
	for(const Bytes &p : parts)
		out.insert(out.end(), p.begin(), p.end());
	return out;
}

// ------------------------------------------------------------------ names

Id16 Fingerprint(const Pub &iks, const Pub &ikd)
{
	return Prefix<16>(Sha256(Cat({ Label("psrpt id"), B(iks), B(ikd) })));
}

Id16 RequestBox(const Id16 &link_key)
{
	return Prefix<16>(Sha256(Cat({ Label("psrpt requests"), B(link_key) })));
}

Id16 InboxName(const Id16 &fp, const DeviceId &device)
{
	return Prefix<16>(Sha256(Cat({ Label("psrpt inbox"), B(fp), B(device) })));
}

Id16 GroupBox(const Id16 &group_id)
{
	return Prefix<16>(Sha256(Cat({ Label("psrpt group"), B(group_id) })));
}

Id16 LinkBox(const Id16 &link_secret)
{
	return Prefix<16>(Sha256(Cat({ Label("psrpt link"), B(link_secret) })));
}

Id16 StatusBox(const Key32 &epoch_key, uint32_t utc_day)
{
	const Bytes day{ static_cast<uint8_t>(utc_day >> 24), static_cast<uint8_t>(utc_day >> 16), static_cast<uint8_t>(utc_day >> 8),
		static_cast<uint8_t>(utc_day) };
	return Prefix<16>(Sha256(Cat({ Label("psrpt status box"), B(epoch_key), day })));
}

std::string SafetyCode(const Id16 &a, const Id16 &b)
{
	const bool a_first = std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
	const Id16 &lo = a_first ? a : b;
	const Id16 &hi = a_first ? b : a;
	Key32 h = Sha256(Cat({ Label("psrpt safety"), B(lo), B(hi) }));
	std::string out;
	for(int i = 0; i < 5; i++)
	{
		char group[8];
		std::snprintf(group, sizeof(group), "%05u", static_cast<unsigned>(U32At(h.data() + 4 * i) % 100000));
		if(i)
			out += ' ';
		out += group;
	}
	return out;
}

// ------------------------------------------------------------------ identity and card

bool CompleteIdentity(Identity &id)
{
	if(!Sign(id.iks, Cat({ Label("psrpt ikd"), B(id.ikd.pub) }), id.ikd_sig))
		return false;
	id.fp = Fingerprint(id.iks.pub, id.ikd.pub);
	return true;
}

bool MakeIdentity(Identity &out, const KeySource &keys)
{
	return NewPair(keys, out.iks) && NewPair(keys, out.ikd) && CompleteIdentity(out);
}

static Bytes SpkMessage(const Id16 &fp, const DeviceId &device, uint32_t spk_id, const Pub &spk)
{
	Bytes id;
	PutU32(id, spk_id);
	return Cat({ Label("psrpt spk"), B(fp), B(device), id, B(spk) });
}

bool SignPrekey(const Identity &id, const DeviceId &device, uint32_t spk_id, const Pub &spk, Sig &out)
{
	return Sign(id.iks, SpkMessage(id.fp, device, spk_id, spk), out);
}

/** Everything of a card that card_sig covers. */
static Bytes CardBody(const Card &card)
{
	Bytes out;
	ByteWriter w(out);
	w.U8(kProtocolVersion);
	w.Bytes(card.iks.data(), card.iks.size());
	w.Bytes(card.ikd.data(), card.ikd.size());
	w.Bytes(card.ikd_sig.data(), card.ikd_sig.size());
	w.U32(card.seq);
	w.U8(static_cast<uint8_t>(card.devices.size()));
	for(const CardDevice &d : card.devices)
	{
		w.Bytes(d.id.data(), d.id.size());
		w.U8(d.kind);
		w.U32(d.spk_id);
		w.Bytes(d.spk.data(), d.spk.size());
		w.Bytes(d.spk_sig.data(), d.spk_sig.size());
	}
	w.U16(static_cast<uint16_t>(card.profile.size()));
	w.Bytes(card.profile.data(), card.profile.size());
	return out;
}

bool EncodeCard(const Identity &id, Card &card, Bytes &out)
{
	if(card.devices.empty() || card.devices.size() > kMaxDevices || card.profile.size() > 65535)
		return false;
	for(const CardDevice &d : card.devices)
	{
		if(d.kind != 1 && d.kind != 2)
			return false;
	}
	card.iks = id.iks.pub;
	card.ikd = id.ikd.pub;
	card.ikd_sig = id.ikd_sig;
	card.fp = id.fp;
	out = CardBody(card);
	if(!Sign(id.iks, Cat({ Label("psrpt card"), out }), card.card_sig))
		return false;
	out.insert(out.end(), card.card_sig.begin(), card.card_sig.end());
	return true;
}

bool DecodeCard(const Bytes &data, Card &card)
{
	ByteReader r(data.data(), data.size());
	if(r.U8() != kProtocolVersion)
		return false;
	Card c;
	if(!ReadFixed(r, c.iks) || !ReadFixed(r, c.ikd) || !ReadFixed(r, c.ikd_sig))
		return false;
	c.seq = r.U32();
	const uint8_t count = r.U8();
	if(!r.ok() || count == 0 || count > kMaxDevices)
		return false;
	for(uint8_t i = 0; i < count; i++)
	{
		CardDevice d;
		if(!ReadFixed(r, d.id))
			return false;
		d.kind = r.U8();
		d.spk_id = r.U32();
		if(!ReadFixed(r, d.spk) || !ReadFixed(r, d.spk_sig) || (d.kind != 1 && d.kind != 2))
			return false;
		c.devices.push_back(d);
	}
	const uint16_t profile_size = r.U16();
	if(!r.ok() || !ReadBytes(r, profile_size, c.profile) || !ReadFixed(r, c.card_sig))
		return false;
	if(r.remaining() != 0)
		return false;
	// Every signature: the identity's own, the card's, each prekey's
	if(!Verify(c.iks, Cat({ Label("psrpt ikd"), B(c.ikd) }), c.ikd_sig) || !ValidPublic(c.ikd))
		return false;
	c.fp = Fingerprint(c.iks, c.ikd);
	const Bytes body(data.begin(), data.end() - 64);
	if(!Verify(c.iks, Cat({ Label("psrpt card"), body }), c.card_sig))
		return false;
	for(const CardDevice &d : c.devices)
	{
		if(!ValidPublic(d.spk) || !Verify(c.iks, SpkMessage(c.fp, d.id, d.spk_id, d.spk), d.spk_sig))
			return false;
	}
	card = std::move(c);
	return true;
}

static Key32 ProfileKey(const Id16 &link_key)
{
	Bytes k = Hkdf(Bytes(), B(link_key), "psrpt profile", 32);
	Key32 out{};
	std::memcpy(out.data(), k.data(), 32);
	return out;
}

bool SealProfile(const Id16 &link_key, const Id16 &fp, const std::string &json, Bytes &out, const RandomSource &rnd)
{
	uint8_t nonce[12];
	if(!Rnd(rnd, nonce, sizeof(nonce)))
		return false;
	Key32 key = ProfileKey(link_key);
	Bytes sealed;
	if(!Seal(key.data(), nonce, B(fp), Bytes(json.begin(), json.end()), sealed))
		return false;
	out.assign(nonce, nonce + 12);
	out.insert(out.end(), sealed.begin(), sealed.end());
	return true;
}

bool OpenProfile(const Id16 &link_key, const Id16 &fp, const Bytes &sealed, std::string &json)
{
	if(sealed.size() < 12 + 16)
		return false;
	Key32 key = ProfileKey(link_key);
	Bytes plain;
	if(!Open(key.data(), sealed.data(), B(fp), Bytes(sealed.begin() + 12, sealed.end()), plain))
		return false;
	json.assign(plain.begin(), plain.end());
	return true;
}

// ------------------------------------------------------------------ X3DH

static Key32 X3dhKey(const std::vector<Key32> &dhs)
{
	Bytes ikm(32, 0xFF);
	for(const Key32 &d : dhs)
		ikm.insert(ikm.end(), d.begin(), d.end());
	Bytes k = Hkdf(Bytes(), ikm, "psrpt x3dh", 32);
	Key32 out{};
	std::memcpy(out.data(), k.data(), 32);
	return out;
}

bool X3dhInitiate(const Identity &own, const DeviceId &own_device, const PrekeyBundle &other, X3dhStart &out, const KeySource &keys)
{
	if(!NewPair(keys, out.ek))
		return false;
	Key32 dh1, dh2, dh3, dh4;
	if(!Dh(own.ikd, other.spk, dh1) || !Dh(out.ek, other.ikd, dh2) || !Dh(out.ek, other.spk, dh3))
		return false;
	std::vector<Key32> dhs = { dh1, dh2, dh3 };
	if(other.opk_id)
	{
		if(!Dh(out.ek, other.opk, dh4))
			return false;
		dhs.push_back(dh4);
	}
	out.sk = X3dhKey(dhs);
	out.ad = Cat({ B(own.fp), B(own_device), B(other.fp), B(other.device) });
	return true;
}

bool X3dhRespond(const Identity &own, const DeviceId &own_device, const KeyPair &spk, const KeyPair *opk,
	const Id16 &other_fp, const Pub &other_ikd, const DeviceId &other_device, const Pub &other_ek, Key32 &sk, Bytes &ad)
{
	Key32 dh1, dh2, dh3, dh4;
	if(!Dh(spk, other_ikd, dh1) || !Dh(own.ikd, other_ek, dh2) || !Dh(spk, other_ek, dh3))
		return false;
	std::vector<Key32> dhs = { dh1, dh2, dh3 };
	if(opk)
	{
		if(!Dh(*opk, other_ek, dh4))
			return false;
		dhs.push_back(dh4);
	}
	sk = X3dhKey(dhs);
	ad = Cat({ B(other_fp), B(other_device), B(own.fp), B(own_device) });
	return true;
}

// ------------------------------------------------------------------ the Double Ratchet

Bytes RatchetHeader::Encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.Bytes(dh.data(), dh.size());
	w.U32(pn);
	w.U32(n);
	return out;
}

bool RatchetHeader::Decode(const uint8_t *data, size_t size, RatchetHeader &out)
{
	if(size != kSize)
		return false;
	ByteReader r(data, size);
	RatchetHeader h;
	if(!ReadFixed(r, h.dh))
		return false;
	h.pn = r.U32();
	h.n = r.U32();
	if(!r.ok())
		return false;
	out = h;
	return true;
}

bool Ratchet::InitInitiator(const Key32 &sk, const Pub &other_spk, const KeySource &keys)
{
	*this = Ratchet();
	if(!NewPair(keys, dhs))
		return false;
	has_dhr = true;
	dhr = other_spk;
	Key32 dh;
	if(!Dh(dhs, dhr, dh))
		return false;
	rk = sk;
	RootStep(rk, dh, cks);
	has_cks = true;
	return true;
}

void Ratchet::InitResponder(const Key32 &sk, const KeyPair &spk)
{
	*this = Ratchet();
	dhs = spk;
	rk = sk;
}

bool Ratchet::Encrypt(const Bytes &ad, const Bytes &plain, RatchetHeader &header, Bytes &sealed)
{
	// The envelope gives the sealed text two length bytes
	if(!has_cks || plain.size() > kMaxPlain)
		return false;
	Key32 mk;
	Key32 ck = cks;
	ChainStep(ck, mk);
	header.dh = dhs.pub;
	header.pn = pn;
	header.n = ns;
	uint8_t key[32], nonce[12];
	MessageKey(mk, "psrpt message", key, nonce);
	if(!Seal(key, nonce, Cat({ ad, header.Encode() }), plain, sealed))
		return false;
	cks = ck;
	ns++;
	return true;
}

/** Keeps the message keys up to `until` of the current receiving chain. */
static bool SkipKeys(Ratchet &s, uint32_t until, uint64_t now_ms)
{
	if(!s.has_ckr || until <= s.nr)
		return true;
	// A message that claims to be far ahead is refused rather than costing thousands of steps
	if(until - s.nr > kMaxSkippedKeys)
		return false;
	while(s.nr < until)
	{
		Key32 mk;
		ChainStep(s.ckr, mk);
		s.skipped[{ s.dhr, s.nr }] = { mk, now_ms };
		s.nr++;
	}
	// The oldest go first when there are too many
	while(s.skipped.size() > kMaxSkippedKeys)
	{
		auto oldest = std::min_element(s.skipped.begin(), s.skipped.end(),
			[](const auto &a, const auto &b) { return a.second.at_ms < b.second.at_ms; });
		s.skipped.erase(oldest);
	}
	return true;
}

bool Ratchet::Decrypt(const Bytes &ad, const RatchetHeader &header, const Bytes &sealed, Bytes &plain, uint64_t now_ms,
	const KeySource &keys)
{
	// Keys of messages that are too old go
	for(auto it = skipped.begin(); it != skipped.end();)
	{
		if(now_ms > it->second.at_ms && now_ms - it->second.at_ms > kSkippedKeyMaxAgeMs)
			it = skipped.erase(it);
		else
			++it;
	}
	const Bytes full_ad = Cat({ ad, header.Encode() });
	// A message that was skipped before
	auto found = skipped.find({ header.dh, header.n });
	if(found != skipped.end())
	{
		uint8_t key[32], nonce[12];
		MessageKey(found->second.mk, "psrpt message", key, nonce);
		if(!Open(key, nonce, full_ad, sealed, plain))
			return false;
		skipped.erase(found);
		return true;
	}
	// The rest on a copy: the state changes only if the message opens
	Ratchet s = *this;
	if(!s.has_dhr || header.dh != s.dhr)
	{
		if(!SkipKeys(s, header.pn, now_ms))
			return false;
		// DHRatchet
		s.pn = s.ns;
		s.ns = 0;
		s.nr = 0;
		s.has_dhr = true;
		s.dhr = header.dh;
		Key32 dh;
		if(!Dh(s.dhs, s.dhr, dh))
			return false;
		RootStep(s.rk, dh, s.ckr);
		s.has_ckr = true;
		if(!NewPair(keys, s.dhs))
			return false;
		if(!Dh(s.dhs, s.dhr, dh))
			return false;
		RootStep(s.rk, dh, s.cks);
		s.has_cks = true;
	}
	if(!SkipKeys(s, header.n, now_ms))
		return false;
	Key32 mk;
	ChainStep(s.ckr, mk);
	s.nr++;
	uint8_t key[32], nonce[12];
	MessageKey(mk, "psrpt message", key, nonce);
	if(!Open(key, nonce, full_ad, sealed, plain))
		return false;
	*this = std::move(s);
	return true;
}

Bytes Ratchet::Save() const
{
	Bytes out;
	ByteWriter w(out);
	w.U8(1);
	w.Bytes(dhs.d.data(), 32);
	w.Bytes(dhs.pub.data(), 65);
	w.U8(has_dhr ? 1 : 0);
	w.Bytes(dhr.data(), 65);
	w.Bytes(rk.data(), 32);
	w.U8(has_cks ? 1 : 0);
	w.Bytes(cks.data(), 32);
	w.U8(has_ckr ? 1 : 0);
	w.Bytes(ckr.data(), 32);
	w.U32(ns);
	w.U32(nr);
	w.U32(pn);
	w.U16(static_cast<uint16_t>(skipped.size()));
	for(const auto &e : skipped)
	{
		w.Bytes(e.first.first.data(), 65);
		w.U32(e.first.second);
		w.Bytes(e.second.mk.data(), 32);
		w.U32(static_cast<uint32_t>(e.second.at_ms >> 32));
		w.U32(static_cast<uint32_t>(e.second.at_ms));
	}
	return out;
}

bool Ratchet::Load(const Bytes &data, Ratchet &out)
{
	ByteReader r(data.data(), data.size());
	if(r.U8() != 1)
		return false;
	Ratchet s;
	if(!ReadFixed(r, s.dhs.d) || !ReadFixed(r, s.dhs.pub))
		return false;
	s.has_dhr = r.U8() != 0;
	if(!ReadFixed(r, s.dhr) || !ReadFixed(r, s.rk))
		return false;
	s.has_cks = r.U8() != 0;
	if(!ReadFixed(r, s.cks))
		return false;
	s.has_ckr = r.U8() != 0;
	if(!ReadFixed(r, s.ckr))
		return false;
	s.ns = r.U32();
	s.nr = r.U32();
	s.pn = r.U32();
	const uint16_t count = r.U16();
	if(!r.ok() || count > kMaxSkippedKeys)
		return false;
	for(uint16_t i = 0; i < count; i++)
	{
		Pub dh;
		Skipped k;
		if(!ReadFixed(r, dh))
			return false;
		const uint32_t n = r.U32();
		if(!ReadFixed(r, k.mk))
			return false;
		const uint64_t hi = r.U32();
		const uint64_t lo = r.U32();
		k.at_ms = (hi << 32) | lo;
		s.skipped[{ dh, n }] = k;
	}
	if(!r.ok() || r.remaining() != 0)
		return false;
	out = std::move(s);
	return true;
}

// ------------------------------------------------------------------ envelopes

Bytes Envelope::Encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.U8(kProtocolVersion);
	w.U8(kind);
	w.Bytes(from_fp.data(), from_fp.size());
	w.Bytes(from_device.data(), from_device.size());
	w.Bytes(to_device.data(), to_device.size());
	if(kind == 1)
	{
		w.Bytes(ek.data(), ek.size());
		w.U32(spk_id);
		w.U32(opk_id);
	}
	Bytes h = header.Encode();
	w.Bytes(h.data(), h.size());
	w.U16(static_cast<uint16_t>(sealed.size()));
	w.Bytes(sealed.data(), sealed.size());
	return out;
}

bool Envelope::Decode(const Bytes &data, Envelope &out)
{
	ByteReader r(data.data(), data.size());
	if(r.U8() != kProtocolVersion)
		return false;
	Envelope e;
	e.kind = r.U8();
	if(!r.ok() || (e.kind != 1 && e.kind != 2))
		return false;
	if(!ReadFixed(r, e.from_fp) || !ReadFixed(r, e.from_device) || !ReadFixed(r, e.to_device))
		return false;
	if(e.kind == 1)
	{
		if(!ReadFixed(r, e.ek))
			return false;
		e.spk_id = r.U32();
		e.opk_id = r.U32();
	}
	uint8_t h[RatchetHeader::kSize];
	if(!r.Bytes(h, sizeof(h)) || !RatchetHeader::Decode(h, sizeof(h), e.header))
		return false;
	const uint16_t n = r.U16();
	if(!r.ok() || !ReadBytes(r, n, e.sealed) || r.remaining() != 0)
		return false;
	out = std::move(e);
	return true;
}

// ------------------------------------------------------------------ groups

bool SenderChain::New(uint32_t chain_number, const KeySource &keys, const RandomSource &rnd)
{
	*this = SenderChain();
	chain = chain_number;
	if(!Rnd(rnd, ck.data(), ck.size()) || !NewPair(keys, sign))
		return false;
	sign_pub = sign.pub;
	return true;
}

static Bytes GroupAd(const GroupMessage &m)
{
	Bytes nums;
	PutU32(nums, m.chain);
	PutU32(nums, m.iter);
	return Cat({ B(m.group), B(m.fp), B(m.device), nums });
}

static Bytes GroupSigned(const GroupMessage &m)
{
	Bytes out;
	ByteWriter w(out);
	w.U8(kProtocolVersion);
	w.Bytes(m.group.data(), 16);
	w.Bytes(m.fp.data(), 16);
	w.Bytes(m.device.data(), 8);
	w.U32(m.chain);
	w.U32(m.iter);
	w.U16(static_cast<uint16_t>(m.sealed.size()));
	w.Bytes(m.sealed.data(), m.sealed.size());
	return out;
}

Bytes GroupMessage::Encode() const
{
	Bytes out = GroupSigned(*this);
	out.insert(out.end(), sig.begin(), sig.end());
	return out;
}

bool GroupMessage::Decode(const Bytes &data, GroupMessage &out)
{
	ByteReader r(data.data(), data.size());
	if(r.U8() != kProtocolVersion)
		return false;
	GroupMessage m;
	if(!ReadFixed(r, m.group) || !ReadFixed(r, m.fp) || !ReadFixed(r, m.device))
		return false;
	m.chain = r.U32();
	m.iter = r.U32();
	const uint16_t n = r.U16();
	if(!r.ok() || !ReadBytes(r, n, m.sealed) || !ReadFixed(r, m.sig) || r.remaining() != 0)
		return false;
	out = std::move(m);
	return true;
}

bool GroupSeal(SenderChain &own, const Id16 &group, const Id16 &fp, const DeviceId &device, const Bytes &plain, Bytes &out)
{
	if(plain.size() > kMaxPlain)
		return false;
	GroupMessage m;
	m.group = group;
	m.fp = fp;
	m.device = device;
	m.chain = own.chain;
	m.iter = own.iter;
	Key32 ck = own.ck;
	Key32 mk;
	ChainStep(ck, mk);
	uint8_t key[32], nonce[12];
	MessageKey(mk, "psrpt group message", key, nonce);
	if(!Seal(key, nonce, GroupAd(m), plain, m.sealed))
		return false;
	if(!Sign(own.sign, GroupSigned(m), m.sig))
		return false;
	own.ck = ck;
	own.iter++;
	out = m.Encode();
	return true;
}

bool GroupOpen(SenderChain &theirs, const GroupMessage &m, Bytes &plain)
{
	if(m.chain != theirs.chain || !Verify(theirs.sign_pub, GroupSigned(m), m.sig))
		return false;
	Key32 mk;
	SenderChain s = theirs;
	auto skipped = s.skipped.find(m.iter);
	if(skipped != s.skipped.end())
	{
		mk = skipped->second;
		s.skipped.erase(skipped);
	}
	else
	{
		if(m.iter < s.iter || m.iter - s.iter > 2000)
			return false;
		while(s.iter < m.iter)
		{
			Key32 k;
			ChainStep(s.ck, k);
			s.skipped[s.iter] = k;
			s.iter++;
		}
		ChainStep(s.ck, mk);
		s.iter++;
		while(s.skipped.size() > 2000)
			s.skipped.erase(s.skipped.begin());
	}
	uint8_t key[32], nonce[12];
	MessageKey(mk, "psrpt group message", key, nonce);
	if(!Open(key, nonce, GroupAd(m), m.sealed, plain))
		return false;
	theirs = std::move(s);
	return true;
}

// ------------------------------------------------------------------ status

Key32 FriendsKeyForDay(const Key32 &epoch_key, uint32_t day)
{
	Key32 k = epoch_key;
	static const std::string next = "psrpt next day";
	for(uint32_t i = 0; i < day; i++)
		k = Hmac(k.data(), k.size(), reinterpret_cast<const uint8_t *>(next.data()), next.size());
	return k;
}

bool SealStatus(const Key32 &epoch_key, uint32_t epoch, uint32_t day, const Id16 &fp, const std::string &json, Bytes &out,
	const RandomSource &rnd)
{
	uint8_t nonce[12];
	if(!Rnd(rnd, nonce, sizeof(nonce)))
		return false;
	Key32 k = FriendsKeyForDay(epoch_key, day);
	Bytes sealed;
	if(!Seal(k.data(), nonce, B(fp), Bytes(json.begin(), json.end()), sealed))
		return false;
	out.clear();
	PutU32(out, epoch);
	PutU32(out, day);
	out.insert(out.end(), nonce, nonce + 12);
	out.insert(out.end(), sealed.begin(), sealed.end());
	return true;
}

bool OpenStatus(const Key32 &epoch_key, uint32_t epoch, const Id16 &fp, const Bytes &note, std::string &json, uint32_t *day_out)
{
	if(note.size() < 8 + 12 + 16)
		return false;
	if(U32At(note.data()) != epoch)
		return false;
	const uint32_t day = U32At(note.data() + 4);
	// A note from more than a year after the epoch is no note of ours
	if(day > 366)
		return false;
	Key32 k = FriendsKeyForDay(epoch_key, day);
	Bytes plain;
	if(!Open(k.data(), note.data() + 8, B(fp), Bytes(note.begin() + 20, note.end()), plain))
		return false;
	json.assign(plain.begin(), plain.end());
	if(day_out)
		*day_out = day;
	return true;
}

// ------------------------------------------------------------------ linking and backups

Key32 LinkKey(const Id16 &link_secret, const Key32 &dh)
{
	Bytes k = Hkdf(B(link_secret), B(dh), "psrpt link key", 32);
	Key32 out{};
	std::memcpy(out.data(), k.data(), 32);
	return out;
}

std::string LinkCode(const Key32 &link_key)
{
	Key32 h = Sha256(Cat({ Label("psrpt link code"), B(link_key) }));
	char text[16];
	const unsigned code = static_cast<unsigned>(U32At(h.data()) % 1000000);
	std::snprintf(text, sizeof(text), "%03u %03u", code / 1000, code % 1000);
	return text;
}

static const char kBackupHead[] = "PSRPT-BACKUP 1\n";

bool SealBackup(const std::string &passphrase, const std::string &json, std::string &out, uint32_t iterations, const RandomSource &rnd)
{
	uint8_t salt[16], nonce[12];
	if(!Rnd(rnd, salt, sizeof(salt)) || !Rnd(rnd, nonce, sizeof(nonce)))
		return false;
	Key32 key = Pbkdf2(passphrase, salt, sizeof(salt), iterations);
	Bytes sealed;
	if(!Seal(key.data(), nonce, Label("psrpt backup"), Bytes(json.begin(), json.end()), sealed))
		return false;
	Bytes all(salt, salt + 16);
	PutU32(all, iterations);
	all.insert(all.end(), nonce, nonce + 12);
	all.insert(all.end(), sealed.begin(), sealed.end());
	out = kBackupHead + Base64UrlEncode(all.data(), all.size());
	return true;
}

bool OpenBackup(const std::string &passphrase, const std::string &text, std::string &json)
{
	// The first line, also when an editor saved it with Windows line ends
	const std::string head = "PSRPT-BACKUP 1";
	if(text.compare(0, head.size(), head) != 0)
		return false;
	size_t at = head.size();
	if(at < text.size() && text[at] == '\r')
		at++;
	if(at >= text.size() || text[at] != '\n')
		return false;
	std::string body = text.substr(at + 1);
	while(!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' '))
		body.pop_back();
	Bytes all;
	if(!Base64UrlDecode(body, all) || all.size() < 16 + 4 + 12 + 16)
		return false;
	const uint32_t iterations = U32At(all.data() + 16);
	// Far fewer rounds than ever written would make a weak key; far more would hang the app
	if(iterations < 100000 || iterations > 10000000)
		return false;
	Key32 key = Pbkdf2(passphrase, all.data(), 16, iterations);
	Bytes plain;
	if(!Open(key.data(), all.data() + 20, Label("psrpt backup"), Bytes(all.begin() + 32, all.end()), plain))
		return false;
	json.assign(plain.begin(), plain.end());
	return true;
}

// ------------------------------------------------------------------ signed requests

Bytes RequestMessage(const std::string &op, const std::string &t, const std::map<std::string, std::string> &fields)
{
	std::string text = "psrpt friends 1\n" + op + "\n" + t + "\n";
	// std::map is sorted by name already
	for(const auto &f : fields)
		text += f.first + "=" + f.second + "\n";
	return Bytes(text.begin(), text.end());
}

} // namespace friends
} // namespace coplay
