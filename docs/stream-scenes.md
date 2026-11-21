# Stream scenes

Full pictures for a live stream at the moments that are not the game, in the look of
the app (designs: the "Stream scenes" canvas). Each exists twice, after the same design:

- in the app's own live stream ([live streaming](live-streaming.md)): picked under
  **Scenes** in the stream menu, the scene takes the place of the game picture. The
  streamer keeps seeing the game, with a note at the top: "Viewers see: Be right back".
- in OBS, as a Browser Source (1920 × 1080) of the app's overlay server, like the
  overlay: `http://localhost:9412/scene/<id>`.

| Scene | id | Shows |
| --- | --- | --- |
| Starting soon | `starting` | countdown, title and line, who is in the room (HOST / LIVE), how to play along, channel |
| Lobby | `lobby` | everybody's webcam (or picture), who talks, muted, voice chat count, the game picture and name, how to play along |
| Be right back | `brb` | blurred picture of the game, "back in about N min", line, players and game |
| Play with us | `play` | steps to join, the room (live picture, title, game, how it is played and who gets in), free places, QR code |
| Technical difficulties | `difficulties` | console (or host) connection, voice chat, players |
| Thanks for watching | `ending` | the game, the time played together, everybody who played, next stream, channel |

`/scene/current` shows whichever scene is picked in the stream menu, and nothing while
the game is on: one Browser Source for all of them, switched from the app.
`?demo=1` fills a scene with made-up players and a public room, to set it up in OBS
without a session.

## What fills them

All from the app (`gui/src/livescenes.cpp`, `Chiaki.scenes`):

- **Players**: names, pictures, webcams, who talks, muted, HOST and LIVE. Whoever asked
  to be left out of streams (see [live streaming](live-streaming.md)) appears as
  "Player N", without picture or webcam, in every scene and in OBS as well.
- **The game**: its name as the console reports it, and a picture of it (640 × 360) taken
  from the stream every few seconds while a scene shows it.
- **The room**: title, how it is played, who gets in, browsers, free places (when
  hosting). How to get in depends on the room: a public room with browsers allowed shows
  the page for browsers (and a QR code to it), a public room without them the app's page,
  and a private session "ask in the chat for an invite". Scenes never show an address or
  an invite.
- **Times**: the countdown of Starting soon starts when the scene is picked (and again
  from the stream menu), the break of Be right back likewise; Thanks for watching counts
  from the start of the session, across a console reconnect.
- **Connection**: while the app gets the console back after a lost connection, Technical
  difficulties says so and, with **Technical difficulties by itself** on, the stream shows
  it without being asked. The live stream keeps going with the session that takes the
  lost one's place.

## Settings

**Settings → Stream scenes**: the channel (shown as twitch.tv/…), the next stream, the
minutes of the countdown and of the break, Technical difficulties by itself, the OBS
addresses (with Copy), and every text. Texts take `{game}`, `{host}`, `{me}`,
`{players}` and `{channel}`; a line whose name is not known is left out. They are kept in
`scenes/<key>` of the settings.

## Music

Starting soon, Be right back and Thanks for watching have music: the app's own loops
(`gui/res/sounds/music-*.flac`, 16 bars, made by `scripts/sounds/make_scene_music.py`, safe
to stream) or the player's own files (MP3, Ogg Vorbis or Opus, FLAC, WAV; in order or
shuffled, starting over). **Settings → Stream scenes → Music** has a switch and a volume for
each, and the choice of the built-in track or own files, with the note that only music one
may stream belongs there. Settings: `scenes/music_<scene>_on`, `_volume`, `_source`
(`builtin` or `own`), `_files`, `_shuffle`.

- In the app's own stream `SceneMusic` (`gui/src/scenemusic.cpp`) decodes with FFmpeg
  (`gui/src/sounddecoder.cpp`) a quarter of a second ahead into the stream's mix
  (`live::Engine::PushMusic`), fading in and out over 1.5 s when the scene changes. A single
  FLAC or WAV loops sample-exact; an MP3 may have a tiny gap at the loop.
- In OBS each scene page plays it with Web Audio (`AudioBufferSourceNode`, `loop` for a
  single track). The overlay server hands out the files only by scene and number,
  `/music/<scene>/builtin` or `/music/<scene>/<n>`, never by path. The exported OBS layout
  runs a scene's page only while its scene is shown, so its music starts with it.
- A file that cannot be played is skipped; when none can, the app's own track plays and
  the settings page says so. Nothing about it shows on the stream.

## How it works

- `gui/src/livescenes.cpp`: the data, the texts, the QR code (Project Nayuki's
  generator, `third-party/qrcodegen`), the JSON for the pages.
- `gui/src/qml/scenes/StreamScene.qml`: the scenes in QML, drawn at 1920 × 1080 into the
  live overlay of `StreamView.qml`; while one is up, `captureLiveFrame` leaves the game
  out of the stream's picture. Needs the Vulkan renderer, like the display.
- `gui/res/overlay/scene.html`, `scene.css`, `scene.js`: the scenes for OBS, served by
  `OverlayServer` with `/scene/data`, `/scene/qr.svg`, `/scene/game.jpg`, `/scene/logo.svg`
  and the font; live data comes as the `scene` event of `/events?scene=1`. Names and texts
  only ever go in as text, never as HTML.
- The font is Plus Jakarta Sans (SIL Open Font License, `gui/res/fonts/OFL.txt`).
