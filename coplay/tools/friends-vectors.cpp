// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Writes test/web/friends-vectors.json: what the C++ side of friends' cryptography
// (coplay/friends_crypto.h) makes from fixed keys and fixed "random" bytes, for the
// browser's side (web/js/friends/crypto.js) to come out the same, byte for byte.
// ECDSA signatures are random each time: they are written to be verified, not made again.
//
// Run: friends-vectors > test/web/friends-vectors.json

#include <coplay/bytes.h>
#include <coplay/friends_crypto.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace coplay::friends;
using coplay::ByteWriter;

namespace {

std::string Hex(const uint8_t *p, size_t n)
{
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for(size_t i = 0; i < n; i++)
	{
		out += digits[p[i] >> 4];
		out += digits[p[i] & 15];
	}
	return out;
}

std::string Hex(const Bytes &b) { return Hex(b.data(), b.size()); }
template<size_t N> std::string Hex(const std::array<uint8_t, N> &a) { return Hex(a.data(), N); }

std::string Quote(const std::string &s)
{
	std::string out = "\"";
	for(char c : s)
	{
		if(c == '"' || c == '\\')
			out += '\\';
		if(c == '\n')
		{
			out += "\\n";
			continue;
		}
		out += c;
	}
	return out + "\"";
}

/** Key pair number i: d = H("psrpt vector key" || u32 i), the next one if that is no scalar. */
KeyPair FixedKey(uint32_t i)
{
	for(uint32_t round = 0;; round++)
	{
		Bytes seed = Label("psrpt vector key");
		ByteWriter w(seed);
		w.U32(i);
		w.U32(round);
		KeyPair kp;
		if(KeyPairFromPrivate(Sha256(seed), kp))
			return kp;
	}
}

struct Fixed
{
	uint32_t next_key = 1;
	uint32_t next_rnd = 1;
	std::vector<std::pair<uint32_t, KeyPair>> used;

	KeySource Keys()
	{
		return [this](KeyPair &out) {
			out = FixedKey(next_key);
			used.emplace_back(next_key, out);
			next_key++;
			return true;
		};
	}

	RandomSource Random()
	{
		return [this](uint8_t *out, size_t size) {
			Bytes seed = Label("psrpt vector random");
			ByteWriter w(seed);
			w.U32(next_rnd++);
			Bytes stream;
			for(uint8_t block = 0; stream.size() < size; block++)
			{
				Bytes s = seed;
				s.push_back(block);
				Key32 h = Sha256(s);
				stream.insert(stream.end(), h.begin(), h.end());
			}
			std::memcpy(out, stream.data(), size);
			return true;
		};
	}
};

std::string KeyJson(const KeyPair &k)
{
	return "{\"d\":\"" + Hex(k.d) + "\",\"pub\":\"" + Hex(k.pub) + "\"}";
}

} // namespace

int main()
{
	Fixed f;
	std::vector<std::string> parts;
	auto add = [&](const std::string &name, const std::string &value) { parts.push_back(Quote(name) + ":" + value); };

	// ---- primitives
	{
		Bytes salt = Label("salt"), ikm = Label("input key material");
		add("hkdf", "{\"salt\":\"" + Hex(salt) + "\",\"ikm\":\"" + Hex(ikm) + "\",\"info\":\"psrpt test\",\"out\":\"" + Hex(Hkdf(salt, ikm, "psrpt test", 80))
			+ "\",\"out_empty_salt\":\"" + Hex(Hkdf(Bytes(), ikm, "psrpt test", 32)) + "\"}");
		KeyPair a = FixedKey(1000), b = FixedKey(1001);
		Key32 dh;
		Dh(a, b.pub, dh);
		Sig sig;
		Sign(a, Label("signed text"), sig);
		uint8_t key[32], nonce[12];
		std::memset(key, 7, 32);
		std::memset(nonce, 9, 12);
		Bytes sealed;
		Seal(key, nonce, Label("ad"), Label("plain text"), sealed);
		Key32 hm = Hmac(key, 32, reinterpret_cast<const uint8_t *>("data"), 4);
		Key32 pb = Pbkdf2("passphrase", reinterpret_cast<const uint8_t *>("saltsalt"), 8, 1000);
		add("primitives", "{\"a\":" + KeyJson(a) + ",\"b\":" + KeyJson(b) + ",\"dh\":\"" + Hex(dh) + "\",\"signed\":\""
			+ Hex(Label("signed text")) + "\",\"sig\":\"" + Hex(sig) + "\",\"key\":\"" + Hex(key, 32) + "\",\"nonce\":\"" + Hex(nonce, 12)
			+ "\",\"ad\":\"" + Hex(Label("ad")) + "\",\"plain\":\"" + Hex(Label("plain text")) + "\",\"sealed\":\"" + Hex(sealed)
			+ "\",\"hmac\":\"" + Hex(hm) + "\",\"pbkdf2\":\"" + Hex(pb) + "\"}");
	}

	// ---- identities, names and codes
	Identity alice, bob;
	MakeIdentity(alice, f.Keys());
	MakeIdentity(bob, f.Keys());
	Id16 link_key{};
	f.Random()(link_key.data(), link_key.size());
	DeviceId alice_device{}, bob_device{};
	f.Random()(alice_device.data(), 8);
	f.Random()(bob_device.data(), 8);
	Id16 group_id{}, link_secret{};
	f.Random()(group_id.data(), 16);
	f.Random()(link_secret.data(), 16);
	auto identity_json = [](const Identity &id) {
		return "{\"iks\":" + KeyJson(id.iks) + ",\"ikd\":" + KeyJson(id.ikd) + ",\"ikd_sig\":\"" + Hex(id.ikd_sig) + "\",\"fp\":\"" + Hex(id.fp) + "\"}";
	};
	add("alice", identity_json(alice));
	add("bob", identity_json(bob));
	Key32 link_dh;
	Dh(FixedKey(2000), FixedKey(2001).pub, link_dh);
	Key32 lk32 = LinkKey(link_secret, link_dh);
	add("names", "{\"link_key\":\"" + Hex(link_key) + "\",\"request_box\":\"" + Hex(RequestBox(link_key)) + "\",\"alice_device\":\""
		+ Hex(alice_device) + "\",\"bob_device\":\"" + Hex(bob_device) + "\",\"alice_inbox\":\"" + Hex(InboxName(alice.fp, alice_device))
		+ "\",\"group_id\":\"" + Hex(group_id) + "\",\"group_box\":\"" + Hex(GroupBox(group_id)) + "\",\"link_secret\":\"" + Hex(link_secret)
		+ "\",\"link_box\":\"" + Hex(LinkBox(link_secret)) + "\",\"link_dh\":\"" + Hex(link_dh) + "\",\"link_key32\":\"" + Hex(lk32)
		+ "\",\"link_code\":" + Quote(LinkCode(lk32)) + ",\"safety\":" + Quote(SafetyCode(alice.fp, bob.fp)) + "}");

	// ---- the card, with a profile
	KeyPair bob_spk = FixedKey(3000), bob_opk = FixedKey(3001);
	{
		Card card;
		card.seq = 2;
		CardDevice d;
		d.id = bob_device;
		d.kind = 1;
		d.spk_id = 1;
		d.spk = bob_spk.pub;
		SignPrekey(bob, bob_device, 1, bob_spk.pub, d.spk_sig);
		card.devices.push_back(d);
		const std::string profile = "{\"name\":\"Bob\"}";
		SealProfile(link_key, bob.fp, profile, card.profile, f.Random());
		Bytes bytes;
		EncodeCard(bob, card, bytes);
		add("card", "{\"bytes\":\"" + Hex(bytes) + "\",\"fp\":\"" + Hex(bob.fp) + "\",\"profile_json\":" + Quote(profile)
			+ ",\"profile_sealed\":\"" + Hex(card.profile) + "\",\"spk\":" + KeyJson(bob_spk) + ",\"opk\":" + KeyJson(bob_opk) + "}");
	}

	// ---- X3DH and a conversation
	{
		PrekeyBundle bundle;
		bundle.fp = bob.fp;
		bundle.ikd = bob.ikd.pub;
		bundle.device = bob_device;
		bundle.spk_id = 1;
		bundle.spk = bob_spk.pub;
		bundle.opk_id = 5;
		bundle.opk = bob_opk.pub;
		X3dhStart start;
		X3dhInitiate(alice, alice_device, bundle, start, f.Keys());
		X3dhStart start_no_opk;
		PrekeyBundle no_opk = bundle;
		no_opk.opk_id = 0;
		X3dhInitiate(alice, alice_device, no_opk, start_no_opk, f.Keys());
		add("x3dh", "{\"ek\":" + KeyJson(start.ek) + ",\"sk\":\"" + Hex(start.sk) + "\",\"ad\":\"" + Hex(start.ad) + "\",\"ek_no_opk\":"
			+ KeyJson(start_no_opk.ek) + ",\"sk_no_opk\":\"" + Hex(start_no_opk.sk) + "\"}");

		// The ratchet: every new key pair comes from the fixed ones, in this order
		Ratchet a, b;
		const uint32_t first_key = f.next_key;
		a.InitInitiator(start.sk, bob_spk.pub, f.Keys());
		b.InitResponder(start.sk, bob_spk);
		struct Step
		{
			const char *from;
			std::string text;
		};
		const std::vector<Step> steps = { { "a", "hello Bob" }, { "a", "second" }, { "b", "hi Alice" }, { "b", "two from Bob" },
			{ "a", "third" }, { "b", "four" } };
		std::string list;
		std::vector<std::pair<RatchetHeader, Bytes>> sent;
		for(const Step &s : steps)
		{
			Ratchet &from = s.from[0] == 'a' ? a : b;
			Ratchet &to = s.from[0] == 'a' ? b : a;
			const Bytes &ad = start.ad;
			RatchetHeader h;
			Bytes sealed;
			from.Encrypt(ad, Label(s.text), h, sealed);
			Bytes plain;
			to.Decrypt(ad, h, sealed, plain, 1000, f.Keys());
			if(!list.empty())
				list += ",";
			list += "{\"from\":\"" + std::string(s.from) + "\",\"text\":" + Quote(s.text) + ",\"header\":\"" + Hex(h.Encode()) + "\",\"sealed\":\""
				+ Hex(sealed) + "\",\"opened\":" + (plain == Label(s.text) ? "true" : "false") + "}";
		}
		// The keys the ratchets made, in the order they made them
		std::string keys;
		for(const auto &k : f.used)
		{
			if(k.first < first_key)
				continue;
			if(!keys.empty())
				keys += ",";
			keys += KeyJson(k.second);
		}
		add("ratchet", "{\"keys\":[" + keys + "],\"messages\":[" + list + "]}");

		// A first message as an envelope
		Ratchet a2;
		a2.InitInitiator(start.sk, bob_spk.pub, [](KeyPair &out) { out = FixedKey(4000); return true; });
		Envelope e;
		e.kind = 1;
		e.from_fp = alice.fp;
		e.from_device = alice_device;
		e.to_device = bob_device;
		e.ek = start.ek.pub;
		e.spk_id = 1;
		e.opk_id = 5;
		a2.Encrypt(start.ad, Label("{\"t\":\"request\"}"), e.header, e.sealed);
		add("envelope", "{\"ratchet_key\":" + KeyJson(FixedKey(4000)) + ",\"bytes\":\"" + Hex(e.Encode()) + "\"}");
	}

	// ---- a group sender key
	{
		SenderChain own;
		own.New(1, [](KeyPair &out) { out = FixedKey(5000); return true; }, f.Random());
		const Key32 ck0 = own.ck;
		std::string list;
		for(int i = 0; i < 3; i++)
		{
			Bytes out;
			GroupSeal(own, group_id, alice.fp, alice_device, Label("group " + std::to_string(i)), out);
			if(!list.empty())
				list += ",";
			list += "{\"text\":\"group " + std::to_string(i) + "\",\"bytes\":\"" + Hex(out) + "\"}";
		}
		add("group", "{\"chain\":1,\"ck\":\"" + Hex(ck0) + "\",\"sign\":" + KeyJson(FixedKey(5000)) + ",\"messages\":[" + list + "]}");
	}

	// ---- status, backup, signed requests
	{
		Key32 epoch_key;
		f.Random()(epoch_key.data(), 32);
		const std::string json = "{\"state\":\"online\",\"activity\":\"Playing\"}";
		Bytes note;
		SealStatus(epoch_key, 3, 4, alice.fp, json, note, f.Random());
		add("status", "{\"epoch_key\":\"" + Hex(epoch_key) + "\",\"epoch\":3,\"day\":4,\"day_key\":\"" + Hex(FriendsKeyForDay(epoch_key, 4)) + "\",\"json\":"
			+ Quote(json) + ",\"note\":\"" + Hex(note) + "\"}");
		// Where it is kept on day 20420 (2025-11-28) and the day after
		add("status_box", "{\"epoch_key\":\"" + Hex(epoch_key) + "\",\"days\":{\"20420\":\"" + Hex(StatusBox(epoch_key, 20420))
			+ "\",\"20421\":\"" + Hex(StatusBox(epoch_key, 20421)) + "\"}}");
		std::string backup;
		SealBackup("pass phrase", "{\"backup\":true}", backup, 100000, f.Random());
		add("backup", "{\"passphrase\":\"pass phrase\",\"json\":" + Quote("{\"backup\":true}") + ",\"text\":" + Quote(backup) + "}");
		const Bytes m = RequestMessage("status_put", "1795000000", { { "note", "abc" }, { "fp", "def" } });
		add("request", "{\"op\":\"status_put\",\"t\":\"1795000000\",\"fields\":{\"note\":\"abc\",\"fp\":\"def\"},\"message\":" + Quote(std::string(m.begin(), m.end())) + "}");
	}

	std::printf("{\n");
	for(size_t i = 0; i < parts.size(); i++)
		std::printf("  %s%s\n", parts[i].c_str(), i + 1 < parts.size() ? "," : "");
	std::printf("}\n");
	return 0;
}
