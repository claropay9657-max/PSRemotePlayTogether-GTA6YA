# The friends server

`psrpt_friends.php` keeps what friends need to find and reach each other while
no session runs: every player's **card** (their public keys and devices), the
**prekeys** that let a friend start a conversation with a device that is
offline, and small sealed notes in boxes: friend requests, chat messages, group
messages, statuses, and what two devices hand each other while one links the
other. How the feature works is in [docs/friends.md](../../docs/friends.md), the
exact formats and every operation in
[docs/friends-protocol.md](../../docs/friends-protocol.md).

It **cannot read** any of it. Names, pictures, statuses and messages are sealed
end to end between the players' devices; the server keeps sealed bytes in boxes
with random names. There are no accounts and no passwords: a player is a key
pair their app or browser makes. The server checks signatures, so that nobody
but a player's own devices can change their card, prekeys, request box, tokens
or status, and nobody but a group's admins can change who may write to it.

Nothing of a session passes through it: picture, sound and controllers still go
directly between the players.

The app and the page use the one on `gamernova.net` unless a player names
another under **Settings → Network**, or switches friends off. You only need
this folder if you run that one, or want one of your own.

## What it needs

- PHP 7.4 or newer with the **openssl** extension, behind any web server, with
  HTTPS. **curl** is used for push messages when it is there; without it PHP's
  own streams send them. No database, nothing else.
- A folder PHP can write to that is **not** served to the web, and that
  **lasts**: cards are kept for a year and undelivered messages for 30 days. Set
  `PSRPT_FRIENDS_DIR` (as an environment variable, in `psrpt_friends.config.php`
  next to the script, below, or the constant at the top of the file). The script
  puts an `.htaccess` with `Require all denied` into the folder, a second lock on
  Apache should the folder end up in the web root. Without it the script uses `psrpt-friends` in the system's
  temporary folder, which is fine for trying it out and wrong for a real server:
  some systems empty that folder when they restart, and then every player's card
  and every waiting message are gone (the apps put their cards and prekeys back
  by themselves; the messages are lost).
- One web server instance. The data are files on that machine with file locks;
  two servers behind a load balancer would each know half of them.
- A file system with `flock` (any local one; not NFS without lock support).

## Installing

1. Copy `psrpt_friends.php` to the web server, for example as
   `https://example.org/psrpt_friends.php`, and set `PSRPT_FRIENDS_DIR` to a
   folder outside the web root that the PHP user can write to.
2. Open that address in a browser. It answers:

       {"ok":true,"poll":30000,"info":"PSRemotePlayTogether friends server. Nothing to see here: the app talks to it."}

3. Optional: a cron job that runs `php psrpt_friends.php sweep` once an hour
   (with the same `PSRPT_FRIENDS_DIR`). The script also sweeps by itself now and
   then; the cron job only keeps a quiet server tidy.
4. In the app, under **Settings → Network**, name
   `example.org/psrpt_friends.php` as the friends server. Friend links made
   after that carry the address (`...?at=example.org/psrpt_friends.php`), so
   friends need not enter anything.

The checks in [test/server/friends-server.test.php](../../test/server/friends-server.test.php)
start PHP's built-in server with this file and go through every operation:

    php test/server/friends-server.test.php

## Settings

Each one is a constant at the top of the file and can also be set as an
environment variable of the same name, or in a file `psrpt_friends.config.php`
next to the script that returns them as an array (an environment variable wins
over the file, the file over the constant). With the file, a newer
`psrpt_friends.php` can replace the old one as it is:

    <?php
    return [
        'PSRPT_FRIENDS_DIR' => '/var/lib/psrpt-friends',
        'PSRPT_FRIENDS_CLIENT_HEADER' => 'X-Real-IP',
    ];

| Setting | Default | What it is |
| --- | --- | --- |
| `PSRPT_FRIENDS_DIR` | system temp + `psrpt-friends` | The data folder (above). |
| `PSRPT_FRIENDS_POLL_MS` | 30000 | How often devices are asked to come back. Raise it to slow everybody down. |
| `PSRPT_FRIENDS_CORS` | empty | `Access-Control-Allow-Origin` for the browser page (below). |
| `PSRPT_FRIENDS_CLIENT_HEADER` | empty | The header a reverse proxy passes the client's address in (below). |
| `PSRPT_FRIENDS_BANNED` | `banned.txt` in the data folder | The ban list. |
| `PSRPT_FRIENDS_CARD_DAYS` | 365 | Days a card stays when none of its devices has been here. |
| `PSRPT_FRIENDS_MAX_CARD` | 49152 | Largest card, bytes (ten devices and a picture of 24 KB). |
| `PSRPT_FRIENDS_IDENTITIES_PER_HOUR` | 10 | New identities from one address in an hour. |
| `PSRPT_FRIENDS_MAX_PREKEYS` | 100 | One-time prekeys kept per device. |
| `PSRPT_FRIENDS_BUNDLES_PER_HOUR` | 200 | Bundles one address may fetch in an hour (each takes prekeys away). |
| `PSRPT_FRIENDS_REQUEST_DAYS` | 14 | Days a friend request waits. |
| `PSRPT_FRIENDS_MAX_REQUESTS` | 50 | Requests waiting in one request box. |
| `PSRPT_FRIENDS_MAX_REQUEST_NOTE` | 65536 | Largest request, bytes (it carries the sender's card). |
| `PSRPT_FRIENDS_REQUESTS_PER_HOUR` | 30 | Requests one address may drop in an hour; a request to a player with three devices is three. |
| `PSRPT_FRIENDS_INBOX_DAYS` | 30 | Days an undelivered message waits. |
| `PSRPT_FRIENDS_MAX_INBOX` | 1000 | Messages waiting in one device's inbox. |
| `PSRPT_FRIENDS_MAX_NOTE` | 8192 | Largest message in an inbox or a group box, bytes. |
| `PSRPT_FRIENDS_MAX_TOKENS` | 1000 | Write tokens per identity (one per friend). |
| `PSRPT_FRIENDS_STATUS_SECONDS` | 180 | Seconds a status is fresh. |
| `PSRPT_FRIENDS_MAX_STATUS` | 2048 | Largest status note, bytes. |
| `PSRPT_FRIENDS_GROUP_DAYS` | 30 | Days a group message stays, and a group box nobody uses. |
| `PSRPT_FRIENDS_MAX_GROUP` | 2000 | Messages kept per group box; the oldest go. |
| `PSRPT_FRIENDS_GROUPS_PER_HOUR` | 20 | New group boxes from one address in an hour. |
| `PSRPT_FRIENDS_LINK_SECONDS` | 120 | Seconds a link box stays after its last write. |
| `PSRPT_FRIENDS_MAX_LINK` | 4194304 | Largest link box, bytes. |
| `PSRPT_FRIENDS_LINKS_PER_HOUR` | 10 | New link boxes from one address in an hour. |
| `PSRPT_FRIENDS_REPORT_DAYS` | 90 | Days a report is kept. |
| `PSRPT_FRIENDS_MAX_REPORT` | 65536 | Largest report, bytes. |
| `PSRPT_FRIENDS_REPORTS_PER_HOUR` | 5 | Reports from one address in an hour. |
| `PSRPT_FRIENDS_PUSH_HOSTS` | the push services of Chrome, Edge, Firefox, Safari | Where push messages may go (below). |
| `PSRPT_FRIENDS_PUSH_CONTACT` | the project's GitHub address | Who runs this server, for the push services: an `https:` or `mailto:` address. Put your own. |
| `PSRPT_FRIENDS_MIN_FREE_MB` | 100 | Below this much free disk, nothing new is taken (503). |
| `PSRPT_FRIENDS_TEST` | empty | Only for the project's tests; never on a real server. |

Fixed by the protocol: a signed request's time may be off by five minutes, a
signed message is remembered for ten, a part of a link box is at most 256 KB,
`status_get` takes at most 200 fingerprints, an answer carries at most 200 notes
(and about 2 MB; `"more": true` says there are more).

## What to tell a reverse proxy or web application firewall

- **Method and body:** `POST`, `application/x-www-form-urlencoded`. Most
  requests are under 2 KB; a card is up to 65 KB, a friend request up to 90 KB,
  a part of a link box up to 360 KB. Allow 600 KB (nginx:
  `client_max_body_size 1m` is enough). `GET` is answered with 200 and a line of
  JSON.
- **Rate limit:** per client address, at least 2 requests per second. A device
  asks every 30 seconds, more while a chat is open, and several devices and
  players can share one address.
- **No bot challenge** (JavaScript, cookie or captcha) on this path: the clients
  are an app and a script on a page. The app sends
  `User-Agent: PSRemotePlayTogether/<version>`.
- **Status codes:** 400 only for malformed requests, which no app sends.
  Everything an honest app can run into (a clock that is off, a request sent
  twice, a full box, a limit per address) is answered with 200 and
  `"ok": false`, so that a proxy that bans addresses after a number of 4xx
  answers only ever bans what deserves it. 503 means "cannot write" or "disk
  full".
- **Caching:** none. The script sends `Cache-Control: no-store`.
- **The client's address:** behind a proxy, every request seems to come from the
  proxy, and the limits per address would count all players together. Set
  `PSRPT_FRIENDS_CLIENT_HEADER` to the header your proxy sets: `X-Real-IP`, or
  `X-Forwarded-For` (the script takes its last address, the one your proxy
  added). Only set it when the proxy always sets or overwrites that header;
  otherwise clients could choose their own address for the counters.

## The browser page

The page for web browsers is served from GitHub Pages, the friends server from
somewhere else, and a browser only lets the page read the answers if they carry
`Access-Control-Allow-Origin`.

- If nothing in front of the script adds that header, set `PSRPT_FRIENDS_CORS`
  to `*`, or to the page's origin only (`https://crypto90.github.io`). `*` is
  safe here: there are no cookies, and nothing a page can read is secret.
- If your reverse proxy already adds it for the whole site, leave the setting
  empty. Two such headers count as none, and browsers would be locked out.

**Push messages** for players who only use a browser: when something lands in
a device's inbox, an identity's request box, or a group box a browser asked to
hear about, the server sends an **empty** Web Push message ("something is new")
to the browser's push service, signed with the server's own key (VAPID, an
ES256 JWT for the push service's origin, valid 12 hours, `TTL: 86400`). The
browser's service worker then fetches what is new and opens it itself. The
server makes its key pair the first time it needs it and keeps it in
`server.json` in the data folder (readable for its own user only);
`op=push_key` answers the public half. Keep that file when you move the server:
a new key means every browser has to subscribe again.

Push messages go only to the hosts in `PSRPT_FRIENDS_PUSH_HOSTS`, over HTTPS on
port 443, never to an address of a private network: a client cannot make the
server send requests anywhere else. The default list is
`fcm.googleapis.com` (Chrome, Edge on Android, most Chromium browsers),
`updates.push.services.mozilla.com` and `*.push.services.mozilla.com`
(Firefox), `*.notify.windows.com` (Edge on Windows), `web.push.apple.com` and
`*.push.apple.com` (Safari, and the page installed on an iPhone or iPad). `*`
allows any public host. A subscription the push service answers with 404 or 410
("gone") is removed. At most 50 push messages go out per request, after the
answer, so that the writer never waits for them.

## What it stores, and for how long

In the data folder, with every file named after a hash or the hex of a checked
value, never after anything a client wrote:

| What | Where | Kept |
| --- | --- | --- |
| A player's card, and what the server knows about it (seq, the public signing key, the device ids, the request box, hashes of the write tokens, when one of its devices was last here) | `ids/<fingerprint>/card`, `meta.json` | a year after the last signed request of its devices |
| One-time prekeys (public halves) | `ids/<fingerprint>/opk-<device>` | until fetched, at most 100 per device |
| Messages waiting for a device | `ids/<fingerprint>/inbox-<device>/` | until the device takes them away, at most 30 days |
| The status note | `ids/<fingerprint>/status.json` | 3 minutes |
| A browser's push subscription (its push service address and keys, and which group boxes it wants to hear about) | `ids/<fingerprint>/push-<device>.json` | until removed, or the push service says it is gone |
| Friend requests | `requests/<box>/` | 14 days, at most 50 |
| Group messages, the group's owner key (public) and the hash of its write token | `groups/<box>/` | 30 days, at most 2000; the box 30 days after its last use |
| Link boxes | `links/<box>/` | 2 minutes after the last write |
| Reports | `reports/` | 90 days |
| Hashes of signed requests seen (against replays) | `seen/<minute>/` | about 11 minutes |
| Counters per address | `rate/<hour>/` | the hour |
| The server's VAPID key pair and the key for the counters | `server.json` | for good |

Each of these is removed by the sweep when its time is up; what has expired is
never answered, swept or not. A card that is replaced takes away the prekeys,
inbox and push subscription of devices that are no longer on it.

## The ban list

`banned.txt` in the data folder (or where `PSRPT_FRIENDS_BANNED` says), one
fingerprint per line, in base64url as the app shows it (22 characters) or as 32
hex digits; `#` starts a comment:

    # 2026-11-23, reported for spam
    TI7QCRPk1BBQqR5DeBygpg

A banned identity can no longer put its card, prekeys, status, tokens, request
box or push subscription, nor read its inbox. Nobody gets its card or status any
more, and messages, requests and group messages that name it as their writer
are refused (an envelope names its writer in clear; one that lies about it
cannot be opened by the receiver). Takes effect with the next request; nothing
has to be restarted.

A banned player can make a new identity, but nobody finds it without a new
friend link, so a ban still keeps them away from the people who reported them.

**Reports** land in `reports/`, one text file each, with what the reporter's app
sent: the messages in question, opened on the reporter's device, and only with
the reporter's say. The server learns nothing else about them; the fingerprint
to ban is in the report.

## Privacy: what it can see, and what it cannot

- **It cannot read** names, pictures, statuses, messages, requests or what a
  linked device receives. They are sealed between the players' devices (Signal's
  X3DH, Double Ratchet and Sender Keys, on P-256). The profile on a card is
  sealed with the friend link's key, which the server never gets.
- **It cannot change** a card, its devices or its prekeys (they are signed by the
  identity key), nor slip a member into a group (membership is signed by the
  group's admins inside the sealed messages). It could withhold or delete notes,
  as any server can.
- **It sees** the public keys on cards; box names (hashes, which cannot be turned
  back into a link, a group or a device by name); sizes and times of notes; how
  often a device asks; which fingerprints a device asks the status of (its
  friends, without names); and the writer's fingerprint and device id in the
  clear header of envelopes and group messages. A browser that wants push
  messages for group boxes tells the server which ones, so the server can tell
  that its identity is in those groups.
- **It stores no IP addresses** and writes no log of its own. The limits per
  address are counters in files named by a keyed hash of the address, removed
  after the hour. Like any web server it sees the address a request comes from,
  and the web server in front of it may log that as it logs everything else.
- **Push services** (Google, Mozilla, Microsoft, Apple) learn that a browser got
  an empty message from this server at a certain time, nothing more.

## Answers

Every answer is JSON with `"ok"` and `"poll"` (milliseconds until a device should
ask again). Signed requests carry `fp`, `t`, a nonce `n` and `sig`; the server
checks the signature, that `t` is within five minutes of its own clock, and that
the same signed message has not come in the last ten minutes. A refusal carries
`"error"`, one word:

| `error` | Meaning |
| --- | --- |
| `malformed` | A field is missing or not what it should be (status 400). |
| `version` | A protocol version this file does not know. |
| `unknown_op` | An operation this file does not know (the app is newer). |
| `unknown` | No such identity (card), request box, group box. |
| `unknown_device` | The device is not on the identity's card. |
| `bad_card` | The card is not valid, or is of another identity than `fp`. |
| `old_card` | The server has a card with the same or a larger `seq` (answered with `"seq"`). |
| `bad_signature` | The signature does not match. |
| `stale_time` | `t` is off by more than five minutes (answered with the server's `"now"`). |
| `replay` | The same signed message came before; sign again with another `t`. |
| `bad_token` | The token does not open that inbox or group box. |
| `bad_key` | A public key that is not a point on P-256 (or a prekey id 0). |
| `box_taken` | The request box or group box belongs to somebody else. |
| `box_full` | The box holds as much as it may. |
| `too_many` | The identity has as many write tokens as it may. |
| `rate_limited` | Too many of these from one address this hour (answered with `"retry"`, seconds). |
| `banned` | The identity is on the ban list. |
| `not_allowed` | A push subscription to a host that is not a push service. |
| `cannot_write`, `server_full` | The server cannot keep anything right now (status 503). |

The operations and their answers are listed in
[docs/friends-protocol.md](../../docs/friends-protocol.md#the-server).

## On gamernova.net

As the meeting point and the room list, next to them as
`api/psrpt_friends.php`, with `api/psrpt_friends.config.php` naming the data
folder. What differs from them: the data have to last, so the folder is
`psrpt_friends_data/` in the site's own folder, which updates keep (it is in
the site's list of state paths, backed up with its other flat files, and
refused to the web by the site's `.htaccess` as well as its own), not the
container's temporary folder. BunkerWeb adds `Access-Control-Allow-Origin: *`
for the whole site, so `PSRPT_FRIENDS_CORS` stays empty there, and Apache takes
the client's address from BunkerWeb's `X-Real-IP` (mod_remoteip) already, so
`PSRPT_FRIENDS_CLIENT_HEADER` stays empty too. PHP takes bodies of 500 MB there,
more than the 600 KB a link part needs. The site's rules for a new public entry
point apply as for the meeting point (threat model, architecture overview,
privacy note).
