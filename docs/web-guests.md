# Joining from a web browser

A friend without the app opens a link, plugs in a controller and plays along.
This note says how that works, what it needs, and where it ends.

Decided on 2026-11-20, like for the app itself: **nothing of a session passes
through a server.** The browser connects to the host's computer directly. A
server only helps the two find each other.

## The short version

- The host's app prints a link, the **Web browser** invite:
  `https://<page>/#<key>`. The page is a folder of static files (`web/` in this
  repository). The key is the session's key, the same one the app's invites
  carry.
- The page and the host each leave a sealed note at the **meeting point** (the
  one that lets the app join without a forwarded port, see
  [nat-traversal.md](nat-traversal.md)) and then connect to each other with
  **WebRTC**: a direct, encrypted connection that browsers bring along.
- Over that connection the page speaks the app's own protocol. To the host a
  browser guest is a guest like any other: it shows up in the player list (with
  a small "WEB" mark), its controller is merged with everybody's, it hears and
  is heard, it reads and writes in the chat.

## What works and what does not

| In a browser | |
| --- | --- |
| Game picture and sound | yes (WebCodecs; H.264, and H.265 where the browser has it) |
| Controller | yes, through the Gamepad API; the keyboard can stand in |
| Rumble | where the browser has it (Chrome, Edge, Safari) |
| Voice chat | yes, both ways |
| Text chat | yes; link previews are shown as plain links |
| The other players' webcams | yes |
| The own webcam | not yet |
| Player pictures | shown; a browser guest has none of its own yet |
| Haptics, adaptive triggers, light bar | no: browsers have nothing for them |
| Touchpad, motion sensors | no |
| A host's forwarded port | a second one, for browsers (see "Routers" below) |

Browsers: a current Chrome, Edge, Firefox or Safari. The page says what is
missing if a browser cannot do something. Without WebCodecs it still lets a
guest join with the controller only, which is all somebody in front of the
host's TV needs.

**H.265.** A PS5 streams H.265 unless the host changes it. Chrome, Edge and
Safari decode it where the computer has a hardware decoder, which is most of
them; Firefox on fewer. A guest whose browser cannot is told so, and the host
can switch to H.264 (Settings → Video).

**Delay.** Measured on one machine the way through WebRTC adds about a
millisecond in each direction. What a browser adds on top is its decoder and
its drawing, which wait for the screen's next refresh: a frame or so more than
the app.

## How the two find each other

A browser cannot send the app's UDP packets, and it cannot be handed an
address and a port to send to. What it can do is WebRTC, which needs each side
to know how the other can be reached and how it proves who it is:

    ICE name and password, the fingerprint of its certificate,
    and its addresses (in the own network, and on the internet as a STUN server saw it)

WebRTC leaves it to the application to carry that across. Here the meeting
point does, in two more notes:

1. The page makes its half of the connection, and leaves a note (role `g`,
   like a guest's) that says the above about the browser.
2. The host's app, which looks at the meeting point every 3 seconds while it
   shares, finds the note, makes its half, and leaves a second note of its own
   next to its usual one, with its answers to the browsers that are on their
   way in.
3. The page finds its answer. Both sides now send to each other's addresses at
   the same time, which makes ordinary routers let the other side in (the same
   hole punching as for the app), and the connection is made.
4. The page takes its note away again, the host its answer.

The notes are sealed with a key derived from the session's key:

    AES-256-GCM, key = HKDF-SHA256(session key, "psrpt coplay web note")
    text = base64url(12 random bytes | ciphertext | 16 byte tag)

AES-GCM rather than the ChaCha20-Poly1305 of everything else, because it is
what a browser brings along (WebCrypto). The meeting point cannot read these
notes either, and cannot write one. An app that does not know them (older than
this feature) ignores them: it cannot open them as notes of its own.

The meeting point needs nothing new for this. Its limits already fit (a note
is 150 to 700 characters, two notes per host, ten guests' notes per session),
and a web page may talk to it as long as it answers with
`Access-Control-Allow-Origin` (see `server/meeting-point/README.md`).

`coplay/include/coplay/web.h` has the exact layout of the notes.

## Why this is safe

- **Only somebody with the link can join.** Without the key a note cannot be
  written, and the host only ever answers a note it could open.
- **The connection cannot be taken over.** Each note names the fingerprint of
  its side's certificate, and WebRTC refuses a connection whose certificate
  does not match. A meeting point (or anybody else on the way) that swapped
  notes could at most keep the two from connecting.
- **Encrypted end to end** between the browser and the host's app (DTLS), like
  every WebRTC data channel. Inside it the packets of the app's protocol
  travel without their own header and seal, which the connection provides.
- **The key stays out of sight.** It stands behind the `#` of the link, the
  part of an address a browser never sends to a server. The page copies it
  into the tab's own storage and takes it out of the address bar at once, so
  that a screenshot or a stream of the browser window does not show it.
- **The page asks for nothing it does not need:** no cookies, no tracking, no
  third-party scripts, fonts or pictures. A content security policy only
  allows its own files and the request to the meeting point. The microphone is
  only asked for when the guest ticks "Join the voice chat".
- **The host stays in control.** Browser guests can be muted, set to watching
  and removed like everybody else, "Make a new invite" locks all old links
  out, and **Play Together → Friends can join from a web browser** switches
  the whole thing off.

The page is as trustworthy as where it is served from: whoever can change the
files a guest loads could read the key. The built-in page is served straight
from this repository. A host who would rather not rely on that can serve the
folder `web/` from anywhere with HTTPS and enter that address under
**Play Together → Page for browsers**; the links then lead there.

## The pieces

### In the app

- `coplay/src/web.cpp`: the notes, and what is needed to put a session
  description back together from one.
- `coplay/src/webrtc.cpp`: the WebRTC side, built on
  [libdatachannel](https://github.com/paullouisageneau/libdatachannel) (MPL
  2.0; with its libjuice for the connection and usrsctp for the data channel).
  One connection per browser, each with the one data channel the page opens.
  The channel delivers like UDP: no order, nothing sent again; the protocol
  repairs what it needs itself, as it does for the app.
- `coplay/src/host.cpp`: a browser guest is a `Guest` whose packets go through
  the gateway instead of the UDP socket. Everything after that is shared.

libdatachannel is fetched and built by CMake at a pinned commit, statically,
with only what data channels need. `-DCOPLAY_ENABLE_WEB=OFF` builds the app
without it; **Web browser** invites then do not appear.

A browser's connection has its own flow control. When a guest's line cannot
keep up, the host notices that packets pile up in front of it, holds the
picture back for that guest only, and starts it again with a fresh key frame
once the pile is gone. Sound, chat and the others are not affected.

### The page

`web/` is plain HTML, CSS and JavaScript modules: no framework, no build step,
nothing to install.

| File | |
| --- | --- |
| `js/meet.js` | the notes and the meeting point |
| `js/session.js` | the connection and the protocol |
| `js/video.js` | putting frames back together, decoding, drawing |
| `js/audio.js`, `js/audio-worklet.js` | game sound, voices, the microphone |
| `js/pad.js` | controller and keyboard |
| `js/app.js` | the page itself |

It is published to GitHub Pages by `.github/workflows/pages.yml` whenever
`web/` changes on `main`.

**Versions.** The page speaks one version of the protocol, the newest. A host
with an older app is told to update; a page left open across an update of the
host says to reload. When the protocol changes, hosts that have not updated
yet cannot take browser guests until they do.

## Routers

A browser guest gets in where the app gets in without a forwarded port: with
ordinary home routers on both sides, and over IPv6. It does not get through
when a router gives every connection a new port (mobile networks often do) and
the other side's router is strict as well.

For those cases the host can forward a port, as for the app, but not the same
one: a browser's connection cannot share the socket the app's guests use. All
browser guests come in on one UDP port of their own, **one above the sharing
port (9411)** unless another is set under **Play Together → UDP port for
browsers**. Forwarded to the host's computer, it lets a browser in whatever
the friend's router is like.

If that port is in use by something else on the host's computer, the app says
so in the session's log and gives every browser a port of its own instead,
which works with ordinary routers but cannot be forwarded.

## Trying it on one machine

Without a console and without a second computer. PHP serves the page and the
meeting point from one address, `coplay-sim` plays a video as if it were the
console:

```bash
mkdir -p /tmp/psrpt-www && ln -sfn "$PWD/web" /tmp/psrpt-www/web \
  && ln -sfn "$PWD/server/meeting-point/psrpt_meet.php" /tmp/psrpt-www/psrpt_meet.php
php -S 127.0.0.1:8765 -t /tmp/psrpt-www &
build/coplay/coplay-sim host sample.h264 --voice --chat \
  --meet http://127.0.0.1:8765/psrpt_meet.php --stun off --web http://127.0.0.1:8765/web/
```

It prints a link for a browser on the same machine. A meeting point without
HTTPS is only accepted by the page when the page itself is opened from
`127.0.0.1` or `localhost`. There the page also puts `window.psrpt` into the
browser's console for a look inside (the session, the decoder's counters).

`build/coplay/coplay-tests web` and `... browser` run the tests of the notes
and of the host's side, with a stand-in for WebRTC.

## Not done

- Sending the own webcam and an own player picture from a browser.
- Controls on the screen for phones and tablets without a controller.
- Picking the browser's STUN servers: the page asks Google's and Cloudflare's.
