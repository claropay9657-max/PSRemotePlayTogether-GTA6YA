# Twitch: alerts and elements

With a Twitch login the app hears what happens on the player's channel and shows it on the
stream: **alerts** (a card for each follow, sub, cheer, raid, …) and **Twitch elements**
(latest follower, goals, poll, chat, … on the game picture). Designs: the "Stream scenes"
canvas, sections "Alerts" and "Twitch elements". Everything shows in the app's own live
stream ([live streaming](live-streaming.md)) and in OBS alike.

## Logging in

**Settings → Twitch** → **Log in with Twitch** shows a code; the player confirms it on
twitch.tv/activate (OAuth device code flow, the app is a public client without a secret).
The app never sees the password. The tokens are kept in the system's password store
(`keystore.h`), the settings only note who is logged in. Builds of the project carry the
app's id at Twitch; own builds set `PSRPT_TWITCH_CLIENT_ID` in CMake.

What the app asks for: reading follows, subs, bits, channel points, hype trains, polls,
predictions, goals, charity, shoutouts, ad schedule and chat (`user:read:chat`), and
managing its own channel point rewards (for viewer actions, later). It never writes in
the chat.

Events come over Twitch's EventSub WebSocket (`gui/src/twitch.cpp`, own RFC 6455 client in
`websocketclient.cpp`), with keepalive, Twitch's own reconnect and a new connection with
backoff when it drops. A few things come from Twitch's API instead: viewers and the stream's
start (about once a minute, only while those elements are on), the next ad, running goals,
polls, predictions and charity campaigns at the start, the bits leaderboard of all time.
Nothing passes through a server of ours.

## In the public room list

**Show my stream in the room list** (Settings → Twitch, off by default): while the channel is
live (EventSub `stream.online` / `stream.offline`), the player's login goes with the public
room: the host's directly, a guest's in its Ping to the host (after the battery, read only by
hosts that know it), and the room carries them all, the host first (`twitch=a,b` in the
announcement, see `server/rooms/README.md`). The app's Join
dialog and the page for browsers then show **On Twitch** and **Watch on Twitch**, and the
filter **Only rooms streaming on Twitch**. The list takes only logins (3 to 25 of `a-z0-9_`)
and returns only those; both clients check it again and build `https://www.twitch.tv/<login>`
themselves, so no other link can get into a public list. The "Live" tag on a room's picture
is something else: it means the room shares a live picture of the game.

## Alerts

`gui/src/livealerts.cpp` (`Chiaki.alerts`): follow, sub, resub, gift subs, bits, raid,
channel points, hype train, poll, prediction, goal, charity, shoutout, ad break. One after
the other, never two at once, with "N more coming up" (at most 50 waiting). Each kind:
switch, sound (the app's, none, or an own file), volume, time on screen, minimum (gift
subs, bits, raid viewers, charity amount), **Test** (also heard here). On top: one level
and a switch for all sounds; **Wait during Be right back**; hidden words.

- **Cards**: an alert with something to read (a resub's or a cheer's message, a reward's
  text) is the wide card of the design "Alert on stream"; every other alert is its kind's
  own card of "All alerts", with its emblem and colour (`gui/src/qml/scenes/StreamAlert.qml`,
  for OBS `gui/res/overlay/alerts.js`, `http://localhost:9412/alerts`).
- **Sounds**: `gui/res/sounds/*.flac`, made by `scripts/sounds/make_alert_sounds.py` (see
  `gui/res/sounds/NOTICE.md`). Decoded with FFmpeg (`sounddecoder.cpp`) into the stream's
  mix, played here with Qt Multimedia when the alerts show on this screen too or for a Test
  or a slider. The OBS page plays them itself (`/alerts/sound/<kind>`, by kind only) at the
  level the app sends; tick "Control audio via OBS".
- **Chat sound**: a soft sound for new messages in Twitch's chat and the room's chat, never
  for one's own, at most one every 1.5 s. On by default, here only; **Also on stream** (off
  by default) puts it into the stream and the OBS alerts page.
- **Viewers' text** (names, messages, rewards, poll questions) is plain text everywhere:
  control characters removed, cut short, hidden words replaced by `*`. Pictures of viewers
  are only loaded from Twitch's own image servers (`static-cdn.jtvnw.net`).

## Twitch elements

`gui/src/twitchelements.cpp` (`Chiaki.twitchElements`), 19 elements in four groups:

| Group | Elements |
| --- | --- |
| Latest | follower, subscriber (tier, months), gift subs, bits, raid, redemption |
| Counters | top cheerer (this stream or all time), this stream (followers, subs, bits), viewers, uptime |
| Live events | follower goal, sub goal, hype train, poll, prediction, charity, ad break (only while they run) |
| All at once | recent events (one line), Twitch chat |

Each has a switch, a place and a size (`twitch_elements/<id>_show`, `_x`, `_y`, `_scale`).
Places are fractions of a 1920 × 1080 picture (32 from the edges), saved once and used
everywhere: on the game picture (`gui/src/qml/scenes/TwitchLayer.qml` inside `StreamHud.qml`,
so also in the app's own stream) and on the OBS overlay page (`gui/res/overlay/twitch.js`).
They are dragged on the game picture (stream menu → Overlays → Arrange, with lines that catch
the edges of the others) or in the browser (`?edit=1`, saved through `/layout?tw_<id>_x=…`).
The look is the overlay's: on the game picture the display's, in OBS the overlay's.

"Latest" ones are remembered across streams (`twitch_latest/*`); this stream's counts start
anew with each stream. They show while the player is live (on Twitch, or with the app's own
stream). While arranging, an element without data shows made-up data, a "latest" one without
data an empty dashed place. Chat messages a moderator deletes, and those of a banned viewer,
go from the chat element too.

## OBS layout

**Settings → Twitch elements → Export for OBS** writes an OBS scene collection (JSON, OBS:
Scene Collection → Import; `LiveScenes::exportObs`): "Game with overlay" (the app's window as
OBS captures it on each system, on macOS also its sound; the overlay page; the alerts) and
one scene for each stream scene with the alerts on top, all Browser Sources of the overlay
server at 1920 × 1080. OBS may ask once which window to capture.

## Testing without Twitch

`PSRPT_TWITCH_API_URL`, `PSRPT_TWITCH_EVENTSUB_URL` and `PSRPT_TWITCH_ID_URL` point the app
at a stand-in; `PSRPT_TWITCH_TEST_TOKEN`, `PSRPT_TWITCH_TEST_USER` and
`PSRPT_TWITCH_TEST_LOGIN` log in without Twitch.
