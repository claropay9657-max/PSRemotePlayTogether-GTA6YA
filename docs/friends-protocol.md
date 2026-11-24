# Friends: the protocol

How the app, the browser page and the friends server (`server/friends/psrpt_friends.php`) talk.
`docs/friends.md` says what the feature is and why it is built this way; this file says exactly how,
so that the C++ client (`coplay/src/friends_crypto.cpp`), the JavaScript client
(`web/js/friends/crypto.js`) and the server agree to the byte. Both clients are held to the vectors
in `test/web/friends-vectors.json`, which `coplay/tools/friends-vectors.cpp` makes.

Private chats follow Signal's X3DH (https://signal.org/docs/specifications/x3dh/) and Double
Ratchet (https://signal.org/docs/specifications/doubleratchet/), groups its Sender Keys, with the
changes this file names. Where this file is silent, those documents decide.

Version of this protocol: 1.

## Encoding

- Integers are big-endian. `u8`, `u16`, `u32`, `u64`.
- `bytes8` / `bytes16`: a length (`u8` / `u16`) and that many bytes. `str8` / `str16`: the same with
  UTF-8 text.
- On the wire and in links, binary data is base64url without padding (as invites are).
- `||` joins byte strings. A label like `"psrpt card"` is its ASCII bytes, without a terminator.
- JSON inside sealed payloads is UTF-8, without a byte order mark; unknown keys are ignored.

## Primitives

| Name | What |
| --- | --- |
| P-256 | the NIST curve (secp256r1) |
| public key | the uncompressed point: `0x04 || x || y`, 65 bytes (WebCrypto's "raw") |
| private key | the scalar `d`, 32 bytes big-endian; where a private key is stored or handed on, it travels as `d || public key` (97 bytes), because a browser can only import it with its point (as JWK). In a browser it is kept as a non-extractable `CryptoKey` |
| `DH(a, B)` | ECDH: the x coordinate of `a·B`, 32 bytes |
| `Sign(k, m)` / `Verify` | ECDSA with SHA-256; the signature is `r || s`, 64 bytes (WebCrypto's format; OpenSSL's DER is converted) |
| `HKDF(salt, ikm, info, n)` | HKDF-SHA256, `n` bytes out; an empty salt is 32 zero bytes |
| `HMAC(k, m)` | HMAC-SHA256, 32 bytes |
| `Seal(k32, nonce12, ad, pt)` | AES-256-GCM, the 16 tag bytes at the end |
| `H(m)` | SHA-256 |
| `Rand(n)` | `n` bytes from the system's random generator |

## Identity

- An identity is two key pairs: `IKs` (ECDSA) and `IKd` (ECDH).
- `ikd_sig = Sign(IKs, "psrpt ikd" || IKd.pub)`.
- **Fingerprint** `FP = H("psrpt id" || IKs.pub || IKd.pub)[0..16]`: 16 bytes, 22 characters in
  base64url. It names the identity everywhere.
- **Link key** `LK = Rand(16)`. **Friend link**: `FP` and `LK` in base64url, joined by a dot:
  `https://<page>/#f=<FP>.<LK>` and `psrpt://friend/<FP>.<LK>`; `?at=<server>` follows for a server
  other than the default, as with invites.
- **Request box** `RB = H("psrpt requests" || LK)[0..16]`. A new link key is a new request box.
- **Safety code** of a friendship: the two fingerprints in byte order, `lo` and `hi`;
  `h = H("psrpt safety" || lo || hi)`; five groups of five digits, group `i` being
  `u32(h[4i..4i+4]) mod 100000` with leading zeros. Both friends see the same code.

## Devices

- `device_id = Rand(8)`.
- **Signed prekey**: an ECDH key pair `SPK` with `spk_id: u32` (1 and counting), renewed weekly; the
  one before stays usable for 30 days.
  `spk_sig = Sign(IKs, "psrpt spk" || FP || device_id || u32 spk_id || SPK.pub)`.
- **One-time prekeys**: ECDH key pairs `OPK` with `opk_id: u32`, about 50 on the server at a time;
  the device makes more when fewer than 20 are left.
- **Inbox** of a device: `IB = H("psrpt inbox" || FP || device_id)[0..16]`.

## The card

What the server keeps under `FP`, and what anybody may fetch:

```
u8      version = 1
bytes   IKs.pub (65)
bytes   IKd.pub (65)
bytes   ikd_sig (64)
u32     seq                 counts up with every change
u8      device count (1..10)
  per device:
  bytes   device_id (8)
  u8      kind              1 the app, 2 a web browser
  u32     spk_id
  bytes   SPK.pub (65)
  bytes   spk_sig (64)
bytes16 profile             sealed, may be empty (below)
bytes   card_sig (64)       Sign(IKs, "psrpt card" || everything above)
```

**Profile**: `pk = HKDF(-, LK, "psrpt profile", 32)`, `nonce = Rand(12)`,
`profile = nonce || Seal(pk, nonce, FP, json)` with `json` = `{"name": "...", "avatar": "<base64 JPEG,
at most 4 KB>"}`. Whoever has the link (or is a friend, who got `LK`) can read it; the server cannot.
The picture is small on purpose: a card has to fit in an inbox note (8 KB) with room to spare.

`kind` is 1 or 2; a card with any other device kind is refused, as is one with a wrong signature.

## Starting a conversation (X3DH)

A conversation is between one device of A and one device of B.

1. A fetches B's **bundle**: B's card and, for each of B's devices, one one-time prekey (`opk_id`,
   `OPK.pub`), which the server then deletes. A checks `ikd_sig`, `card_sig` and `spk_sig`.
2. A makes an ephemeral key pair `EK` and computes
   - `DH1 = DH(IKd_A, SPK_B)`, `DH2 = DH(EK_A, IKd_B)`, `DH3 = DH(EK_A, SPK_B)`,
     `DH4 = DH(EK_A, OPK_B)` if there was a one-time prekey;
   - `SK = HKDF(-, 0xFF×32 || DH1 || DH2 || DH3 [|| DH4], "psrpt x3dh", 32)`;
   - `AD = FP_A || device_A || FP_B || device_B`.
3. A starts the Double Ratchet with `SK` and B's `SPK.pub` as B's first ratchet key, and sends its
   first message as a **first message** (below).
4. B finds `spk_id` and `opk_id` in it, computes the same `SK`, deletes that one-time prekey, and
   starts its ratchet with `SK` and its `SPK` key pair.

Signal's X3DH uses 32 bytes of `0xFF` before the key agreements for X25519; this protocol does the
same for P-256.

## The Double Ratchet

As in Signal's specification, with these choices:

- `KDF_RK(rk, dh) = HKDF(rk, dh, "psrpt ratchet", 64)`: the new root key is the first 32 bytes, the
  new chain key the last 32.
- `KDF_CK(ck)`: the next chain key is `HMAC(ck, 0x02)`, the message key `HMAC(ck, 0x01)`.
- Sealing a message with message key `mk`: `x = HKDF(-, mk, "psrpt message", 44)`, key `x[0..32]`,
  nonce `x[32..44]`; `Seal(key, nonce, AD || header, payload)`.
- **Header**: `bytes ratchet.pub (65) || u32 pn || u32 n`.
- At most 1000 skipped message keys per conversation, each kept at most 30 days.
- The ratchet's state is saved before a message is sent, and after one is opened, so that a crash
  never makes a device send twice with the same key.

## Envelopes

What goes into an inbox, a request box or the sender's own other devices' inboxes:

```
u8      version = 1
u8      kind                1 first message, 2 message
bytes   FP of the sender (16)
bytes   device_id of the sender (8)
bytes   device_id of the receiver (8)
kind 1 only:
  bytes   EK.pub (65)
  u32     spk_id
  u32     opk_id            0: none
bytes   header (73)
bytes16 sealed payload
```

A message for a friend with three devices is three envelopes, each sealed in its own conversation;
a message also goes to the writer's other devices, so that every device shows the whole chat.
A payload is at most 65 519 bytes (the sealed text with its tag has to fit the two length bytes).

**Both sides at once.** When two devices start a conversation with each other at the same moment,
each answers the other's X3DH and keeps its own as well. A device keeps up to four conversations
per other device, the newest first: a message is opened with the first one that opens it, which
then becomes the newest and the one used for sending. Both sides end up in the same conversation
after one message each way.

**The sender's card** is fetched (`card_get`) before a first message (`kind` 1) is opened: X3DH
needs the sender's `IKd`, and a request from a stranger carries no card of its own.

## Payloads

Sealed payloads are JSON objects with a type `t` and an id `id` (16 random bytes in base64url, to
find duplicates) and `at` (the writer's clock, milliseconds since 1970):

| `t` | What | Fields |
| --- | --- | --- |
| `request` | asks to become friends | `lk` (the sender's link key, base64url), `text` (at most 200 characters), `token` (a write token for the sender's inboxes) |
| `accept` | says yes | `lk`, `token`, `fk` (the friends key, below: `key`, `epoch`) |
| `decline` | says no | nothing; the sender's app forgets the request |
| `fk` | a new friends key | `key`, `epoch` |
| `text` | a chat message | `chat` (a friend's FP or a group id), `text` (at most 4000 bytes), `reply` and `quote` (the message it answers, below), `embed` (a link preview, below) |
| `join` | a join card | `chat`, `invite`, `title`, `game`, `console` |
| `plan` / `vote` | agreeing on a time | `chat`, `plan` (id), `times` (a list), `pick` (an index) / `answers` |
| `seen` | read up to here (a read receipt; the apps show each reader's picture under the newest message they have read or written, as Instagram does) | `upto` (the newest message's id), `upto_at` (its `at`); in a group sent as a group message. A player with read receipts off sends none (only to the own other devices, inside `sync`) and shows none |
| `senderkey` | a group sender key | `group`, `chain` (u32), `iter` (u32), `ck` (hex), `pub` (the public signing key) |
| `group` | a group's version | as in "Groups" below: `group`, `version`, `name`, `add`, `members`, `admins`, `banned`, `by`, `sig`, `names`, and `token` (members), `owner` (admins) |
| `sync` | for the writer's own other devices | `of` (the payload sent to a friend), `to` (the friend's FP) |
| `unfriend` | ends a friendship | nothing |
| `lk` | the sender's new link key (after **New friend link**), so friends can still open the profile | `lk` |
| `own` | to the writer's own other devices: a friend's record changed, or the own friends key or link key | `friend` (the record: `fp`, `state`, `lk`, `token`, `my_token`, `fk`, `checked`, …; `removed: true` if it went), `fk`, `lk` |
| `card` | the sender's card changed (a device came): fetch it again before the next message | nothing |
| `leave` | to every member of a group: the sender leaves; an admin takes them out | `group` |
| `delete` | deletes one of the writer's own messages for everybody (1:1 as a payload, in a group as a group message, to the own devices inside `sync`) | `msg` (the message's `id`). A client deletes it only if that message is the sender's own; it keeps a stand-in ("Message deleted") in its place, without text, preview, card, plan, votes, reactions or quote |
| `react` | a reaction to a message, given or taken back (1:1 as a payload, in a group as a group message, to the own devices inside `sync`) | `msg` (the message's `id`), `emoji` (one emoji, at most 16 bytes of UTF-8, the same rule as a session's chat: `coplay::ValidReaction`), `on`. At most 3 different ones per player and 12 per message |

**Replies**: a `text` that answers another message carries `reply` (that message's `id`, at most 64
characters) and `quote` = `{"from": FP, "text": "..."}`, a copy of the start of what it answers (at
most 160 bytes of UTF-8, on one line; a card or plan gives its `text`). A client shows the quote from
the message itself while it has it (a deleted one as "Deleted message"), else from the copy, else as
"A message from earlier"; it keeps `reply` only as a string of at most 64 characters, and `quote`
only with both fields, the text cut to its bytes. Clicking a quote goes to the message.

**Mentions** are text: `@` and a member's name as the chat shows it. A message mentions a player if
it has `@` and their own name (as they call themselves), any case, at the start of the text or after
a space or punctuation, and not followed by a letter or digit. The apps offer the members' names as
soon as `@` is typed in a group (and in a session's chat), draw a member's name after `@` in the
accent colour, and mark a message that mentions the player.

**Muting** is the player's own: a chat or group muted (for an hour, for 8 hours, or until turned
back on) shows no notifications and does not count in the unread total, unless a message mentions
the player. Each device keeps its own (nothing is sent).

A link preview (`embed`) is made by the writer's app as in a session's chat: only it opens the link,
once, and sends what it found sealed with the message, so the others never contact the site. It
carries `url`, `site`, `title`, `description` and `image` (a JPEG as base64, at most 5600 bytes and
800 pixels a side). The writer's app fits it to the note: the message with its preview stays under
7000 bytes of JSON, the picture gets whatever room is left (none below 1200 bytes, the texts alone
then), and a message too long for the texts goes without one. A client keeps a preview only for a
link that is in the message itself (as typed, or percent-encoded as the writer's app wrote it), cuts
the texts to the session chat's limits in bytes of UTF-8 (`site` 48, `title` 100, `description` 160,
`url` at most 256), takes the picture only if its header says JPEG and at most 800 pixels a side
(read before it is decoded), and keeps none that has nothing to show but the link. A browser makes
no previews (a page cannot look at other sites) but shows the ones apps make. The app's switch
**Preview links I post** (Settings → Voice & webcam) covers both chats.

The plan and vote fields in detail: a `plan` carries `plan` = `{"title": "...", "times": [ms, ...]}` (1 to 8
times) and `text` (for whoever shows only text); a `vote` carries `plan` (the plan's `id`) and `answers`, one
number per time (0 no, 1 maybe, 2 yes); a newer vote of the same player replaces the one before. A
`senderkey` carries `group`, `chain`, `iter`, `ck` (hex), `pub` (the public signing key, base64url). A
`join` carries `join` = the session card: `invite`, `web`, `title`, `host`, `game`, `cover`, `console`,
`policy`, `control`, `players`, `max`, `talk`; a client keeps only these, and a `cover` only on
`https://image.api.playstation.com/` (any other address would tell its owner who looked).

## Status

- **Friends key**: `FK_epoch = Rand(32)`, sent to every friend as `fk` when they become friends and
  whenever somebody is removed or blocked (a new epoch). It moves on once a day:
  `FK_day(d+1) = HMAC(FK_day(d), "psrpt next day")`, `FK_day(0) = FK_epoch`, days counted in UTC from
  the epoch's start. A stolen day key reads that day and the ones after until the next epoch; the
  statuses before it are long gone from the server.
- **Status note**: `nonce = Rand(12)`, `note = u32 epoch || u32 day || nonce || Seal(FK_day, nonce, FP, json)`
  with `json` = `{"state": "online|away|busy", "web": bool, "activity": "...", "room": {"invite",
  "title", "game", "console", "players", "max"} or null, "free": "...", "at": ms}` (the `room` is a
  session card as in `join`). Invisible, or shown to nobody: an empty `status_put` takes the note
  away at once. A friend takes a note only if its `at` is within 10 minutes of their own clock.
- **Where it is kept**: under the status box `SB = H("psrpt status box" || FK_epoch || u32 utc_day)[0..16]`
  with `utc_day` = unix milliseconds / 86 400 000. Only friends (who have the friends key) can work out
  the name, so a fingerprint alone tells nobody when its player is online. Friends ask for today's
  and yesterday's box (clocks differ around midnight).

## Groups

- `group_id = Rand(16)` with its top bit set (`group_id[0] |= 0x80`). **Group box**
  `GB = H("psrpt group" || group_id)[0..16]`.
- **Owner key**: an ECDSA key pair made with the group, given to its admins in the `group` payload.
  The server knows its public half and lets only it change the group box's write token
  (`group_token`, signed like any request but with the owner key and without `fp`).
- **Write token**: `GT = Rand(16)`; the server keeps `H(GT)`. Writing needs the current token;
  reading also takes one of the 8 tokens before it, for 30 days, so that a member who had not looked
  yet when an admin made a new one still finds the new one, sealed for them.
- **What the box holds**: group messages (byte 1 is the group id's first byte, top bit set) and
  envelopes between members (byte 1 is the envelope's kind, 1 or 2). The envelopes carry what goes
  from one member to another: sender keys, the group's new versions, `leave`. Members need not be
  friends: an envelope in the box is opened by the device it names, after X3DH with the sender's card.
- **Sender key** of a device: chain key `CK = Rand(32)`, `chain: u32` (counting), a signing key pair
  `SSK` (ECDSA); sent to every member's device (and the own other ones) in a `senderkey` payload
  through the box. Message keys: `mk = HMAC(CK, 0x01)`, next `CK = HMAC(CK, 0x02)`, as the ratchet's
  chains. A device makes a new one whenever who is in the group changes, and after a week.
- **Group message**:
  ```
  u8      version = 1
  bytes   group_id (16)
  bytes   FP of the sender (16), bytes device_id (8)
  u32     chain, u32 iter
  bytes16 sealed            x = HKDF(-, mk, "psrpt group message", 44);
                            Seal(x[0..32], x[32..44], group_id || FP || device_id || chain || iter, json)
  bytes   sig (64)          Sign(SSK, everything above)
  ```
  `json` is a `text`, `join`, `plan` or `vote` payload. A message of a chain whose key has not come
  yet waits (7 days, 200 messages at most); one of an older chain is over.
- **A version of the group** (`group` payload): `group`, `version` (counting up), `name`, `add`
  (`admins` or `all`: who may add people), `members`, `admins`, `banned` (fingerprints), `by` (who
  signed), `sig`, `names` (what the members are called, for those who are no friends of each other;
  not signed, only shown), and for members `token`, for admins also `owner` (the owner key's private
  scalar, hex).
  `sig = Sign(IKs of by, "psrpt group" || group_id || u32 version || u16 name length || name (UTF-8)
  || u8 add (1: all) || u8 n || members || u8 n || admins || u8 n || banned)`, each list sorted by its
  bytes. Every admin is a member; at most 50 members.
- **Who may change it**: an admin of the version a device knows (for a group new to it, an admin of
  the version itself); where `add` is `all`, also a member whose version only adds people who are not
  banned and changes nothing else. The version goes from `by` itself (never forwarded) and is checked
  with `by`'s card. A newer version replaces an older one; a device that is no longer in it stops
  reading, forgets its sender keys and says so in the chat.
- **The first version** reaches a new member through their inbox (they are a friend of whoever adds
  them); everything after that through the box. A group new to a device is taken only from a friend,
  and only if that device's player is in it.
- **Versions** count up by at most 16 at a time and stay below 2^31. A version's `token` and `owner`
  count only when `by` is an admin; a member who only adds somebody hands on neither.
- **One device acts** where two would clash: a member's `leave` is carried out by the admin with the
  lowest fingerprint, on that identity's device with the lowest id.
- **Owner key**: whenever an admin is taken out, stops being one or leaves, the remaining admins make
  a new one (`group_owner`, signed with the one before) before anything else.
- **Removing or banning**: the admin makes a new write token (`group_token`), then a new version
  without them (banned ones in `banned`), and every remaining device makes a new sender key. The one
  taken out gets that version without the token. **Leaving**: an admin leaves with a new version of
  their own (the last admin hands it on to the longest member); a member sends `leave` to everybody
  and an admin takes them out.

## Linking a device

1. The device that has the identity (side 1) makes `L = Rand(16)` and the link box name
   `LB = H("psrpt link" || L)[0..16]`, and shows `psrpt://link/<L in base64url>` (with `?at=<server>`
   for a server other than the default) to copy or type on the new device.
2. Side 1 puts `H(E1)` (the hash of an ephemeral ECDH public key) as its part 0. Side 2 waits for it,
   then puts its own key `E2` as its part 0. Side 1, having read `E2`, puts `E1` as its part 1; side 2
   checks it against the hash. Neither side can choose its key after seeing the other's, so nobody
   who learns `L` can try keys until the digits match.
3. `K = HKDF(L, DH(e_self, E_other), "psrpt link key", 32)`. Both show the six digits
   `u32(H("psrpt link code" || K)[0..4]) mod 1000000` as `123 456`; the player compares them, and only
   on side 1 confirms.
4. Side 1 puts `nonce || Seal(K, nonce, LB, bundle)` (the bundle as in a backup) in parts of at most
   192 KB, numbered from 2: part 2 starts with `u16 n` (how many parts there are) and is put last,
   parts 3 to n + 1 before it. Side 2 reads part 1, then the others, opens it, becomes a new device of the identity (its own
   device id and prekeys, the card with itself added, `card` to every friend) and takes the box away
   (an empty `link_put`).

The private identity keys are the one thing a browser keeps extractable (`IKs`, `IKd`, and a
group's owner key): linking and backups carry them. Every other private key a page makes is not.

## Backup file

```
PSRPT-BACKUP 1
<base64url of: bytes salt (16) || u32 iterations || bytes nonce (12) || sealed>
```
`key = PBKDF2-SHA256(passphrase, salt, iterations, 32)`; written with 600 000 iterations, opened with
anything from 100 000 to 10 000 000 (fewer would make a weak key, more would hang the app);
`sealed = Seal(key, nonce, "psrpt backup", json)` with the same `bundle` as linking, but no device:
the client's whole state (JSON) without `device`, `sessions` and `bundle_opks`, and without the
groups' `mine`, `chains` and `waiting`. Its `identity` holds the private scalars of `IKs` and `IKd`
(hex, 32 bytes each; the public points are worked out again), `fp` the fingerprint they have to give,
`lk` the link key, `fk` the friends key, `friends`, `groups`, `chats` (the history) and `settings`.
A restore keeps the server the device uses, not the one in the bundle.
a restored copy is a new device of the identity, with its own device id, prekeys and conversations.
The first line may end in `\r\n` (a file an editor saved on Windows). The passphrase is taken as
UTF-8 after Unicode normalisation (NFC), at least 8 characters.

## The server

One PHP file. Requests are `POST`, `application/x-www-form-urlencoded`; answers are JSON with
`Content-Type: application/json`, `Cache-Control: no-store`, the CORS header the setting allows (as
the meeting point), and always `"ok": true|false`, `"poll": ms` (how often to come back), and on
failure `"error": "..."`.

### Signed requests

Requests that change what belongs to an identity carry `fp` (base64url), `t` (unix seconds) and
`sig = Sign(IKs, "psrpt friends 1\n" || op || "\n" || t || "\n" || for each other field, sorted by
name: name "=" value "\n")`. The fields signed are all form fields but `v`, `op`, `t` and `sig`
(so `fp` is one of them), with their values as sent (base64url text, not decoded). The server checks the signature with the card's `IKs`, `t` within 5
minutes, and that it has not seen the same signed message (`op`, `t` and the fields) in the last 10
minutes. It looks at the message, not at the signature, because an ECDSA signature can be changed
into another valid one for the same message.

Every signed request also carries a nonce `n`: 8 random bytes, base64url (11 characters), signed like
every other field. Without it two requests with the same fields in the same second (a poll and a
refresh) would be one message, and the second would be refused as `replay`. The server takes a
request without `n` too; one whose `n` is not base64url is malformed. `group_token` is signed the
same way with the group's owner key instead of `IKs`, and carries no `fp`.

### Operations (`op`)

| `op` | Signed | Fields | What it does |
| --- | --- | --- | --- |
| `card_put` | yes (with the uploaded card's IKs) | `card` | keeps the card if it is valid, `FP` matches and `seq` is larger |
| `card_get` | no | `fp` | the card |
| `bundle_get` | no | `fp`, optional `devices` (comma-separated device ids) | the card and one one-time prekey per device (taken away); with `devices` only for those |
| `prekeys_put` | yes | `device`, `keys` (`u32 id || pub` repeated) | adds one-time prekeys, at most 100 kept |
| `requests_set` | yes | `box` | the identity's request box from now on (the old one goes) |
| `request_put` | no | `box`, `note` | drops an envelope into a request box; a few per hour from one address |
| `token_add` / `token_remove` | yes | `hash` | a write token's hash for the identity's inboxes |
| `inbox_put` | no | `fp`, `device`, `token`, `note` | drops an envelope into a device's inbox if `H(token)` is one of the identity's |
| `inbox_get` | yes | `device`, (`box` for the request box) | the notes waiting, with their ids; for a device inbox also `prekeys`, how many one-time prekeys of it are left |
| `inbox_ack` | yes | `device` or `box`, `ids` | takes them away |
| `status_put` | yes | `note`, `box` (the status box; not with an empty `note`) | the identity's status note under that box, for 3 minutes; the box before goes. An empty `note` takes it away |
| `status_get` | no | `boxes` (comma-separated, at most 400) | the fresh notes among them |
| `group_new` | no | `box`, `owner` (public key), `hash` | a new group box |
| `group_token` | signed by the owner key | `box`, `hash` | a new write token |
| `group_owner` | signed by the owner key | `box`, `owner` (public key) | a new owner key (when an admin goes); 20 changes of tokens and owners per box and address in an hour |
| `group_put` | no | `box`, `token`, `note` | a group message |
| `group_get` | no | `box`, `token`, `after` | messages after that id, at most 200 |
| `link_put` / `link_get` | no | `box`, `side`, `part`, `data` | the link box, 2 minutes |
| `push_key` | no | | the server's VAPID public key |
| `push_set` | yes | `device`, `endpoint`, `p256dh`, `auth`, optional `groups` (comma-separated `box.token`: only a token that reads the box counts) | a browser's push subscription; empty `endpoint` removes it |
| `report` | no | `text` (what the reporter chose to send, opened) | kept for the operator |

Every write is refused for an identity on the operator's ban list (`banned.txt` next to the data,
one fingerprint per line).

### Answers

Besides `ok` and `poll`. Binary values are base64url, as in requests.

| `op` | Answer when `ok` is true |
| --- | --- |
| `card_put` | `"seq"`: the seq now kept |
| `card_get` | `"card"` |
| `bundle_get` | `"card"`, `"prekeys": {"<device id>": {"id": <u32>, "pub": "<65 bytes>"}, ...}`: a device with no one-time prekey left is missing (X3DH then goes without one) |
| `prekeys_put` | `"prekeys"`: how many the device has on the server now |
| `token_add` / `token_remove` | `"tokens"`: how many the identity has now |
| `inbox_get` | `"notes": [{"id": "<text>", "note": "<envelope>"}, ...]` oldest first, `"more"`: true when not all fitted (at most 200 notes, about 2 MB, per answer); for a device inbox also `"prekeys": n` |
| `inbox_ack` | `"removed"`: how many went |
| `status_get` | `"notes": {"<box as asked>": "<status note>", ...}`, the fresh ones only |
| `group_put` | `"id"`: the message's id (a number counting up per box) |
| `group_get` | `"notes": [{"id": <number>, "note": "<group message>"}, ...]` with ids above `after`, oldest first, `"more"` as above |
| `link_get` | `"parts"`: the numbers of the parts `side` has put, and with `part` also `"data"` (that part, or null) |
| `push_key` | `"key"`: 65 bytes |
| `push_set` | `"groups"`: how many of the group boxes asked for are there |
| the others | nothing more |

A refusal has `"ok": false` and `"error"`, one word: `malformed` (HTTP 400: what no client sends),
`version`, `unknown_op`, `unknown` (no such identity, request box or group box), `unknown_device`
(not on the card), `bad_card`, `old_card` (with the `"seq"` kept), `bad_signature`, `stale_time`
(with the server's `"now"`, to set a clock by), `replay`, `bad_token`, `bad_key` (no point on
P-256, or prekey id 0), `box_taken`, `box_full`, `too_many` (write tokens), `rate_limited` (with
`"retry"` in seconds), `banned`, `not_allowed` (a push endpoint that is no push service), and
`cannot_write` / `server_full` (HTTP 503), and `too_big` (a note larger than its box takes: a long message,
answered with HTTP 200). Everything else an honest client can run into is answered
with HTTP 200, so that reverse proxies that ban addresses after 4xx answers do not ban players.

Details:

- `card_put` carries `fp` like every signed request; it has to be the card's own `FP`. The server
  checks the card as `DecodeCard` does, and refuses two devices with one `device_id`. When a card
  leaves out a device, its prekeys, inbox and push subscription go.
- `prekeys_put`: `opk_id` 0 is refused (it means "none" in a first message); when more than 100
  would be kept, the oldest go.
- `requests_set` with an empty `box` leaves the identity without one. A box another identity has is
  refused. The old box goes, with the requests still in it.
- `inbox_put` and `request_put` refuse an envelope whose sender (bytes 2 to 18) is banned,
  `group_put` a group message whose writer (bytes 17 to 33) is banned. A write token is checked as
  `H(token)` over the token's bytes (not its base64url text).
- `group_put` keeps the newest 2000; the oldest go. A note whose byte 1 is 1 or 2 is an envelope (its
  writer at bytes 2 to 18), any other a group message (its writer at bytes 17 to 33).
- `report`: `text` is JSON: `why`, `by` (the reporter's FP), `group` (a group chat or not), and
  `messages`, each `from`, `at`, `text`, as the reporter's app shows them. Only what the player chose.
- Link boxes: `side` is 1 (the old device) or 2 (the new one), `part` a number from 0 to 999 (the
  ephemeral key as part 0, then the chunks), `data` at most 256 KB and what sealing adds. `link_put`
  writes the part of its own `side`; `link_get` reads the parts of the `side` it names, the other
  device's. A `link_put` with empty `data` removes the whole box. A box stays 2 minutes after its last
  write, at most 4 MB.
- `push_set`: `groups` (optional, comma-separated group box names, at most 200) are the group boxes
  whose messages should wake this browser too; the server then knows that this identity is in them.
  Push messages go only to the hosts of known push services (a setting of the server), over HTTPS.

### Push (web only)

When something is dropped into an inbox, a request box or a group box whose device has a push
subscription, the server sends an **empty** Web Push message to its endpoint: `TTL: 86400`, no body,
`Authorization: vapid t=<JWT>, k=<server's public key>` with an ES256 JWT for the endpoint's origin
(`aud`), expiring in 12 hours. The server makes its VAPID key pair once and keeps it in its data
folder; `op=push_key` answers with the public key for the page's subscription.

### Limits

As in `docs/friends.md`: cards a year after the last use, one-time prekeys until used (100 per
device), request boxes 50 notes for 14 days, inboxes 1000 notes of at most 8 KB for 30 days, status
notes 3 minutes, group boxes 30 days and 2000 notes, link boxes 2 minutes and 4 MB, reports 90 days.

## Test vectors

`coplay/tools/friends-vectors.cpp` writes `test/web/friends-vectors.json` from fixed keys and fixed
"random" bytes: fingerprints, link and box names, a card and its signatures, an X3DH (with and
without a one-time prekey), a run of ratchet messages in both directions, a
sender-key chain, a status note, the six-digit link code and a backup file. Messages out of order,
skipped keys and their limits are checked by the tests on each side rather than by the vectors. `coplay/test` checks the C++ side
against them, `test/web/friends.test.mjs` the browser's. ECDSA signatures are random each time (in
WebCrypto too), so the vectors carry signatures to verify, not to make again; everything else,
nonces included, is fixed and has to come out to the byte.
