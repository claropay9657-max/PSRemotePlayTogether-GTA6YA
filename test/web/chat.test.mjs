// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The chat's read marks and reactions as the browser page has them: the packets byte for byte
// as coplay writes them (chat-vectors.json, made by chat-vectors.cpp with coplay's own code),
// which reactions count as one emoji, how marks are applied and in which order, a guest's
// session (acknowledging, resending, its own reactions), and where the reactions and the
// readers' pictures go in both chats (web/js/chatmarks.js).
// Run: node --experimental-default-type=module --test test/web/

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.join(here, '..', '..', 'web', 'js');
const P = await import(path.join(web, 'proto.js'));
const M = await import(path.join(web, 'chatmarks.js'));
const { Session } = await import(path.join(web, 'session.js'));
const V = JSON.parse(fs.readFileSync(path.join(here, 'chat-vectors.json'), 'utf8'));

const hex = (b) => Buffer.from(b).toString('hex');
const unhex = (h) => new Uint8Array(Buffer.from(h, 'hex'));
const u32 = (v) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const utf8 = (s) => [...new TextEncoder().encode(s)];

// ------------------------------------------------------------------ the packets

test('the packet types are the app\'s', () => {
  assert.equal(P.PT.ChatSeen, 0x19);
  assert.equal(P.PT.ChatReactUp, 0x1a);
  assert.equal(P.PT.ChatMarkAck, 0x1b);
  assert.equal(P.PT.ChatMark, 0x2a);
  assert.equal(P.MAX_REACTION_SIZE, 16);
  assert.equal(P.MAX_REACTIONS_PER_PLAYER, 3);
  assert.equal(P.MAX_REACTION_KINDS, 12);
});

test('read, read up to and acknowledged: a type and a u32, big-endian', () => {
  assert.deepEqual([...P.chatSeenFrame(0x01020304)], [0x19, 1, 2, 3, 4]);
  assert.deepEqual([...P.chatMarkAckFrame(4294967295)], [0x1b, 255, 255, 255, 255]);
  assert.deepEqual([...P.chatSeenFrame(7)], [0x19, ...u32(7)]);
});

test('a reaction going up is the bytes coplay writes (WriteChatReaction)', () => {
  for (const v of V.reactions) {
    const frame = P.chatReactUpFrame(v.seq, v.emoji, v.on);
    assert.equal(frame[0], P.PT.ChatReactUp);
    assert.equal(hex(frame.subarray(1)), v.hex, v.emoji);
  }
  // By hand: u32 seq, u8 on, str8 emoji
  assert.deepEqual([...P.chatReactUpFrame(7, '👍', true)], [0x1a, ...u32(7), 1, 4, ...utf8('👍')]);
});

test('a mark coming down reads as coplay wrote it (WriteChatMark / ReadChatMark)', () => {
  for (const v of V.marks) {
    const m = P.readChatMark(unhex(v.hex));
    assert.ok(m, v.hex);
    assert.equal(m.mark, v.mark);
    assert.equal(m.kind, v.kind);
    assert.equal(m.slot, v.slot);
    assert.equal(m.seq, v.seq);
    if (v.kind === P.MARK_REACTION) {
      assert.equal(m.on, v.on);
      assert.equal(m.emoji, v.emoji);
    }
  }
  // By hand: u32 mark, u8 kind, u8 slot, u32 seq [, u8 on, str8 emoji]
  const fire = P.readChatMark(new Uint8Array([...u32(2), 2, 2, ...u32(7), 0, 4, ...utf8('🔥')]));
  assert.deepEqual(fire, { mark: 2, kind: 2, slot: 2, seq: 7, on: false, emoji: '🔥' });
  const left = P.readChatMark(new Uint8Array([...u32(9), 1, 3, ...u32(0)]));
  assert.equal(left.seq, 0);
});

test('what coplay refuses to read, the page refuses too', () => {
  for (const v of V.refused) assert.equal(P.readChatMark(unhex(v.hex)) !== null, v.reads, v.hex);
});

test('one emoji: the same answers as coplay\'s ValidReaction', () => {
  for (const v of V.valid) assert.equal(P.validReaction(unhex(v.hex)), v.valid, v.hex);
  // As text too, as the C++ test chat_reactions_are_emoji has it
  for (const ok of ['👍', '❤️', '👍🏽', '1️⃣']) assert.ok(P.validReaction(ok), ok);
  for (const bad of ['', 'a', '1', 'ok', ' 👍', '<img>', '👍👍👍👍👍', '#']) assert.ok(!P.validReaction(bad), bad);
  // Letters of other scripts, digits beside an emoji, invisible marks
  for (const bad of ['é', '日本', 'ок', '1👍', '👍1', '\u20e3', '\ufe0f', '\u200d', '👍\u200b', '👍\u202e']) {
    assert.ok(!P.validReaction(bad), bad);
  }
  for (const ok of ['❤️‍🔥', '🇩🇪', '#️⃣', '*⃣', '‼️', '🫶']) assert.ok(P.validReaction(ok), ok);
  assert.ok(!P.validReaction(null));
  assert.ok(!P.validReaction(42));
});

// ------------------------------------------------------------------ applying marks

test('marks applied as ApplyChatMark does (the C++ test, and read marks)', () => {
  const marks = P.newChatMarks();
  const m = { kind: P.MARK_REACTION, slot: 2, seq: 7, on: true, emoji: '🔥' };
  assert.ok(P.applyChatMark(marks, m));
  assert.ok(!P.applyChatMark(marks, m));
  assert.deepEqual(marks.reactions.get(7).get('🔥'), [2]);
  assert.ok(P.applyChatMark(marks, { ...m, on: false }));
  assert.equal(marks.reactions.size, 0);
  // Taking back what was not given changes nothing, and leaves nothing behind
  assert.ok(!P.applyChatMark(marks, { ...m, on: false }));
  assert.equal(marks.reactions.size, 0);

  assert.ok(P.applyChatMark(marks, { kind: P.MARK_READ, slot: 1, seq: 4 }));
  assert.ok(!P.applyChatMark(marks, { kind: P.MARK_READ, slot: 1, seq: 4 }));
  assert.ok(P.applyChatMark(marks, { kind: P.MARK_READ, slot: 1, seq: 3 })); // the host's word counts, even backwards
  assert.equal(marks.read.get(1), 3);
  // 0: the player left
  assert.ok(P.applyChatMark(marks, { kind: P.MARK_READ, slot: 1, seq: 0 }));
  assert.ok(!marks.read.has(1));
  assert.ok(!P.applyChatMark(marks, { kind: P.MARK_READ, slot: 1, seq: 0 }));
});

test('what a mark is about, as the app\'s guest keys it', () => {
  assert.equal(P.chatMarkKey({ kind: P.MARK_READ, slot: 3, seq: 9 }), 'r3');
  assert.equal(P.chatMarkKey({ kind: P.MARK_REACTION, slot: 3, seq: 9, emoji: '👍' }), 'x3:9:👍');
  assert.equal(P.chatMarkKey({ kind: P.MARK_DELETED, slot: 3, seq: 9 }), 'd9');
});

// ------------------------------------------------------------------ replies and deleting

test('chat messages with and without a reply read as coplay wrote them (WriteChatMessage / ReadChatMessage)', () => {
  assert.equal(P.PT.ChatDeleteUp, 0x1c);
  assert.equal(P.MARK_DELETED, 3);
  for (const v of V.messages) {
    const m = P.readChat(unhex(v.hex));
    assert.ok(m, v.hex);
    assert.equal(m.seq, v.seq);
    assert.equal(m.upId, v.up_id);
    assert.equal(m.slot, v.slot);
    assert.equal(m.timeMs, v.time_ms);
    assert.equal(m.name, v.name);
    assert.equal(m.text, v.text);
    assert.equal(m.reply, v.reply);
    assert.equal(!!m.embed, v.embed);
    // Written by an older version: no reply at the end, read as none
    const older = unhex(v.hex).subarray(0, unhex(v.hex).length - 4);
    const old = P.readChat(older);
    assert.ok(old);
    assert.equal(old.reply, 0);
    assert.equal(old.text, v.text);
  }
});

test('what a guest writes is the bytes coplay\'s guest writes (ChatUp, the reply last)', () => {
  // The guest's ChatUp is a ChatMessage with only its number (in seq), the text and the reply
  const v = V.messages.find((x) => !x.up_id && !x.slot && !x.time_ms && !x.name && !x.embed && x.reply);
  assert.ok(v);
  const frame = P.chatUpFrame(v.seq, v.text, v.reply);
  assert.equal(frame[0], P.PT.ChatUp);
  assert.equal(hex(frame.subarray(1)), v.hex);
  // Without a reply: 0 at the end
  assert.deepEqual([...P.chatUpFrame(7, 'hi').subarray(-4)], [0, 0, 0, 0]);
  assert.deepEqual([...P.chatDeleteUpFrame(0x01020304)], [0x1c, 1, 2, 3, 4]);
});

test('a deleted message as ApplyChatMark has it: gone with its reactions, once', () => {
  const marks = P.newChatMarks();
  P.applyChatMark(marks, { kind: P.MARK_REACTION, slot: 1, seq: 31, on: true, emoji: '🔥' });
  const del = P.readChatMark(unhex(V.marks.find((x) => x.kind === 3 && x.seq === 31).hex));
  assert.deepEqual(del, { mark: 6, kind: 3, slot: 2, seq: 31, on: true, emoji: '' });
  assert.ok(P.applyChatMark(marks, del));
  assert.ok(marks.deleted.has(31));
  assert.ok(!marks.reactions.has(31));
  assert.ok(!P.applyChatMark(marks, del));
  // Deleted without reactions: still a change the first time
  assert.ok(P.applyChatMark(marks, { kind: P.MARK_DELETED, slot: 0, seq: 32 }));
  // seq 0 is no message
  assert.equal(P.readChatMark(new Uint8Array([...u32(1), 3, 2, ...u32(0)])), null);
});

test('a guest\'s delete goes again until the host\'s Deleted mark, or 20 seconds', () => {
  const { s, sent, marksSeen } = guest(2);
  s.deleteChat(9);
  assert.deepEqual([...sent[0]], [0x1c, ...u32(9)]);
  const t0 = s.deletesOut[0].firstMs;
  s.resendMarks(t0 + 399);
  assert.equal(sent.length, 1);
  s.resendMarks(t0 + 401);
  assert.equal(sent.length, 2);
  assert.deepEqual([...sent[1]], [0x1c, ...u32(9)]);
  // Asked again: one wish, not two
  s.deleteChat(9);
  assert.equal(s.deletesOut.length, 1);
  // The host's mark (another player's number on it changes nothing about whose wish it was)
  s.onMessage(markPacket(4, P.MARK_DELETED, 2, 9));
  assert.equal(s.deletesOut.length, 0);
  assert.ok(s.chatMarks.deleted.has(9));
  assert.equal(marksSeen.length, 1);
  // Deleted already: not asked again
  const n = sent.length;
  s.deleteChat(9);
  assert.equal(sent.length, n);
  // Again (the acknowledgement was lost): acknowledged, nothing changes
  s.onMessage(markPacket(4, P.MARK_DELETED, 2, 9));
  assert.equal(marksSeen.length, 1);
  // A host that never answers: given up after 20 seconds
  s.deleteChat(10);
  const t1 = s.deletesOut[0].firstMs;
  s.resendMarks(t1 + 20001);
  assert.equal(s.deletesOut.length, 0);
  const m = sent.length;
  s.resendMarks(t1 + 21000);
  assert.equal(sent.length, m);
  // No message, or not connected: nothing
  s.deleteChat(0);
  s.connected = false;
  s.deleteChat(11);
  assert.equal(sent.length, m);
});

test('a guest\'s reply goes with the message, and again with it', () => {
  const { s, sent } = guest(2);
  const id = s.sendChat('same here', 6);
  assert.ok(id);
  const m = P.readChat(sent[0].subarray(1));
  assert.equal(m.seq, id);
  assert.equal(m.text, 'same here');
  assert.equal(m.reply, 6);
  // What is sent again until the host has it is the same, the reply too
  assert.equal(P.readChat(s.chatOut.get(id).payload.subarray(1)).reply, 6);
  // Without one: 0
  const plain = s.sendChat('hi');
  assert.equal(P.readChat(s.chatOut.get(plain).payload.subarray(1)).reply, 0);
});

// ------------------------------------------------------------------ a guest's session

function guest(slot = 2) {
  const sent = [];
  const marksSeen = [];
  const s = new Session({ secret: new Uint8Array(16), meetUrl: '', name: 'Web', playerId: new Uint8Array(8) },
    { onChatMarks: (marks) => marksSeen.push(marks) });
  s.connected = true;
  s.welcome = { slot };
  s.dc = { readyState: 'open', send: (b) => sent.push(Uint8Array.from(b)) };
  return { s, sent, marksSeen };
}

function markPacket(mark, kind, slot, seq, on = true, emoji = '') {
  const body = [...u32(mark), kind, slot, ...u32(seq)];
  // A Deleted mark has nothing after the message's number
  if (kind === P.MARK_REACTION) body.push(on ? 1 : 0, utf8(emoji).length, ...utf8(emoji));
  return new Uint8Array([P.PT.ChatMark, ...body]);
}

test('a guest acknowledges every mark, applies it once and only if newer', () => {
  const { s, sent, marksSeen } = guest();
  s.onMessage(markPacket(5, P.MARK_REACTION, 1, 7, true, '🔥'));
  assert.deepEqual([...sent[0]], [0x1b, ...u32(5)]);
  assert.deepEqual(s.chatMarks.reactions.get(7).get('🔥'), [1]);
  assert.equal(marksSeen.length, 1);
  // Again (the acknowledgement was lost): acknowledged again, nothing changes
  s.onMessage(markPacket(5, P.MARK_REACTION, 1, 7, true, '🔥'));
  assert.deepEqual([...sent[1]], [0x1b, ...u32(5)]);
  assert.equal(marksSeen.length, 1);
  // Taken back (mark 8), then the older "given" (mark 6) arrives late: it stays taken back
  s.onMessage(markPacket(8, P.MARK_REACTION, 1, 7, false, '🔥'));
  assert.equal(s.chatMarks.reactions.size, 0);
  s.onMessage(markPacket(6, P.MARK_REACTION, 1, 7, true, '🔥'));
  assert.equal(s.chatMarks.reactions.size, 0);
  assert.equal(sent.length, 4);
  // Read marks: per slot, the newest mark number wins
  s.onMessage(markPacket(10, P.MARK_READ, 3, 12));
  s.onMessage(markPacket(9, P.MARK_READ, 3, 11));
  assert.equal(s.chatMarks.read.get(3), 12);
  // Another thing has its own numbers: an older mark number about something else counts
  s.onMessage(markPacket(7, P.MARK_REACTION, 3, 12, true, '👍'));
  assert.deepEqual(s.chatMarks.reactions.get(12).get('👍'), [3]);
  // What does not read is not acknowledged
  const before = sent.length;
  s.onMessage(new Uint8Array([P.PT.ChatMark, ...u32(11), 2, 1, ...u32(12), 1, 1, 0x61]));
  assert.equal(sent.length, before);
});

test('a guest\'s reaction goes again until the host shows it, then no more', () => {
  const { s, sent } = guest(2);
  s.react(7, '👍', true);
  assert.deepEqual([...sent[0]], [0x1a, ...u32(7), 1, 4, ...utf8('👍')]);
  assert.equal(s.reactionsOut.length, 1);
  const t0 = s.reactionsOut[0].firstMs;
  s.resendMarks(t0 + 100);
  assert.equal(sent.length, 1);
  s.resendMarks(t0 + 401);
  assert.equal(sent.length, 2);
  // The newest wish counts: taken back before the host answered
  s.react(7, '👍', false);
  assert.equal(s.reactionsOut.length, 1);
  assert.equal(s.reactionsOut[0].on, false);
  // The host's mark for it: no need to send it again
  s.onMessage(markPacket(1, P.MARK_REACTION, 2, 7, false, '👍'));
  assert.equal(s.reactionsOut.length, 0);
  // Another player's mark for the same emoji is no answer for this one
  s.react(7, '🔥', true);
  s.onMessage(markPacket(2, P.MARK_REACTION, 1, 7, true, '🔥'));
  assert.equal(s.reactionsOut.length, 1);
  assert.ok(!s.reacted(7, '🔥'));
  s.onMessage(markPacket(3, P.MARK_REACTION, 2, 7, true, '🔥'));
  assert.equal(s.reactionsOut.length, 0);
  assert.ok(s.reacted(7, '🔥'));
  // Given and taken back at once, the taking back lost: the host's mark for the giving is an
  // older wish, and the taking back goes again
  s.react(7, '😎', true);
  s.react(7, '😎', false);
  s.onMessage(markPacket(4, P.MARK_REACTION, 2, 7, true, '😎'));
  assert.equal(s.reactionsOut.length, 1);
  assert.equal(s.reactionsOut[0].on, false);
  const tOff = s.reactionsOut[0].lastMs;
  const before = sent.length;
  s.resendMarks(tOff + 401);
  assert.deepEqual([...sent[sent.length - 1]], [0x1a, ...u32(7), 0, 4, ...utf8('😎')]);
  assert.ok(sent.length > before);
  s.onMessage(markPacket(5, P.MARK_REACTION, 2, 7, false, '😎'));
  assert.equal(s.reactionsOut.length, 0);
  // Given up after 20 seconds without an answer
  s.react(8, '😂', true);
  const t1 = s.reactionsOut[0].firstMs;
  s.resendMarks(t1 + 20001);
  assert.equal(s.reactionsOut.length, 0);
  // Not one emoji, or no message: nothing goes
  const n = sent.length;
  s.react(7, 'ok', true);
  s.react(0, '👍', true);
  assert.equal(sent.length, n);
});

test('a guest\'s read mark goes once, and again until the host\'s marks show it', () => {
  const { s, sent } = guest(2);
  s.setChatRead(5);
  assert.deepEqual([...sent[0]], [0x19, ...u32(5)]);
  s.setChatRead(5);
  s.setChatRead(4);
  assert.equal(sent.length, 1);
  const t0 = s.readSentMs;
  s.resendMarks(t0 + 1000);
  assert.equal(sent.length, 1);
  s.resendMarks(t0 + 1201);
  assert.equal(sent.length, 2);
  assert.deepEqual([...sent[1]], [0x19, ...u32(5)]);
  // The host shows it (the acknowledgement goes out too)
  s.onMessage(markPacket(1, P.MARK_READ, 2, 5));
  const n = sent.length;
  s.resendMarks(t0 + 5000);
  assert.equal(sent.length, n);
  // A host without read marks never shows it: given up after 20 seconds, and told again with the
  // next message read
  s.setChatRead(6);
  const t1 = s.readFirstMs;
  s.resendMarks(t1 + 19000);
  const m = sent.length;
  s.resendMarks(t1 + 20001);
  s.resendMarks(t1 + 30000);
  assert.equal(sent.length, m);
  s.setChatRead(7);
  assert.deepEqual([...sent[sent.length - 1]], [0x19, ...u32(7)]);
  // Not connected: nothing
  const k = sent.length;
  s.connected = false;
  s.setChatRead(9);
  assert.equal(sent.length, k);
});

// ------------------------------------------------------------------ what both chats show

test('reactions as chips: a session\'s in the app\'s order (UTF-8), one\'s own marked', () => {
  const marks = P.newChatMarks();
  for (const [slot, emoji] of [[1, '🔥'], [2, '👍'], [3, '🔥'], [2, '❤️'], [0, '😂']]) {
    P.applyChatMark(marks, { kind: P.MARK_REACTION, slot, seq: 4, on: true, emoji });
  }
  assert.deepEqual(M.roomReactions(marks, 4, 2), [
    { emoji: '❤️', count: 1, mine: true },
    { emoji: '😂', count: 1, mine: false },
    { emoji: '🔥', count: 2, mine: false },
    { emoji: '👍', count: 1, mine: true },
  ].sort((a, b) => M.byCodePoint(a.emoji, b.emoji)));
  // ❤ (U+2764) before the ones above U+FFFF, as bytes sort
  assert.equal(M.roomReactions(marks, 4, 2)[0].emoji, '❤️');
  assert.deepEqual(M.roomReactions(marks, 5, 2), []);
  assert.deepEqual(M.roomReactions(marks, 0, 2), []);
  assert.ok(M.byCodePoint('\u{1F525}', '�') > 0, 'code point order, not UTF-16 order');
});

test('reactions as chips: a friends\' chat message\'s, by key as the app\'s JSON keeps them', () => {
  assert.deepEqual(M.friendReactions({ '🔥': ['b', 'me'], '👍': ['c'], '😮': [] }, 'me'), [
    { emoji: '👍', count: 1, mine: false },
    { emoji: '🔥', count: 2, mine: true },
  ]);
  assert.deepEqual(M.friendReactions(undefined, 'me'), []);
});

test('readers\' pictures in a session\'s chat: under the newest message read, not under their own, not oneself', () => {
  const messages = [
    { slot: 0, seq: 1, name: 'Host', timeMs: 0 },
    { slot: 1, seq: 2, name: 'Jonas', timeMs: 1 },
    { slot: 2, seq: 3, name: 'Me', timeMs: 2 },
    { slot: 2, seq: 0, pending: true, name: 'Me', timeMs: 3 },
  ];
  const players = [
    { slot: 0, name: 'Host', avatar: '', you: false },
    { slot: 1, name: 'Jonas', avatar: 'blob:x', you: false },
    { slot: 2, name: 'Me', avatar: '', you: true },
  ];
  const read = new Map([[0, 3], [1, 2], [2, 3], [5, 3]]);
  const placed = M.placeRoomReaders(messages, read, players);
  // The host read up to 3 (mine): its picture there. Jonas read up to his own (2): none.
  // Me: never. Slot 5 is no longer here: none.
  assert.deepEqual([...placed.entries()], [[2, [{ name: 'Host', avatar: '' }]]]);
  // Jonas read further than his own message: under the newest he has read
  read.set(1, 3);
  assert.deepEqual(M.placeRoomReaders(messages, read, players).get(2).map((r) => r.name), ['Host', 'Jonas']);
  // A newer message by Jonas: he has seen everything up to it, his picture goes (under his own: nowhere)
  messages.push({ slot: 1, seq: 4, name: 'Jonas', timeMs: 4 });
  assert.deepEqual(M.placeRoomReaders(messages, read, players).get(2).map((r) => r.name), ['Host']);
  assert.ok(!M.placeRoomReaders(messages, read, players).has(4));
});

test('readers\' pictures in a friends\' chat: by time, not under their own message', () => {
  const messages = [
    { from: 'me', at: 100 },
    { from: 'jonas', at: 200 },
    { system: true, at: 250 },
    { from: 'me', at: 300 },
    { from: 'me', at: 400 },
  ];
  const readers = [{ fp: 'jonas', name: 'Jonas', avatar: '', at: 300 }, { fp: 'lea', name: 'Lea', avatar: 'data:x', at: 999 }];
  const placed = M.placeFriendReaders(messages, readers);
  assert.deepEqual([...placed.keys()], [3, 4]);
  assert.equal(placed.get(3)[0].name, 'Jonas');
  assert.equal(placed.get(4)[0].name, 'Lea');
  // Somebody who read only up to their own message shows nowhere
  assert.equal(M.placeFriendReaders(messages, [{ fp: 'jonas', name: 'Jonas', avatar: '', at: 210 }]).size, 0);
});

test('a run by one writer shows the picture and name once', () => {
  const room = [
    { slot: 1, name: 'Jonas', timeMs: 0 },
    { slot: 1, name: 'Jonas', timeMs: 60000 },
    { slot: 1, name: 'Jonas', timeMs: 200000 },
    { slot: 2, name: 'Lea', timeMs: 200001 },
    { slot: 2, name: 'Lena', timeMs: 200002 },
  ];
  assert.deepEqual(room.map((_, i) => M.roomRunStarts(room, i)), [true, false, true, true, true]);
  const friends = [{ from: 'a' }, { from: 'a' }, { system: true }, { from: 'a' }, { from: 'b' }];
  assert.deepEqual(friends.map((_, i) => M.friendRunStarts(friends, i)), [true, false, true, true, true]);
});

test('the quick reactions and the emoji are the app\'s, and each of them is one reaction', () => {
  assert.deepEqual(M.QUICK_REACTIONS, ['❤️', '😂', '👍', '😮', '😢', '🔥']);
  assert.equal(M.EMOJI_GROUPS.length, 5);
  for (const emoji of M.QUICK_REACTIONS) assert.ok(P.validReaction(emoji), emoji);
  for (const [, list] of M.EMOJI_GROUPS) {
    for (const emoji of list.split(' ')) assert.ok(P.validReaction(emoji), emoji);
  }
});

// ------------------------------------------------------------------ mentions

test('a mention of a player: "@" and their name, at the start or after a space or punctuation, nothing after it', () => {
  const yes = ['@Mara', 'hi @Mara', 'hi @mara!', '(@MARA)', 'ok,@Mara', 'see you\n@Mara', '@Mara\'s turn', '@Mara🔥', '@Mara.', '@@Mara',
    'race: @Mara?'];
  const no = ['mail@Mara', '@Mara2', '@Marab', '@Maraé', 'Mara', '@ Mara', '@Mar', '😀@Mara', ''];
  for (const t of yes) assert.ok(M.mentions(t, 'Mara'), t);
  for (const t of no) assert.ok(!M.mentions(t, 'Mara'), t);
  // Any case, beyond ASCII too
  assert.ok(M.mentions('salut @ÉLODIE', 'élodie'));
  // A name with a space or with what a pattern would read as more
  assert.ok(M.mentions('@Ann Marie, you?', 'Ann Marie'));
  assert.ok(!M.mentions('@Ann Mariee', 'Ann Marie'));
  assert.ok(M.mentions('go @A.B', 'A.B'));
  assert.ok(!M.mentions('go @AxB', 'A.B'));
  assert.ok(M.mentions('(@x+y)', 'x+y'));
  // No name: nobody
  assert.ok(!M.mentions('@', ''));
  assert.ok(!M.mentions('@ ', '  '));
});

test('mentions cut out of a text for drawing: the longest name first, the name as listed', () => {
  assert.deepEqual(M.splitMentions('@ann marie and @Ann, x@Ann @Bob2 @bob', ['Ann', 'Ann Marie', 'Bob']), [
    { text: '@ann marie', name: 'Ann Marie' },
    { text: ' and ', name: '' },
    { text: '@Ann', name: 'Ann' },
    { text: ', x@Ann @Bob2 ', name: '' },
    { text: '@bob', name: 'Bob' },
  ]);
  assert.deepEqual(M.splitMentions('no names here', ['Ann']), [{ text: 'no names here', name: '' }]);
  assert.deepEqual(M.splitMentions('@Ann', []), [{ text: '@Ann', name: '' }]);
  assert.deepEqual(M.splitMentions('', ['Ann']), []);
  // Back to back: the second "@" follows punctuation of its own ("@" is punctuation)
  assert.deepEqual(M.splitMentions('@Ann!@Bob', ['Ann', 'Bob']).map((p) => p.name), ['Ann', '', 'Bob']);
});

test('what is typed after "@" offers names: those that start with it first', () => {
  assert.deepEqual(M.mentionQuery('hey @Le', 7), { at: 4, query: 'Le' });
  assert.deepEqual(M.mentionQuery('@', 1), { at: 0, query: '' });
  assert.equal(M.mentionQuery('mail@Le', 7), null);
  assert.equal(M.mentionQuery('no at', 5), null);
  assert.equal(M.mentionQuery('@a\nb', 4), null);
  // Only what is before the caret counts
  assert.deepEqual(M.mentionQuery('@Le and more', 3), { at: 0, query: 'Le' });
  const people = [{ name: 'Lea' }, { name: 'Jonas' }, { name: 'Ole' }, { name: 'Leon' }];
  assert.deepEqual(M.mentionChoices(people, 'le').map((p) => p.name), ['Lea', 'Leon', 'Ole']);
  assert.deepEqual(M.mentionChoices(people, '').map((p) => p.name), ['Lea', 'Jonas', 'Ole', 'Leon']);
  assert.deepEqual(M.mentionChoices(people, 'x'), []);
});

test('a quote is the start of a message on one line, at most 160 bytes of UTF-8', () => {
  assert.equal(M.oneLine('  a\n\tb   c '), 'a b c');
  const long = 'é'.repeat(100);
  const cut = M.oneLine(long);
  assert.ok(new TextEncoder().encode(cut).length <= 160);
  assert.equal(cut, 'é'.repeat(80));
  assert.equal(M.oneLine('🔥'.repeat(41)).length, 80, 'never half a character');
});
