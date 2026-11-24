// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Writes with coplay's own code (coplay/src/protocol.cpp) the bytes of the chat's read marks,
// reactions and deleted messages (ChatMark, ChatReactUp), chat messages with replies
// (WriteChatMessage), what ReadChatMark refuses, and which texts ValidReaction takes, as JSON. The browser page has the same in JavaScript (web/js/proto.js);
// test/web/chat.test.mjs expects the same bytes and answers. Made again after a change to either
// side, with coplay built (build/coplay/libcoplay.a):
//
//   c++ -std=c++17 -Icoplay/include test/web/chat-vectors.cpp build/coplay/libcoplay.a -o /tmp/chat-vectors
//   /tmp/chat-vectors > test/web/chat-vectors.json
//
// (On macOS with the Command Line Tools, add the build's own flags if <array> is not found:
// -nostdinc++ -isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1.)
// Not part of the build: the JSON it writes is in the repository.

#include <coplay/protocol.h>
#include <coplay/bytes.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace coplay;

static std::string Hex(const std::vector<uint8_t> &b)
{
	std::string out;
	char buf[3];
	for(uint8_t c : b)
	{
		std::snprintf(buf, sizeof(buf), "%02x", c);
		out += buf;
	}
	return out;
}

static std::string Json(const std::string &s)
{
	std::string out = "\"";
	for(unsigned char c : s)
	{
		if(c == '"' || c == '\\')
		{
			out += '\\';
			out += static_cast<char>(c);
		}
		else if(c < 0x20)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			out += buf;
		}
		else
			out += static_cast<char>(c);
	}
	return out + "\"";
}

int main()
{
	std::printf("{\n  \"marks\": [\n");
	struct M { uint32_t mark; ChatMarkKind kind; uint8_t slot; uint32_t seq; bool on; std::string emoji; };
	const std::vector<M> marks = {
		{ 1, ChatMarkKind::Read, 0, 1, true, "" },
		{ 0x01020304, ChatMarkKind::Read, 8, 0xa0b0c0d0, true, "" },
		{ 77, ChatMarkKind::Read, 3, 0, true, "" },
		{ 2, ChatMarkKind::Reaction, 2, 7, true, "🔥" },
		{ 3, ChatMarkKind::Reaction, 1, 9, false, "👍🏽" },
		{ 4294967295u, ChatMarkKind::Reaction, 0, 4294967295u, true, "1️⃣" },
		{ 5, ChatMarkKind::Reaction, 4, 12, true, "❤️‍🔥" },
		{ 6, ChatMarkKind::Deleted, 2, 31, true, "" },
		{ 0x0a0b0c0d, ChatMarkKind::Deleted, 0, 0x01020304, true, "" },
	};
	for(size_t i = 0; i < marks.size(); i++)
	{
		const M &x = marks[i];
		ChatMark m;
		m.mark = x.mark;
		m.kind = x.kind;
		m.slot = x.slot;
		m.seq = x.seq;
		m.on = x.on;
		m.emoji = x.emoji;
		std::vector<uint8_t> payload;
		ByteWriter w(payload);
		WriteChatMark(w, m);
		ByteReader r(payload.data(), payload.size());
		ChatMark back;
		const bool ok = ReadChatMark(r, back);
		std::printf("    {\"mark\": %u, \"kind\": %u, \"slot\": %u, \"seq\": %u, \"on\": %s, \"emoji\": %s, \"hex\": \"%s\", \"reads\": %s}%s\n",
			x.mark, static_cast<unsigned>(x.kind), x.slot, x.seq, x.on ? "true" : "false", Json(x.emoji).c_str(), Hex(payload).c_str(),
			ok ? "true" : "false", i + 1 < marks.size() ? "," : "");
	}
	std::printf("  ],\n  \"reactions\": [\n");
	struct R { uint32_t seq; bool on; std::string emoji; };
	const std::vector<R> reactions = { { 7, true, "👍" }, { 0x01020304, false, "🔥" }, { 1, true, "#️⃣" } };
	for(size_t i = 0; i < reactions.size(); i++)
	{
		ChatReaction cr;
		cr.seq = reactions[i].seq;
		cr.on = reactions[i].on;
		cr.emoji = reactions[i].emoji;
		std::vector<uint8_t> payload;
		ByteWriter w(payload);
		WriteChatReaction(w, cr);
		std::printf("    {\"seq\": %u, \"on\": %s, \"emoji\": %s, \"hex\": \"%s\"}%s\n", cr.seq, cr.on ? "true" : "false", Json(cr.emoji).c_str(),
			Hex(payload).c_str(), i + 1 < reactions.size() ? "," : "");
	}
	// Chat messages (Chat and ChatUp alike) with and without a reply, as WriteChatMessage writes them
	std::printf("  ],\n  \"messages\": [\n");
	struct C { uint32_t seq; uint32_t up_id; uint8_t slot; uint8_t flags; uint64_t time_ms; std::string name; std::string text; uint32_t reply; bool embed; };
	const std::vector<C> chats = {
		{ 5, 0, 0, 0, 1700000000000ull, "Host", "hi", 0, false },
		{ 6, 3, 2, 1, 1700000000123ull, "Ana", "same here 🔥", 5, false },
		{ 7, 4, 1, 0, 1700000000456ull, "Bo", "look https://example.com/", 6, true },
		{ 0x01020304, 0, 0, 0, 0, "", "x", 0x0a0b0c0d, false },
	};
	for(size_t i = 0; i < chats.size(); i++)
	{
		const C &x = chats[i];
		ChatMessage m;
		m.seq = x.seq;
		m.up_id = x.up_id;
		m.slot = x.slot;
		m.flags = x.flags;
		m.time_ms = x.time_ms;
		m.name = x.name;
		m.text = x.text;
		m.reply = x.reply;
		if(x.embed)
		{
			m.embed.url = "https://example.com/";
			m.embed.site = "example.com";
			m.embed.title = "Example";
			m.embed.description = "A page";
		}
		std::vector<uint8_t> payload;
		ByteWriter w(payload);
		WriteChatMessage(w, m);
		std::printf("    {\"seq\": %u, \"up_id\": %u, \"slot\": %u, \"flags\": %u, \"time_ms\": %llu, \"name\": %s, \"text\": %s, \"reply\": %u, \"embed\": %s, \"hex\": \"%s\"}%s\n",
			x.seq, x.up_id, x.slot, x.flags, static_cast<unsigned long long>(x.time_ms), Json(x.name).c_str(), Json(x.text).c_str(), x.reply,
			x.embed ? "true" : "false", Hex(payload).c_str(), i + 1 < chats.size() ? "," : "");
	}
	// Malformed marks: what ReadChatMark refuses
	std::printf("  ],\n  \"refused\": [\n");
	const std::vector<std::string> bad = {
		"00000001010900000001",            // slot 9
		"00000001040000000001",            // kind 4
		"00000001030200000000",            // deleted, message 0
		"0000000102000000000001" "04f09f94a5", // reaction on message 0
		"000000010200000000070101" "61",     // "a"
		"000000010200000000070104f09f94", // cut short
		"000000010100000000",              // read mark cut short
	};
	for(size_t i = 0; i < bad.size(); i++)
	{
		std::vector<uint8_t> payload;
		for(size_t k = 0; k + 1 < bad[i].size(); k += 2)
			payload.push_back(static_cast<uint8_t>(std::stoi(bad[i].substr(k, 2), nullptr, 16)));
		ByteReader r(payload.data(), payload.size());
		ChatMark m;
		const bool ok = ReadChatMark(r, m);
		std::printf("    {\"hex\": \"%s\", \"reads\": %s}%s\n", bad[i].c_str(), ok ? "true" : "false", i + 1 < bad.size() ? "," : "");
	}
	// ValidReaction
	std::printf("  ],\n  \"valid\": [\n");
	const std::vector<std::string> texts = { "👍", "❤️", "👍🏽", "1️⃣", "", "a", "1", "ok", " 👍", "<img>", std::string("\xff\xfe"),
		"👍👍👍👍👍", "👍👍👍👍", "é", "日本", "#", "#️⃣", "*a", "©", "\xe2\x80", "🔥\n", "\xef\xbf\xbd", "1👍", "a👍",
		"❤️‍🔥", "🇩🇪", "*⃣", "‼️", "👍1", "ок", "\xe2\x83\xa3", "\xef\xb8\x8f", "\xe2\x80\x8d", "👍\xe2\x80\x8b", "👍\xe2\x80\xae",
		std::string("\xc1\x81"), std::string("\xf0\x80\x80\xb1\xe2\x83\xa3"), "1\xe2\x83\xa3x", "🫶", "⬆️" };
	for(size_t i = 0; i < texts.size(); i++)
		std::printf("    {\"hex\": \"%s\", \"valid\": %s}%s\n", Hex(std::vector<uint8_t>(texts[i].begin(), texts[i].end())).c_str(),
			ValidReaction(texts[i]) ? "true" : "false", i + 1 < texts.size() ? "," : "");
	std::printf("  ]\n}\n");
	return 0;
}
