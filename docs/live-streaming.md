# Going live from the app

Every player of a session, host or guest, can stream what they see and hear to
Twitch (or any other RTMP or RTMPS service) on their own, without OBS. The stream
goes straight from the player's computer to the service: no server of the project
carries any of it.

Set up under **Settings → Live streaming**; **Go live** and **Stop live** are in the
stream menu. While somebody is live, everybody in the session sees a red **LIVE**
tag next to their name, in the app and in the browser.

## What goes into the stream

- **The picture:** the game picture as this player gets it, drawn a second time at
  the stream's size (720p or 1080p, 30 or 60 frames a second), with the display on
  top of it: players, webcams and controllers as arranged under **Overlays & OBS**.
  The display is laid out like the one on screen, in 16:9. The stream never shows
  the stream menu, the chat panel, chat notes, knocks or invites. HDR games are tone
  mapped to ordinary SDR (BT.709).
- **The sound:** the game sound (whatever the volume on this computer), the other
  players' voices as heard here (a player muted here is not heard in the stream
  either) and, if switched on, this player's own microphone while it sends (muted is
  muted in the stream too). Each has its own volume, 0 to 200 %.

When no new picture arrives in time, the last one is sent again, so the stream keeps
a steady frame rate. A full picture goes out every two seconds, as Twitch wants it.

## Being left out

**Leave me out of their streams** (Settings → Live streaming; in the browser the
**In streams / Not in streams** switch with the other own switches) tells the other
players' apps to keep this player out of their streams:

- their voice is not mixed into the stream,
- their webcam is not shown,
- their name and picture are replaced by "Player N".

A player who is live sees **NOT IN STREAM** on such a player's tile. The choice
travels with the player's other media flags (`kMediaNoStreams`, next to
`kMediaLive` for "live now", in `coplay/include/coplay/protocol.h`). Players and hosts
with an older version ignore both flags: they do not see the LIVE tag, and a choice
made there is not passed on by an older host.

## The stream key

The key is kept in the system's password store, never in the settings file:

| System | Where |
| --- | --- |
| macOS | the login keychain, item "PSRemotePlayTogether" / `live-stream-key-twitch` (or `-custom`) |
| Windows | the Credential Manager, `PSRemotePlayTogether/live-stream-key-twitch` |
| Linux | the Secret Service (GNOME Keyring, KWallet) through libsecret, opened when needed |

Where there is no such store (Linux without libsecret, for example), the key is kept
in memory until the app quits and the settings page says so. The settings file only
notes *that* a key was saved. The key field never shows the key, and the log only
names the server, without path or key. Since macOS builds are signed ad hoc, macOS
may ask once after an update whether the new version may read the key ("Always
Allow" keeps it quiet).

Twitch is reached over TLS (`rtmps://ingest.global-contribute.live-video.net/app`,
Twitch's own choice of the nearest ingest server). The app checks the server's
certificate before it sends the key: against `/etc/ssl/cert.pem` on macOS, the
system's certificate bundle on Linux, the Windows certificate store on Windows.

## Encoders

The picture is encoded as H.264 with the computer's own encoder, the first that
works of:

| System | Encoders, in order |
| --- | --- |
| macOS | VideoToolbox (hardware, falls back to Apple's software encoder) |
| Windows | NVIDIA NVENC, AMD AMF, Media Foundation (Intel and others), Media Foundation in software |
| Linux | NVIDIA NVENC, VAAPI (Intel, AMD), OpenH264 in software where the build has it |

The sound is AAC, 160 kbit/s, 48 kHz stereo. Everything goes out as FLV over RTMP or
RTMPS. `PSRPT_LIVE_ENCODER=<name>` forces one encoder, for trying them out.

## When the connection is slow or lost

Encoding and sending run on threads of their own. When more than 2.5 seconds wait to
be sent, pictures are left out until the connection catches up ("left out" in the
stream menu); the sound goes on. A lost connection is made again by itself, after 2,
4, 8 and then every 15 seconds, up to 20 times; a server that cannot be reached at
all is given up after three tries, with the reason in the stream menu.

## How it works

- `gui/src/livestream.cpp` (`live::Engine`): the encoder thread on a steady clock,
  the sender thread, reconnecting, the numbers.
- `gui/src/livecontroller.cpp` (`Chiaki.live`): the settings, the key, Go live and
  Stop, telling the others through the session's media flags.
- `gui/src/keystore.cpp`: the system password stores.
- `gui/src/qmlmainwindow.cpp` (`captureLiveFrame`): after the screen got its picture,
  libplacebo draws the same game frame once more into NV12 textures of the stream's
  size, with the display laid over it, and reads them back without waiting.
- `gui/src/qml/StreamView.qml` (live overlay): a second `StreamHud` with
  `broadcast: true`, drawn by Qt into a texture of the stream's size and never shown
  on screen. This needs the Vulkan renderer; with OpenGL the stream shows the game
  alone.
- `gui/src/voicechat.cpp`: the voices as heard, without whoever asked to be left out,
  and the own microphone, handed to the engine.

## Trying it without Twitch

A local RTMP server is enough:

```
ffmpeg -listen 1 -i rtmp://127.0.0.1:1935/app/test -c copy test.flv
```

Choose **Other service** with the address `rtmp://127.0.0.1:1935/app/test` and no
key, join a session (`coplay-sim host` works) and press **Go live**. For testing
without clicking, `PSRPT_LIVE_START=N` goes live four seconds into a session and stops
after N seconds (0: stays live). `coplay-sim host … --no-streams` is a host that asks to be
left out of streams, and its list of guests says which of them is live. `PSRPT_LIVE_CA_FILE` takes the certificates to check
an RTMPS server with, for a test server with a certificate of its own (FFmpeg cannot
listen on RTMPS itself; put a TLS front such as stunnel before the plain listener).

## Not yet

- Logging in to Twitch (title, category) and Twitch chat in the app.
- Tuning for 1080p at 60 frames a second on slower computers.
- Presets for YouTube and other services (the address works already).
- Browser guests cannot go live from the page.
