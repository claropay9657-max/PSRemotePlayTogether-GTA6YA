// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The browser's side of friends' cryptography (web/js/friends/crypto.js) against the app's
// (coplay/src/friends_crypto.cpp): friends-vectors.json holds what the C++ made from fixed
// keys and fixed "random" bytes (coplay/tools/friends-vectors.cpp writes it), and the page has
// to come out the same, byte for byte. ECDSA signatures are random each time, so the ones in
// the vectors are verified, not made again.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { Writer, base64UrlDecode, base64UrlEncode, concat, hex } from '../../web/js/bytes.js';
import * as fc from '../../web/js/friends/crypto.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const V = JSON.parse(fs.readFileSync(path.join(here, 'friends-vectors.json'), 'utf8'));

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const text = (s) => encoder.encode(s);
const str = (b) => decoder.decode(b);
const h = (s) => new Uint8Array((s.match(/../g) || []).map((x) => parseInt(x, 16)));

/** A key pair of the vectors ({d, pub} in hex) as the page keeps one. */
async function kp(k, usage = 'ecdh', extractable = false) {
  const pair = await fc.importKeyPair(concat(h(k.d), h(k.pub)), { usage, extractable });
  assert.ok(pair, 'the key pair imports');
  return pair;
}

/** An identity of the vectors. */
async function identity(v) {
  return { iks: await kp(v.iks, 'ecdsa'), ikd: await kp(v.ikd, 'ecdh'), ikdSig: h(v.ikd_sig), fp: h(v.fp) };
}

/** A key source that hands out the given key pairs in order, as the C++ Fixed::Keys() did. */
function keysFrom(list) {
  const source = async (usage) => {
    assert.ok(source.used < list.length, 'more key pairs asked for than the C++ made');
    return kp(list[source.used++], usage);
  };
  source.used = 0;
  return source;
}

// The fixed keys and bytes of coplay/tools/friends-vectors.cpp, made again here
const ORDER = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
const PKCS8_HEAD = h('308141020100301306072a8648ce3d020106082a8648ce3d030107042730250201010420');

/** Key pair number i: d = H("psrpt vector key" || u32 i || u32 round). WebCrypto works out the point from a PKCS #8 key without one. */
async function fixedKey(i) {
  for (let round = 0; ; round++) {
    const d = await fc.sha256(concat(text('psrpt vector key'), new Writer(8).u32(i).u32(round).done()));
    const n = BigInt('0x' + hex(d));
    if (n === 0n || n >= ORDER) continue;
    const key = await crypto.subtle.importKey('pkcs8', concat(PKCS8_HEAD, d), { name: 'ECDH', namedCurve: 'P-256' }, true,
      ['deriveBits']);
    const jwk = await crypto.subtle.exportKey('jwk', key);
    return { d: hex(d), pub: '04' + hex(base64UrlDecode(jwk.x)) + hex(base64UrlDecode(jwk.y)) };
  }
}

/** "Random" bytes number i: H("psrpt vector random" || u32 i || u8 block) one after the other. */
async function fixedRandom(i, size) {
  const seed = concat(text('psrpt vector random'), new Writer(4).u32(i).done());
  const blocks = [];
  for (let block = 0; blocks.length * 32 < size; block++) blocks.push(await fc.sha256(concat(seed, new Uint8Array([block]))));
  return concat(...blocks).slice(0, size);
}

/** A random source that gives "random" bytes number first, first + 1, ... */
function randomFrom(first) {
  let next = first;
  return (size) => fixedRandom(next++, size);
}

test('the fixed keys and bytes in the vectors are the ones the generator says', async () => {
  const keys = { 1: V.alice.iks, 2: V.alice.ikd, 3: V.bob.iks, 4: V.bob.ikd, 5: V.x3dh.ek, 6: V.x3dh.ek_no_opk, 1000: V.primitives.a,
    1001: V.primitives.b, 3000: V.card.spk, 3001: V.card.opk, 4000: V.envelope.ratchet_key, 5000: V.group.sign };
  V.ratchet.keys.forEach((k, i) => { keys[7 + i] = k; });
  for (const [i, k] of Object.entries(keys)) assert.deepEqual(await fixedKey(Number(i)), k, `key ${i}`);
  assert.equal(hex(await fixedRandom(1, 16)), V.names.link_key);
  assert.equal(hex(await fixedRandom(2, 8)), V.names.alice_device);
  assert.equal(hex(await fixedRandom(3, 8)), V.names.bob_device);
  assert.equal(hex(await fixedRandom(4, 16)), V.names.group_id);
  assert.equal(hex(await fixedRandom(5, 16)), V.names.link_secret);
  assert.equal(hex(await fixedRandom(6, 12)), V.card.profile_sealed.slice(0, 24));
  assert.equal(hex(await fixedRandom(7, 32)), V.group.ck);
  assert.equal(hex(await fixedRandom(8, 32)), V.status.epoch_key);
});

test('the primitives: HKDF, ECDH, ECDSA, AES-GCM, HMAC and PBKDF2 as the C++ makes them', async () => {
  const p = V.primitives;
  assert.equal(hex(await fc.hkdf(h(V.hkdf.salt), h(V.hkdf.ikm), V.hkdf.info, 80)), V.hkdf.out);
  assert.equal(hex(await fc.hkdf(new Uint8Array(0), h(V.hkdf.ikm), V.hkdf.info, 32)), V.hkdf.out_empty_salt);
  assert.equal(hex(await fc.hkdf(null, h(V.hkdf.ikm), V.hkdf.info, 32)), V.hkdf.out_empty_salt);
  // RFC 5869, case 1, as the C++ test checks it
  const rfc = await fc.hkdf(h('000102030405060708090a0b0c'), new Uint8Array(22).fill(0x0b), h('f0f1f2f3f4f5f6f7f8f9'), 42);
  assert.equal(hex(rfc), '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865');

  const a = await kp(p.a, 'both');
  const b = await kp(p.b, 'both');
  assert.equal(hex(await fc.dh(a, h(p.b.pub))), p.dh);
  assert.equal(hex(await fc.dh(b, h(p.a.pub))), p.dh);
  // the C++'s signature, and one of the page's own
  assert.ok(await fc.verify(h(p.a.pub), h(p.signed), h(p.sig)));
  assert.ok(!(await fc.verify(h(p.b.pub), h(p.signed), h(p.sig))));
  assert.ok(!(await fc.verify(h(p.a.pub), text('signed texT'), h(p.sig))));
  const sig = await fc.sign(a, h(p.signed));
  assert.equal(sig.length, 64);
  assert.ok(await fc.verify(h(p.a.pub), h(p.signed), sig));
  sig[10] ^= 1;
  assert.ok(!(await fc.verify(h(p.a.pub), h(p.signed), sig)));

  assert.equal(hex(await fc.seal(h(p.key), h(p.nonce), h(p.ad), h(p.plain))), p.sealed);
  assert.equal(hex(await fc.open(h(p.key), h(p.nonce), h(p.ad), h(p.sealed))), p.plain);
  assert.equal(await fc.open(h(p.key), h(p.nonce), text('aD'), h(p.sealed)), null);
  assert.equal(await fc.open(h(p.key), h(p.nonce), h(p.ad), h(p.sealed).subarray(0, 15)), null);
  assert.equal(hex(await fc.hmac(h(p.key), text('data'))), p.hmac);
  assert.equal(hex(await fc.pbkdf2('passphrase', text('saltsalt'), 1000)), p.pbkdf2);

  // what is no point on the curve
  const bad = h(p.b.pub);
  bad[64] ^= 1;
  assert.ok(!(await fc.validPublic(bad)));
  assert.ok(await fc.validPublic(h(p.b.pub)));
  assert.equal(await fc.dh(a, bad), null);
  assert.equal(await fc.dh(a, h(p.b.pub).subarray(0, 64)), null);
});

test('keys the page makes for itself stay in it; identity keys travel as d || pub', async () => {
  const own = await fc.generateKeyPair();
  assert.equal(own.pub.length, 65);
  assert.equal(own.pub[0], 0x04);
  await assert.rejects(fc.exportPrivate(own));
  const signer = await fc.generateKeyPair({ usage: 'ecdsa' });
  await assert.rejects(fc.exportPrivate(signer));

  const id = await fc.makeIdentity();
  assert.equal(hex(id.fp), hex(await fc.fingerprint(id.iks.pub, id.ikd.pub)));
  assert.ok(await fc.verify(id.iks.pub, concat(text('psrpt ikd'), id.ikd.pub), id.ikdSig));
  const iks = await fc.exportPrivate(id.iks);
  const ikd = await fc.exportPrivate(id.ikd);
  assert.equal(iks.length, 97);
  assert.equal(hex(iks.subarray(32)), hex(id.iks.pub));
  // back in, on another device: it signs and agrees as the old one did
  const iks2 = await fc.importKeyPair(iks, { usage: 'ecdsa' });
  const ikd2 = await fc.importKeyPair(ikd, { usage: 'ecdh' });
  assert.ok(await fc.verify(id.iks.pub, text('x'), await fc.sign(iks2, text('x'))));
  assert.equal(hex(await fc.dh(ikd2, own.pub)), hex(await fc.dh(own, id.ikd.pub)));
  await assert.rejects(fc.exportPrivate(ikd2));
  // the ratchet's state, keys and all, goes into IndexedDB as it is
  const copy = structuredClone(await fc.ratchetInitInitiator(new Uint8Array(32), own.pub));
  assert.ok(copy.dhs.ecdh instanceof CryptoKey && copy.skipped instanceof Map);
  assert.equal(await fc.importKeyPair(iks.subarray(0, 96)), null);
});

test('identities, names and codes', async () => {
  const alice = await identity(V.alice);
  const bob = await identity(V.bob);
  for (const [v, id] of [[V.alice, alice], [V.bob, bob]]) {
    assert.ok(await fc.verify(id.iks.pub, concat(text('psrpt ikd'), id.ikd.pub), id.ikdSig));
    const again = await fc.completeIdentity({ iks: id.iks, ikd: id.ikd });
    assert.equal(hex(again.fp), v.fp);
    assert.ok(await fc.verify(id.iks.pub, concat(text('psrpt ikd'), id.ikd.pub), again.ikdSig));
  }
  const n = V.names;
  assert.equal(hex(await fc.requestBox(h(n.link_key))), n.request_box);
  assert.equal(hex(await fc.inboxName(alice.fp, h(n.alice_device))), n.alice_inbox);
  assert.equal(hex(await fc.groupBox(h(n.group_id))), n.group_box);
  assert.equal(hex(await fc.linkBox(h(n.link_secret))), n.link_box);
  const linkDh = await fc.dh(await kp(await fixedKey(2000)), h((await fixedKey(2001)).pub));
  assert.equal(hex(linkDh), n.link_dh);
  const k = await fc.linkKey(h(n.link_secret), h(n.link_dh));
  assert.equal(hex(k), n.link_key32);
  assert.equal(await fc.linkCode(k), n.link_code);
  assert.equal(await fc.safetyCode(alice.fp, bob.fp), n.safety);
  assert.equal(await fc.safetyCode(bob.fp, alice.fp), n.safety);
  assert.match(await fc.safetyCode(new Uint8Array(16).fill(1), new Uint8Array(16).fill(2)), /^\d{5}( \d{5}){4}$/);
});

test('the card: read, every signature checked, the profile opened, and written the same', async () => {
  const bytes = h(V.card.bytes);
  const card = await fc.decodeCard(bytes);
  assert.ok(card);
  assert.equal(hex(card.fp), V.card.fp);
  assert.equal(hex(card.iks), V.bob.iks.pub);
  assert.equal(hex(card.ikdSig), V.bob.ikd_sig);
  assert.equal(card.seq, 2);
  assert.equal(card.devices.length, 1);
  const [d] = card.devices;
  assert.equal(hex(d.id), V.names.bob_device);
  assert.equal(d.kind, 1);
  assert.equal(d.spkId, 1);
  assert.equal(hex(d.spk), V.card.spk.pub);
  assert.equal(hex(card.profile), V.card.profile_sealed);

  const lk = h(V.names.link_key);
  assert.equal(await fc.openProfile(lk, card.fp, card.profile), V.card.profile_json);
  const otherLk = lk.slice();
  otherLk[0] ^= 1;
  assert.equal(await fc.openProfile(otherLk, card.fp, card.profile), null);
  assert.equal(await fc.openProfile(lk, h(V.alice.fp), card.profile), null);
  assert.equal(hex(await fc.sealProfile(lk, card.fp, V.card.profile_json, randomFrom(6))), V.card.profile_sealed);

  // Written again: everything but card_sig the same; the page's own signatures hold
  const bob = await identity(V.bob);
  const spkSig = await fc.signPrekey(bob, d.id, 1, d.spk);
  assert.ok(await fc.verify(bob.iks.pub, concat(text('psrpt spk'), bob.fp, d.id, h('00000001'), d.spk), spkSig));
  const again = { seq: 2, devices: [{ ...d }], profile: card.profile };
  const written = await fc.encodeCard(bob, again);
  assert.equal(hex(written.subarray(0, written.length - 64)), V.card.bytes.slice(0, -128));
  assert.equal(hex(again.cardSig), hex(written.subarray(written.length - 64)));
  const read = await fc.decodeCard(written);
  assert.ok(read);
  assert.equal(hex(read.fp), V.card.fp);
  const fresh = await fc.encodeCard(bob, { seq: 3, devices: [{ ...d, spkSig }], profile: new Uint8Array(0) });
  assert.ok(await fc.decodeCard(fresh));

  // any changed byte, anywhere, makes it no card
  let refused = 0;
  let tried = 0;
  for (let i = 0; i < bytes.length; i += 3) {
    const changed = bytes.slice();
    changed[i] ^= 0x20;
    tried++;
    if (!(await fc.decodeCard(changed))) refused++;
  }
  assert.equal(refused, tried);
  // cut short, too long
  assert.equal(await fc.decodeCard(bytes.subarray(0, bytes.length - 1)), null);
  assert.equal(await fc.decodeCard(concat(bytes, new Uint8Array(1))), null);
  assert.equal(await fc.decodeCard(new Uint8Array(0)), null);
  // a prekey signed by somebody else
  const alice = await identity(V.alice);
  const forged = await fc.encodeCard(bob, { seq: 3, devices: [{ ...d, spkSig: await fc.signPrekey(alice, d.id, 1, d.spk) }] });
  assert.equal(await fc.decodeCard(forged), null);
  // no devices, too many
  assert.equal(await fc.encodeCard(bob, { seq: 1, devices: [] }), null);
  assert.equal(await fc.encodeCard(bob, { seq: 1, devices: new Array(11).fill(d) }), null);
});

test('X3DH: both sides agree, with and without a one-time prekey', async () => {
  const alice = await identity(V.alice);
  const bob = await identity(V.bob);
  const spk = await kp(V.card.spk);
  const opk = await kp(V.card.opk);
  const aliceDevice = h(V.names.alice_device);
  const bobDevice = h(V.names.bob_device);
  const bundle = { fp: bob.fp, ikd: bob.ikd.pub, device: bobDevice, spkId: 1, spk: spk.pub, opkId: 5, opk: opk.pub };

  const start = await fc.x3dhInitiate(alice, aliceDevice, bundle, keysFrom([V.x3dh.ek]));
  assert.equal(hex(start.ek.pub), V.x3dh.ek.pub);
  assert.equal(hex(start.sk), V.x3dh.sk);
  assert.equal(hex(start.ad), V.x3dh.ad);
  const noOpk = await fc.x3dhInitiate(alice, aliceDevice, { ...bundle, opkId: 0 }, keysFrom([V.x3dh.ek_no_opk]));
  assert.equal(hex(noOpk.sk), V.x3dh.sk_no_opk);
  assert.equal(hex(noOpk.ad), V.x3dh.ad);

  const answer = await fc.x3dhRespond(bob, bobDevice, spk, opk, alice.fp, alice.ikd.pub, aliceDevice, h(V.x3dh.ek.pub));
  assert.equal(hex(answer.sk), V.x3dh.sk);
  assert.equal(hex(answer.ad), V.x3dh.ad);
  const answerNoOpk = await fc.x3dhRespond(bob, bobDevice, spk, null, alice.fp, alice.ikd.pub, aliceDevice, h(V.x3dh.ek_no_opk.pub));
  assert.equal(hex(answerNoOpk.sk), V.x3dh.sk_no_opk);
  // the wrong one-time prekey, or none where one was used, is another key
  const wrong = await fc.x3dhRespond(bob, bobDevice, spk, null, alice.fp, alice.ikd.pub, aliceDevice, h(V.x3dh.ek.pub));
  assert.notEqual(hex(wrong.sk), V.x3dh.sk);
  // a bundle with a key that is no point
  const bad = h(V.card.spk.pub);
  bad[40] ^= 1;
  assert.equal(await fc.x3dhInitiate(alice, aliceDevice, { ...bundle, spk: bad }), null);
});

test('the Double Ratchet: the vectors\' conversation, message for message, and the first message as an envelope', async () => {
  const sk = h(V.x3dh.sk);
  const ad = h(V.x3dh.ad);
  const spk = await kp(V.card.spk);
  const keys = keysFrom(V.ratchet.keys);
  const a = await fc.ratchetInitInitiator(sk, spk.pub, keys);
  const b = fc.ratchetInitResponder(sk, spk);
  for (const [i, m] of V.ratchet.messages.entries()) {
    const from = m.from === 'a' ? a : b;
    const to = m.from === 'a' ? b : a;
    const sent = await fc.ratchetEncrypt(from, ad, text(m.text));
    assert.equal(hex(fc.encodeHeader(sent.header)), m.header, `header of message ${i}`);
    assert.equal(hex(sent.sealed), m.sealed, `message ${i}`);
    assert.ok(m.opened);
    const header = fc.decodeHeader(h(m.header));
    assert.equal(str(await fc.ratchetDecrypt(to, ad, header, h(m.sealed), 1000, keys)), m.text, `opening message ${i}`);
    // read twice, it does not open again
    assert.equal(await fc.ratchetDecrypt(to, ad, header, h(m.sealed), 1000, keys), null, `message ${i} again`);
  }
  assert.equal(keys.used, V.ratchet.keys.length);

  // A first message: Alice's envelope to Bob
  const alice = await identity(V.alice);
  const bob = await identity(V.bob);
  const a2 = await fc.ratchetInitInitiator(sk, spk.pub, keysFrom([V.envelope.ratchet_key]));
  const sent = await fc.ratchetEncrypt(a2, ad, text('{"t":"request"}'));
  const envelope = {
    kind: 1, fromFp: alice.fp, fromDevice: h(V.names.alice_device), toDevice: h(V.names.bob_device), ek: h(V.x3dh.ek.pub),
    spkId: 1, opkId: 5, header: sent.header, sealed: sent.sealed,
  };
  assert.equal(hex(fc.encodeEnvelope(envelope)), V.envelope.bytes);
  // Bob, from what the envelope says
  const e = fc.decodeEnvelope(h(V.envelope.bytes));
  assert.ok(e);
  assert.equal(e.kind, 1);
  assert.equal(hex(e.fromFp), V.alice.fp);
  assert.equal(hex(e.toDevice), V.names.bob_device);
  assert.equal(e.spkId, 1);
  assert.equal(e.opkId, 5);
  assert.equal(hex(fc.encodeEnvelope(e)), V.envelope.bytes);
  const answer = await fc.x3dhRespond(bob, e.toDevice, spk, e.opkId ? await kp(V.card.opk) : null, e.fromFp, alice.ikd.pub,
    e.fromDevice, e.ek);
  assert.equal(hex(answer.sk), V.x3dh.sk);
  const b2 = fc.ratchetInitResponder(answer.sk, spk);
  assert.equal(str(await fc.ratchetDecrypt(b2, answer.ad, e.header, e.sealed, 1000)), '{"t":"request"}');
  // what is no envelope
  const bytes = h(V.envelope.bytes);
  assert.equal(fc.decodeEnvelope(concat(bytes, new Uint8Array(1))), null);
  assert.equal(fc.decodeEnvelope(bytes.subarray(0, bytes.length - 1)), null);
  const kind3 = bytes.slice();
  kind3[1] = 3;
  assert.equal(fc.decodeEnvelope(kind3), null);
  assert.equal(fc.decodeHeader(h(V.ratchet.messages[0].header).subarray(1)), null);
});

/** Two devices that met through X3DH, with their ratchets, and Alice's first message opened. */
async function startPair(withOpk = true) {
  const alice = await fc.makeIdentity();
  const bob = await fc.makeIdentity();
  const aliceDevice = new Uint8Array(8).fill(0xa1);
  const bobDevice = new Uint8Array(8).fill(0xb2);
  const spk = await fc.generateKeyPair();
  const opk = await fc.generateKeyPair();
  const bundle = { fp: bob.fp, ikd: bob.ikd.pub, device: bobDevice, spkId: 1, spk: spk.pub, opkId: withOpk ? 5 : 0, opk: opk.pub };
  const start = await fc.x3dhInitiate(alice, aliceDevice, bundle);
  const a = await fc.ratchetInitInitiator(start.sk, spk.pub);
  const first = await fc.ratchetEncrypt(a, start.ad, text('hello'));
  const answer = await fc.x3dhRespond(bob, bobDevice, spk, withOpk ? opk : null, alice.fp, alice.ikd.pub, aliceDevice, start.ek.pub);
  const b = fc.ratchetInitResponder(answer.sk, spk);
  assert.equal(str(await fc.ratchetDecrypt(b, answer.ad, first.header, first.sealed, 1000)), 'hello');
  return { alice, bob, aliceDevice, bobDevice, spk, opk, start, first, a, b, adA: start.ad, adB: answer.ad };
}

function stateOf(r) {
  return [hex(r.rk), r.cks && hex(r.cks), r.ckr && hex(r.ckr), r.dhr && hex(r.dhr), hex(r.dhs.pub), r.ns, r.nr, r.pn,
    [...r.skipped.keys()].sort().join()].join('|');
}

test('the Double Ratchet: out of order, read twice, changed bytes, wrong keys', async () => {
  const p = await startPair();
  const open = async (r, ad, s, now = 2000) => {
    const plain = await fc.ratchetDecrypt(r, ad, s.header, s.sealed, now);
    return plain && str(plain);
  };
  // Bob answers: a new key agreement
  const b1 = await fc.ratchetEncrypt(p.b, p.adB, text('hi Alice'));
  assert.equal(await open(p.a, p.adA, b1), 'hi Alice');
  // Alice sends three: the third arrives first, the second last
  const a1 = await fc.ratchetEncrypt(p.a, p.adA, text('one'));
  const a2 = await fc.ratchetEncrypt(p.a, p.adA, text('two'));
  const a3 = await fc.ratchetEncrypt(p.a, p.adA, text('three'));
  assert.equal(await open(p.b, p.adB, a3), 'three');
  assert.equal(p.b.skipped.size, 2);
  assert.equal(await open(p.b, p.adB, a1), 'one');
  assert.equal(p.b.skipped.size, 1);
  // kept in IndexedDB between two messages
  const kept = structuredClone(p.b);
  assert.equal(await open(kept, p.adB, a2), 'two');
  assert.equal(kept.skipped.size, 0);
  // read twice, a skipped one or the newest: no second time
  assert.equal(await open(kept, p.adB, a2), null);
  assert.equal(await open(kept, p.adB, a3), null);

  // a changed byte in the sealed text, the header or the associated data; the state stays as it was
  const b2 = await fc.ratchetEncrypt(kept, p.adB, text('four'));
  const before = stateOf(p.a);
  const sealed = b2.sealed.slice();
  sealed[0] ^= 1;
  assert.equal(await open(p.a, p.adA, { header: b2.header, sealed }), null);
  assert.equal(await open(p.a, p.adA, { header: { ...b2.header, n: b2.header.n + 1 }, sealed: b2.sealed }), null);
  assert.equal(await open(p.a, p.adA, { header: { ...b2.header, pn: b2.header.pn + 1 }, sealed: b2.sealed }), null);
  const dh = b2.header.dh.slice();
  dh[64] ^= 1;
  assert.equal(await open(p.a, p.adA, { header: { ...b2.header, dh }, sealed: b2.sealed }), null);
  const otherKey = await fc.generateKeyPair();
  assert.equal(await open(p.a, p.adA, { header: { ...b2.header, dh: otherKey.pub }, sealed: b2.sealed }), null);
  const otherAd = p.adA.slice();
  otherAd[0] ^= 1;
  assert.equal(await open(p.a, otherAd, b2), null);
  assert.equal(stateOf(p.a), before);
  // and the real one still opens after all that
  assert.equal(await open(p.a, p.adA, b2), 'four');

  // a long back and forth
  for (let i = 0; i < 12; i++) {
    const [from, adFrom, to, adTo] = i % 3 ? [p.a, p.adA, kept, p.adB] : [kept, p.adB, p.a, p.adA];
    const s = await fc.ratchetEncrypt(from, adFrom, text(`m${i}`));
    assert.equal(await open(to, adTo, s), `m${i}`);
  }

  // a responder before the first message has no sending chain
  assert.equal(await fc.ratchetEncrypt(fc.ratchetInitResponder(new Uint8Array(32), p.spk), p.adB, text('x')), null);
});

test('the Double Ratchet: the wrong one-time prekey, too many skipped messages, keys too old', async () => {
  // Bob answers without the one-time prekey Alice used: nothing opens
  const p = await startPair(true);
  const wrong = await fc.x3dhRespond(p.bob, p.bobDevice, p.spk, null, p.alice.fp, p.alice.ikd.pub, p.aliceDevice, p.start.ek.pub);
  const b = fc.ratchetInitResponder(wrong.sk, p.spk);
  assert.equal(await fc.ratchetDecrypt(b, wrong.ad, p.first.header, p.first.sealed, 1000), null);
  // nor with the right keys but somebody else's prekey
  const other = await fc.generateKeyPair();
  const right = await fc.x3dhRespond(p.bob, p.bobDevice, p.spk, other, p.alice.fp, p.alice.ikd.pub, p.aliceDevice, p.start.ek.pub);
  assert.equal(await fc.ratchetDecrypt(fc.ratchetInitResponder(right.sk, p.spk), right.ad, p.first.header, p.first.sealed, 1000), null);

  // Alice sends 1005 more (n = 1 .. 1005); Bob has read n = 0
  const sent = [];
  for (let i = 1; i <= fc.MAX_SKIPPED_KEYS + 5; i++) sent.push(await fc.ratchetEncrypt(p.a, p.adA, text(`x${i}`)));
  const open = async (s, now) => {
    const plain = await fc.ratchetDecrypt(p.b, p.adB, s.header, s.sealed, now);
    return plain && str(plain);
  };
  // 1004 to skip: refused, and nothing changed
  const before = stateOf(p.b);
  assert.equal(await open(sent[1004], 5000), null);
  assert.equal(stateOf(p.b), before);
  // exactly 1000 to skip is still fine
  assert.equal(await open(sent[1000], 5000), 'x1001');
  assert.equal(p.b.skipped.size, fc.MAX_SKIPPED_KEYS);
  assert.equal(await open(sent[0], 5000), 'x1');
  assert.equal(p.b.skipped.size, fc.MAX_SKIPPED_KEYS - 1);
  // three more skipped make 1002, too many to keep: the oldest go (all of the same age, so in key order: n = 2, 3)
  assert.equal(await open(sent[1004], 5000), 'x1005');
  assert.equal(p.b.skipped.size, fc.MAX_SKIPPED_KEYS);
  assert.equal(await open(sent[1], 5000), null);
  assert.equal(await open(sent[2], 5000), null);
  assert.equal(await open(sent[3], 5000), 'x4');
  // keys kept for more than 30 days go
  assert.equal(await open(sent[4], 5000 + fc.SKIPPED_KEY_MAX_AGE_MS + 1), null);
  assert.equal(p.b.skipped.size, 0);
});

test('groups: sender keys seal as the C++ does, and only the writer\'s chain opens them', async () => {
  const g = V.group;
  const alice = await identity(V.alice);
  const groupId = h(V.names.group_id);
  const device = h(V.names.alice_device);
  const own = await fc.newSenderChain(1, keysFrom([g.sign]), randomFrom(7));
  assert.equal(hex(own.ck), g.ck);
  assert.equal(hex(own.signPub), g.sign.pub);
  const theirs = fc.senderChainOf(g.chain, 0, h(g.ck), h(g.sign.pub));
  const decoded = [];
  for (const [i, m] of g.messages.entries()) {
    const bytes = await fc.groupSeal(own, groupId, alice.fp, device, text(m.text));
    // everything but the signature, which is random each time
    assert.equal(hex(bytes.subarray(0, bytes.length - 64)), m.bytes.slice(0, -128), `group message ${i}`);
    const vector = fc.decodeGroupMessage(h(m.bytes));
    assert.ok(vector);
    assert.equal(vector.iter, i);
    assert.equal(hex(fc.encodeGroupMessage(vector)), m.bytes);
    assert.ok(await fc.verify(h(g.sign.pub), h(m.bytes).subarray(0, h(m.bytes).length - 64), vector.sig), `signature ${i}`);
    // the page's own signature holds as well
    const mine = fc.decodeGroupMessage(bytes);
    assert.ok(await fc.verify(own.signPub, bytes.subarray(0, bytes.length - 64), mine.sig));
    decoded.push(vector);
  }
  assert.equal(own.iter, 3);
  // out of order, and not twice
  const openText = async (chain, m) => {
    const plain = await fc.groupOpen(chain, m);
    return plain && str(plain);
  };
  assert.equal(await openText(theirs, decoded[2]), 'group 2');
  assert.equal(theirs.skipped.size, 2);
  assert.equal(await openText(theirs, decoded[0]), 'group 0');
  assert.equal(await openText(theirs, decoded[0]), null);
  assert.equal(await openText(theirs, decoded[2]), null);
  // signed by somebody else, changed, of another chain
  const impostor = await fc.generateKeyPair({ usage: 'ecdsa' });
  assert.equal(await openText({ ...theirs, signPub: impostor.pub }, decoded[1]), null);
  const changed = h(g.messages[1].bytes);
  changed[changed.length - 70] ^= 1;
  assert.equal(await openText(theirs, fc.decodeGroupMessage(changed)), null);
  assert.equal(await openText({ ...theirs, chain: 2 }, decoded[1]), null);
  assert.equal(theirs.skipped.size, 1);
  assert.equal(await openText(theirs, decoded[1]), 'group 1');
  assert.equal(theirs.skipped.size, 0);
  // what is no group message
  const bytes = h(g.messages[0].bytes);
  assert.equal(fc.decodeGroupMessage(concat(bytes, new Uint8Array(1))), null);
  assert.equal(fc.decodeGroupMessage(bytes.subarray(0, bytes.length - 1)), null);

  // a message 2000 ahead is fine, one further is refused
  const mine = await fc.newSenderChain(4);
  const first = fc.senderChainOf(4, 0, mine.ck, mine.signPub);
  const sent = [];
  for (let i = 0; i <= fc.MAX_GROUP_SKIPPED_KEYS + 1; i++) sent.push(await fc.groupSeal(mine, groupId, alice.fp, device, text(`g${i}`)));
  assert.equal(await openText({ ...first, skipped: new Map() }, fc.decodeGroupMessage(sent[fc.MAX_GROUP_SKIPPED_KEYS + 1])), null);
  assert.equal(await openText(first, fc.decodeGroupMessage(sent[fc.MAX_GROUP_SKIPPED_KEYS])), `g${fc.MAX_GROUP_SKIPPED_KEYS}`);
  assert.equal(first.skipped.size, fc.MAX_GROUP_SKIPPED_KEYS);
  assert.equal(await openText(first, fc.decodeGroupMessage(sent[7])), 'g7');
});

test('status: the note of a day, sealed as the C++ does', async () => {
  const s = V.status;
  const alice = await identity(V.alice);
  const epochKey = h(s.epoch_key);
  assert.equal(hex(await fc.friendsKeyForDay(epochKey, s.day)), s.day_key);
  assert.equal(hex(await fc.friendsKeyForDay(epochKey, 0)), s.epoch_key);
  assert.equal(hex(await fc.friendsKeyForDay(await fc.friendsKeyForDay(epochKey, 1), 3)), s.day_key);
  const note = await fc.sealStatus(epochKey, s.epoch, s.day, alice.fp, s.json, randomFrom(9));
  assert.equal(hex(note), s.note);
  assert.deepEqual(await fc.openStatus(epochKey, s.epoch, alice.fp, h(s.note)), { json: s.json, day: s.day });
  // another epoch, another identity, another key, a changed byte
  assert.equal(await fc.openStatus(epochKey, s.epoch + 1, alice.fp, h(s.note)), null);
  assert.equal(await fc.openStatus(epochKey, s.epoch, h(V.bob.fp), h(s.note)), null);
  const wrong = epochKey.slice();
  wrong[0] ^= 1;
  assert.equal(await fc.openStatus(wrong, s.epoch, alice.fp, h(s.note)), null);
  const changed = h(s.note);
  changed[30] ^= 1;
  assert.equal(await fc.openStatus(epochKey, s.epoch, alice.fp, changed), null);
  // a day more than a year on
  assert.equal(await fc.openStatus(epochKey, s.epoch, alice.fp, await fc.sealStatus(epochKey, s.epoch, 367, alice.fp, '{}')), null);
});

test('status boxes: where the note of a day is kept, named as the C++ names it', async () => {
  const sb = V.status_box;
  const epochKey = h(sb.epoch_key);
  const days = Object.keys(sb.days);
  assert.ok(days.length >= 2);
  for (const day of days) assert.equal(hex(await fc.statusBox(epochKey, Number(day))), sb.days[day]);
  // Another day, another key: another box
  assert.notEqual(hex(await fc.statusBox(epochKey, Number(days[0]) + 7)), sb.days[days[0]]);
  const other = epochKey.slice();
  other[31] ^= 1;
  assert.notEqual(hex(await fc.statusBox(other, Number(days[0]))), sb.days[days[0]]);
});

test('the backup file, sealed as the C++ does and opened only with its passphrase', async () => {
  const b = V.backup;
  assert.equal(await fc.sealBackup(b.passphrase, b.json, 100000, randomFrom(10)), b.text);
  assert.equal(await fc.openBackup(b.passphrase, b.text), b.json);
  assert.equal(await fc.openBackup(b.passphrase, b.text + '\r\n'), b.json);
  assert.equal(await fc.openBackup('Pass phrase', b.text), null);
  assert.equal(await fc.openBackup(b.passphrase, b.text.replace('PSRPT-BACKUP 1', 'PSRPT-BACKUP 2')), null);
  const changed = b.text.slice(0, 40) + (b.text[40] === 'A' ? 'B' : 'A') + b.text.slice(41);
  assert.equal(await fc.openBackup(b.passphrase, changed), null);
  // too few rounds, too many: refused before any work
  assert.equal(await fc.openBackup('p', await fc.sealBackup('p', '{}', 1000)), null);
  const head = 'PSRPT-BACKUP 1\n';
  const all = base64UrlDecode(b.text.slice(head.length));
  for (const rounds of [99999, 10000001]) {
    const patched = all.slice();
    new DataView(patched.buffer).setUint32(16, rounds);
    assert.equal(await fc.openBackup(b.passphrase, head + base64UrlEncode(patched)), null);
  }
});

test('the message a request to the server is signed over', () => {
  const r = V.request;
  assert.equal(str(fc.requestMessage(r.op, r.t, r.fields)), r.message);
  assert.equal(str(fc.requestMessage(r.op, r.t, new Map(Object.entries(r.fields)))), r.message);
  assert.equal(str(fc.requestMessage('status_put', '123', { note: 'N', fp: 'F' })), 'psrpt friends 1\nstatus_put\n123\nfp=F\nnote=N\n');
});
