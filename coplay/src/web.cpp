// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/web.h>

#include <coplay/bytes.h>
#include <coplay/invite.h>
#include <coplay/meet.h>
#include <coplay/protocol.h>

#include <openssl/evp.h>

#include <cstdio>
#include <cstring>

namespace coplay {

const char *const kDefaultWebUrl = "https://crypto90.github.io/PSRemotePlayTogether/";

static const uint8_t kWebNoteMagic = 0x57; // 'W'
static const uint8_t kWebNoteVersion = 1;
static const uint8_t kWebNoteOffer = 1;
static const uint8_t kWebNoteAnswers = 2;
static const size_t kWebNonceSize = 12;
static const size_t kWebTagSize = 16;
/** ICE allows 4 to 256 characters for the name and 22 to 256 for the password; nobody uses that many. */
static const size_t kMaxIceText = 64;
static const size_t kMaxMid = 16;

Key DeriveWebNoteKey(const Secret &secret)
{
	return HkdfSha256(secret.data(), secret.size(), nullptr, 0, "psrpt coplay web note");
}

// ------------------------------------------------------------------ sealing

static bool SealNote(const Secret &secret, const std::vector<uint8_t> &body, std::string &text)
{
	Key key = DeriveWebNoteKey(secret);
	std::vector<uint8_t> out(kWebNonceSize + body.size() + kWebTagSize);
	if(!RandomBytes(out.data(), kWebNonceSize))
		return false;
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	do
	{
		if(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kWebNonceSize), nullptr) != 1)
			break;
		if(EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), out.data()) != 1)
			break;
		if(!body.empty()
			&& EVP_EncryptUpdate(ctx, out.data() + kWebNonceSize, &len, body.data(), static_cast<int>(body.size())) != 1)
			break;
		if(EVP_EncryptFinal_ex(ctx, out.data() + kWebNonceSize + body.size(), &len) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kWebTagSize),
				out.data() + kWebNonceSize + body.size()) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		return false;
	text = Base64UrlEncode(out.data(), out.size());
	return text.size() <= kMeetMaxNoteText;
}

static bool OpenNote(const Secret &secret, const std::string &text, std::vector<uint8_t> &body)
{
	if(text.empty() || text.size() > kMeetMaxNoteText)
		return false;
	std::vector<uint8_t> raw;
	if(!Base64UrlDecode(text, raw) || raw.size() < kWebNonceSize + kWebTagSize + 3)
		return false;
	Key key = DeriveWebNoteKey(secret);
	size_t size = raw.size() - kWebNonceSize - kWebTagSize;
	body.resize(size);
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return false;
	bool ok = false;
	int len = 0;
	uint8_t final_buf[16];
	do
	{
		if(EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kWebNonceSize), nullptr) != 1)
			break;
		if(EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), raw.data()) != 1)
			break;
		if(EVP_DecryptUpdate(ctx, body.data(), &len, raw.data() + kWebNonceSize, static_cast<int>(size)) != 1)
			break;
		if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kWebTagSize),
				raw.data() + kWebNonceSize + size) != 1)
			break;
		if(EVP_DecryptFinal_ex(ctx, final_buf, &len) != 1)
			break;
		ok = true;
	} while(false);
	EVP_CIPHER_CTX_free(ctx);
	if(!ok)
		body.clear();
	return ok;
}

// ------------------------------------------------------------------ what a note says

static bool IceTextOk(const std::string &s, size_t min)
{
	if(s.size() < min || s.size() > kMaxIceText)
		return false;
	// What ICE allows ("ice-char"): letters, digits, "+" and "/"
	for(char c : s)
	{
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
		if(!ok)
			return false;
	}
	return true;
}

static void WriteSide(ByteWriter &w, const WebPeerDescription &side)
{
	w.Str8(side.ufrag);
	w.Str8(side.pwd);
	std::vector<uint8_t> entries;
	ByteWriter e(entries);
	uint8_t count = 0;
	for(const WebCandidate &c : side.candidates)
	{
		if(count == kWebMaxCandidates)
			break;
		uint8_t bytes[16];
		size_t n = c.address.Bytes(bytes);
		if(!n || !c.address.Port())
			continue;
		e.U8(static_cast<uint8_t>(c.type));
		e.U8(static_cast<uint8_t>(n));
		e.Bytes(bytes, n);
		e.U16(c.address.Port());
		count++;
	}
	w.U8(count);
	w.Bytes(entries.data(), entries.size());
}

static bool ReadSide(ByteReader &r, WebPeerDescription &side)
{
	side.ufrag = r.Str8();
	side.pwd = r.Str8();
	uint8_t count = r.U8();
	if(!r.ok() || count > kWebMaxCandidates || !IceTextOk(side.ufrag, 4) || !IceTextOk(side.pwd, 22))
		return false;
	side.candidates.clear();
	for(uint8_t i = 0; i < count; i++)
	{
		uint8_t type = r.U8();
		uint8_t size = r.U8();
		if(!r.ok() || (size != 4 && size != 16))
			return false;
		uint8_t bytes[16];
		if(!r.Bytes(bytes, size))
			return false;
		uint16_t port = r.U16();
		if(!r.ok())
			return false;
		// A kind this version does not know is left out, the rest still counts
		if(!port || (type != static_cast<uint8_t>(WebCandidateType::Host)
				&& type != static_cast<uint8_t>(WebCandidateType::Reflexive)))
			continue;
		WebCandidate c;
		c.type = static_cast<WebCandidateType>(type);
		c.address = Address::FromBytes(bytes, size, port);
		if(c.address.IsValid())
			side.candidates.push_back(c);
	}
	return true;
}

bool SealWebOffer(const Secret &secret, const WebOffer &offer, std::string &text)
{
	std::vector<uint8_t> body;
	ByteWriter w(body);
	w.U8(kWebNoteMagic);
	w.U8(kWebNoteVersion);
	w.U8(kWebNoteOffer);
	w.U8(offer.protocol);
	w.U64(offer.peer_id);
	w.Bytes(offer.fingerprint.data(), offer.fingerprint.size());
	w.Str8(offer.mid);
	WriteSide(w, offer.side);
	return SealNote(secret, body, text);
}

bool OpenWebOffer(const Secret &secret, const std::string &text, WebOffer &offer)
{
	std::vector<uint8_t> body;
	if(!OpenNote(secret, text, body))
		return false;
	ByteReader r(body.data(), body.size());
	if(r.U8() != kWebNoteMagic || r.U8() != kWebNoteVersion || r.U8() != kWebNoteOffer)
		return false;
	WebOffer out;
	out.protocol = r.U8();
	out.peer_id = r.U64();
	r.Bytes(out.fingerprint.data(), out.fingerprint.size());
	out.mid = r.Str8();
	if(!r.ok() || !out.peer_id || out.mid.empty() || out.mid.size() > kMaxMid)
		return false;
	for(char c : out.mid)
	{
		// Goes into a session description as it is
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		if(!ok)
			return false;
	}
	if(!ReadSide(r, out.side))
		return false;
	offer = std::move(out);
	return true;
}

bool SealWebAnswers(const Secret &secret, const WebAnswers &answers, std::string &text)
{
	std::vector<uint8_t> body;
	ByteWriter w(body);
	w.U8(kWebNoteMagic);
	w.U8(kWebNoteVersion);
	w.U8(kWebNoteAnswers);
	w.U8(answers.protocol);
	w.Bytes(answers.fingerprint.data(), answers.fingerprint.size());
	size_t count = answers.answers.size() > kWebMaxPending ? kWebMaxPending : answers.answers.size();
	w.U8(static_cast<uint8_t>(count));
	for(size_t i = 0; i < count; i++)
	{
		const WebAnswer &a = answers.answers[i];
		w.U64(a.peer_id);
		w.U8(a.active ? 1 : 0);
		WriteSide(w, a.side);
	}
	return SealNote(secret, body, text);
}

bool OpenWebAnswers(const Secret &secret, const std::string &text, WebAnswers &answers)
{
	std::vector<uint8_t> body;
	if(!OpenNote(secret, text, body))
		return false;
	ByteReader r(body.data(), body.size());
	if(r.U8() != kWebNoteMagic || r.U8() != kWebNoteVersion || r.U8() != kWebNoteAnswers)
		return false;
	WebAnswers out;
	out.protocol = r.U8();
	r.Bytes(out.fingerprint.data(), out.fingerprint.size());
	uint8_t count = r.U8();
	if(!r.ok() || count > kWebMaxPending)
		return false;
	for(uint8_t i = 0; i < count; i++)
	{
		WebAnswer a;
		a.peer_id = r.U64();
		a.active = (r.U8() & 1) != 0;
		if(!r.ok() || !ReadSide(r, a.side))
			return false;
		out.answers.push_back(std::move(a));
	}
	answers = std::move(out);
	return true;
}

// ------------------------------------------------------------------ session descriptions

std::string WebFingerprintToString(const std::array<uint8_t, kWebFingerprintSize> &fingerprint)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(fingerprint.size() * 3);
	for(size_t i = 0; i < fingerprint.size(); i++)
	{
		if(i)
			out.push_back(':');
		out.push_back(hex[fingerprint[i] >> 4]);
		out.push_back(hex[fingerprint[i] & 15]);
	}
	return out;
}

bool WebFingerprintFromString(const std::string &text, std::array<uint8_t, kWebFingerprintSize> &fingerprint)
{
	auto nibble = [](char c) -> int {
		if(c >= '0' && c <= '9')
			return c - '0';
		if(c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if(c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};
	size_t count = 0;
	size_t i = 0;
	while(i < text.size())
	{
		if(text[i] == ':' || text[i] == ' ')
		{
			i++;
			continue;
		}
		if(i + 1 >= text.size() || count == fingerprint.size())
			return false;
		int hi = nibble(text[i]);
		int lo = nibble(text[i + 1]);
		if(hi < 0 || lo < 0)
			return false;
		fingerprint[count++] = static_cast<uint8_t>((hi << 4) | lo);
		i += 2;
	}
	return count == fingerprint.size();
}

static std::string CandidateLine(const WebCandidate &c, size_t index)
{
	uint8_t bytes[16];
	size_t n = c.address.Bytes(bytes);
	std::string ip = AddressBytesToString(bytes, n);
	if(ip.empty())
		return std::string();
	bool reflexive = c.type == WebCandidateType::Reflexive;
	// Type preference 126 for the machine's own addresses, 100 for the one a STUN server saw
	uint32_t priority = (reflexive ? 100u : 126u) * 16777216u + (65535u - static_cast<uint32_t>(index)) * 256u + 255u;
	std::string line = "a=candidate:" + std::to_string(index + 1) + " 1 udp " + std::to_string(priority) + " " + ip + " "
		+ std::to_string(c.address.Port()) + " typ " + (reflexive ? "srflx" : "host");
	if(reflexive)
		line += n == 16 ? " raddr :: rport 0" : " raddr 0.0.0.0 rport 0";
	return line + "\r\n";
}

std::string WebOfferToSdp(const WebOffer &offer)
{
	char session[24];
	std::snprintf(session, sizeof(session), "%llu", static_cast<unsigned long long>(offer.peer_id >> 1));
	std::string sdp;
	sdp += "v=0\r\n";
	sdp += std::string("o=- ") + session + " 2 IN IP4 127.0.0.1\r\n";
	sdp += "s=-\r\n";
	sdp += "t=0 0\r\n";
	sdp += "a=group:BUNDLE " + offer.mid + "\r\n";
	sdp += "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n";
	sdp += "c=IN IP4 0.0.0.0\r\n";
	sdp += "a=mid:" + offer.mid + "\r\n";
	sdp += "a=ice-ufrag:" + offer.side.ufrag + "\r\n";
	sdp += "a=ice-pwd:" + offer.side.pwd + "\r\n";
	sdp += "a=ice-options:trickle\r\n";
	sdp += "a=fingerprint:sha-256 " + WebFingerprintToString(offer.fingerprint) + "\r\n";
	sdp += "a=setup:actpass\r\n";
	sdp += "a=sctp-port:5000\r\n";
	sdp += "a=max-message-size:262144\r\n";
	for(size_t i = 0; i < offer.side.candidates.size(); i++)
		sdp += CandidateLine(offer.side.candidates[i], i);
	return sdp;
}

// ------------------------------------------------------------------ the link

static std::string TrimText(const std::string &s)
{
	size_t a = 0;
	size_t b = s.size();
	auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while(a < b && is_space(s[a]))
		a++;
	while(b > a && is_space(s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

std::string WebUrlFromText(const std::string &text, const std::string &default_url)
{
	std::string s = TrimText(text);
	if(s.empty())
		return default_url;
	if(s.size() > 300)
		return std::string();
	for(char c : s)
	{
		// "#" starts the part the key goes into, "?" would end up in a server's log
		if(static_cast<unsigned char>(c) <= ' ' || c == '"' || c == '<' || c == '>' || c == '\\' || c == '#' || c == '?')
			return std::string();
	}
	if(s.compare(0, 8, "https://") == 0 || s.compare(0, 7, "http://") == 0)
		return s;
	if(s.find("://") != std::string::npos)
		return std::string();
	return "https://" + s;
}

std::string WebInviteLink(const std::string &page_url, const Secret &secret, const std::string &meet)
{
	if(page_url.empty())
		return std::string();
	std::string link = page_url + "#" + SecretToString(secret);
	if(!meet.empty())
		link += "?meet=" + meet;
	return link;
}

} // namespace coplay
