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
| Controller | yes, through the Gamepad API |
| Keyboard and mouse as a controller | yes, as in the app: **Keyboard & mouse** is the app's settings page of that name, row for row (the mouse aims with the same speeds, acceleration curve, dead zone and smoothing; every key and mouse button can be set; **Try it here**), **Mouse aims** is in the stream menu and F8 catches the mouse and lets it go. The same conversion as the app's, checked against it. Esc stays the browser's: it always gives the mouse back |
| The mouse as the touchpad | yes, as in the app: dragging on the picture swipes, another mouse button presses it |
| Voice & webcam settings | yes: **Voice & webcam** under "You join as" and in the stream menu is the app's settings page of that name: taking part in the voice chat, starting muted, the others' volume (up to 200 %), the microphone meter and mark, the chime (left, right, both), the webcam's size and picture, the others' webcams, and "Leave me out of their streams" |
| Without a controller, on a phone or tablet | controls on the screen; the picture is the touchpad |
| Rumble | where the browser has it (Chrome, Edge, Safari) |
| The game's haptics | as rumble; the strength is set in the stream menu |
| Voice chat | yes, both ways |
| Text chat | yes, with emoji; the previews of links the others post show, but a link posted from a browser gets none: a page cannot open other sites, and the host's app does not open links for browser guests on purpose (anybody in a public room could make it open addresses in the host's own network) |
| Just talk (no game) | yes: the host's **Just talk** shows "Just talking" in the browser as in the app |
| Webcams | the others' and the own |
| Player pictures | shown, and a browser guest can choose one of its own |
| The display on the game picture, the stream menu, chat, options | the app's, see below |
| Controller only (no picture and sound) | yes, like in the app |
| Putting the console to sleep when leaving | asked like in the app, where the host has allowed it for guests |
| Public rooms | the list, searched and filtered like in the app, on the page opened without an invite |
| Waiting at the door | yes, when the host lets each player in |
| Touchpad, motion sensors, adaptive triggers, light bar | with a DualSense the browser opens directly (WebHID: Chrome and Edge); new and little tested |
| A host's forwarded port | a second one, for browsers (see "Routers" below) |

Browsers: a current Chrome, Edge, Firefox or Safari. The page says what is
missing if a browser cannot do something. Without WebCodecs it still lets a
guest join with the controller only, which is all somebody in front of the
host's TV needs.

**H.265.** A PS5 streams H.265 unless the host changes it. Chrome, Edge and
Safari decode it where the computer has a hardware decoder, which is most of
them; Firefox on fewer. A guest whose browser cannot is told so, and the host
can switch to H.264 (Settings → Stream quality).

**Delay.** Measured on one machine the way through WebRTC adds about a
millisecond in each direction. What a browser adds on top is its decoder and
its drawing, which wait for the screen's next refresh: a frame or so more than
the app.

## The same as the app

What the page offers is meant to work, look and read like the app: the same
things in the same places with the same words, ranges and starting values, so
that nobody learns it twice. Whoever adds something to the page takes it from
the app's sources (the QML of the page or menu in question, `controls/Theme.qml`
for colours and sizes) instead of designing it anew, and checks it against the
app: logic against numbers made by the app's own code (`test/web/keypad-vectors.json`,
`test/web/chime-vectors.json`), looks side by side with the app's page.

Where a browser cannot do what the app does, the page says so at that place.
Today that is: Esc always gives the mouse back and leaves full screen, so it
cannot be a button of the controller; the mouse can only be caught right after a
click or a key press; microphone, speakers and camera are chosen in the browser
or the system, not on the page; and nothing that needs the app to host.

## On the game picture: the app's own

What lies on the game picture in a browser is made to look and behave like the
app, piece by piece, from the app's own sources:

- **The display** (`gui/src/qml/StreamHud.qml`): players, webcams, everybody's
  controller and the controller the console gets, each with a switch, a place
  and a size. The same seven presets, the same styles (no boxes, glass boxes,
  dark boxes), the same controller colours drawn with the same pictures
  (`web/pad/`, copied from `gui/res/overlay/cryptopad`). Arranging works the
  same way: drag an element, change its size with the buttons above it or the
  mouse wheel, Escape or **Done** to finish. Notes when somebody joins or
  leaves, and new chat messages for a few seconds.
- **The stream menu** (`StreamMenuContent.qml`, `PlayTogetherBar.qml`): the
  connection, voice chat and webcam, everybody as tiles (a click mutes a
  player for oneself), sound, picture (fit, zoom, stretch) and the presets of
  the display. It opens with **Ctrl+O** (**⌘O** on a Mac), with
  **L1+R1+L3+R3** on the controller, and with the **Menu** button that shows
  while the mouse moves, because a page cannot count on a keyboard. While it
  is open nothing goes to the game, as in the app.
- **The chat** (`StreamChatPanel.qml`) and **the options**
  (`StreamDisplayPanel.qml`, the "On the game picture" card of
  `DisplayOptions.qml`), opened from the stream menu.

What differs, on purpose: the menu has no card for the renderer's quality and
no microphone of the console, which a guest does not have in the app either.
It has one card more, **Controller**, for what the app keeps in its settings:
controls on the screen, how strong the game's haptics rumble, the DualSense,
the keyboard's keys. Settings and presets are kept in the browser
(`localStorage`), so they are per browser and not shared with the app.

When the app's display changes, the page has to follow by hand: sizes, colours
and wording are written out in `web/js/hud.js`, `web/js/menu.js`,
`web/js/options.js` and `web/css/game.css` next to the names of the QML files
they come from.

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
  only asked for when the guest switches on "Take part in the voice chat"
  (Voice & webcam) or joins the voice chat in the stream menu.
- **The host stays in control.** Browser guests can be muted, set to watching
  and removed like everybody else, "Make a new invite" locks all old links
  out, and **Browsers allowed** on the Host page switches
  the whole thing off.

The page is as trustworthy as where it is served from: whoever can change the
files a guest loads could read the key. The built-in page is served straight
from this repository. A host who would rather not rely on that can serve the
folder `web/` from anywhere with HTTPS and enter that address under
**Settings → Network & invites → Page for browsers**; the links then lead there.

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
| `js/keypad.js`, `js/mouseaim.js`, `js/keysdialog.js` | keyboard and mouse as a controller: the conversion (a port of `gui/src/keymousepad.cpp`), catching the mouse, and the window **Keyboard & mouse** (a port of `gui/src/qml/settings/KeyboardPage.qml`) |
| `js/devicecheck.js`, `js/voicedialog.js` | trying microphone, speakers and webcam (a port of `gui/src/devicecheck.cpp`), and the window **Voice & webcam** |
| `js/setparts.js` | the pieces the app's settings pages are made of (group, row, switch, slider, …), for those two windows |
| `js/audio.js`, `js/audio-worklet.js` | game sound, voices, the microphone |
| `js/pad.js` | controller and keyboard |
| `js/touch.js` | controls on the screen |
| `js/dualsense.js` | a DualSense opened directly (WebHID) |
| `js/camera.js` | the own webcam and player picture |
| `js/hud.js`, `js/presets.js` | the display on the game picture and its presets |
| `js/menu.js` | the stream menu |
| `js/options.js` | the options of the display |
| `js/rooms.js` | the list of public rooms |
| `js/app.js` | the page itself, and the chat |
| `css/game.css` | how everything on the game picture looks |
| `pad/` | the controller pictures of the display |

It is published to GitHub Pages by `.github/workflows/pages.yml` whenever
`web/` changes on `main`.

**Versions.** The page speaks one version of the protocol, the newest. A host
with an older app is told to update; a page left open across an update of the
host says to reload. When the protocol changes, hosts that have not updated
yet cannot take browser guests until they do.

## Public rooms

Opened without an invite, the page shows the list of public rooms
([server/rooms](../server/rooms)), the same list as the app's **Join → Public
rooms**, read again every 15 seconds while it is open: search by title, game,
host or language, a language to choose, and switches to hide full rooms and
rooms where the host lets each player in. Rooms that take browsers come first;
the ones whose host has switched browsers off ("App only"), that are full, or
that speak another version of the protocol are listed but cannot be joined.

**Join** on a room works like opening a link: the page takes the room's
invite, which names no address, and finds the host through the meeting point.
The list never sees anything of the session. A host who lets each player in
answers the browser's hello with "wait" instead of "welcome"; the page then
says "Waiting for the host to let you in…" and keeps waiting for up to two
minutes. Turned away, or at a room closed to newcomers, it says so and does
not try again by itself.

The list's address is `DEFAULT_ROOMS_URL` in `js/rooms.js`. Opened from
`127.0.0.1` or `localhost`, the page also takes `?rooms=` and `?meet=` from its
address, for trying things out with a list and a meeting point on the same
machine (see "Trying it on one machine" below). A cover in the list can only be
a picture on Sony's image server, which is also all the page's
Content-Security-Policy allows besides its own.

## Routers

A browser guest gets in where the app gets in without a forwarded port: with
ordinary home routers on both sides, and over IPv6. It does not get through
when a router gives every connection a new port (mobile networks often do) and
the other side's router is strict as well.

For those cases the host can forward a port, as for the app, but not the same
one: a browser's connection cannot share the socket the app's guests use. All
browser guests come in on one UDP port of their own, **one above the sharing
port (9411)** unless another is set under **Settings → Network & invites → UDP port for
browsers**. Forwarded to the host's computer, it lets a browser in whatever
the friend's router is like.

If that port is in use by something else on the host's computer, the app says
so in the session's log and gives every browser a port of its own instead,
which works with ordinary routers but cannot be forwarded.

The link carries the host's choice of STUN server (`&stun=host:port`, or
`&stun=off`) when the host has set one under Settings → Network & invites; otherwise the page
asks Google's and Cloudflare's.

## On a home screen

The page can be installed: `web/manifest.webmanifest` names it, its pictures
(`web/img/icon-*.png`, made from the app's icon; the "maskable" one fills its square so that a
phone can cut its own shape from it) and that it opens full screen. Chrome and Edge then
offer to install it, which the page passes on as **Install this page** at its foot; on an
iPhone or iPad the way is **Share → Add to Home Screen**, and a line at the foot says so
there. On Android an invite link then opens in the installed page; on an iPhone or iPad
links open in Safari, and the installed page starts at its front, where a link can be pasted.

There is no service worker, on purpose: nothing of the page is kept on the device. A copy
kept there could be older than the host's app expects (see "The page for web browsers" in
[releasing.md](releasing.md)), and the page is of no use without a connection anyway.
`test/web/manifest.test.mjs` checks the manifest, the pictures and that it stays that way.

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

It prints a link for a browser on the same machine. For a public room on the
page's list, see [server/rooms](../server/rooms/README.md#trying-it-on-one-machine). A meeting point without
HTTPS is only accepted by the page when the page itself is opened from
`127.0.0.1` or `localhost`. There the page also puts `window.psrpt` into the
browser's console for a look inside (the session, the decoder's counters).

`build/coplay/coplay-tests web` and `... browser` run the tests of the notes
and of the host's side, with a stand-in for WebRTC.

## Not done

- The DualSense opened directly is checked against its report layout with built
  reports (`node --experimental-default-type=module --test test/web/*.test.mjs`,
  also run before the page is published), not yet with a real controller.
- Controllers other than a DualSense opened directly show no battery in a
  browser: the Gamepad API does not tell it.
- A browser guest does not see which of the others came in through a browser
  (the host's app marks them "WEB").
