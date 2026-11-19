<p align="center">
  <img src="gui/chiaking.png" width="160" alt="PSRemotePlayTogether icon">
</p>

# PSRemotePlayTogether

**Play one PlayStation together, from anywhere.** One of you connects to their PS5 or PS4 with Remote Play and shares the session. Friends join with an invite, get the same picture and sound, and plug in their own controller. The console sees all of you as a single controller, so any game works, single-player games included.

Free and open source, for Windows, Linux and macOS. Powered by [GamerNova.net](https://gamernova.net/).

## What it does

- **Shared play.** Everybody's controller is merged into the one Remote Play controller. Three ways to share it: everyone at once, one at a time (control passes to whoever is playing while the others rest), or only the host while guests watch.
- **Any controller.** DualSense (with haptics and adaptive triggers when connected by cable), DualShock 4, Xbox and other controllers. Rumble, trigger effects and the light bar reach every player.
- **Voice chat and webcams.** Everybody can talk to and see each other. Mute yourself, mute somebody just for you, switch the webcam on and off during play. The host can mute a guest for everyone or remove them.
- **Text chat.** Write to everybody in the session while you play: clickable links with a preview, emoji, and new messages shown on the game picture and, if you like, in your OBS overlay.
- **Controller only.** Sit next to each other, or watch the host's own stream: join without picture and sound and only send your controller.
- **Everybody on the game picture.** Players, who is talking, webcams, each player's controller live and the controller the console gets, on top of the game while you play. Show what you want, drag every piece where you want it, and keep several arrangements as presets.
- **Stream overlay for OBS.** The same for your viewers, as a browser source: who is playing, who is in control, every controller live, webcams, controller battery, and the conversation so it ends up in your stream. Transparent by default, arranged by dragging, with presets. Every player gets the overlay, not only the host.
- **Player profiles.** A name and a picture for each player, shown in the app and in the overlay.
- **Everything Remote Play needs.** Console registration, local and internet Remote Play, HDR, hardware decoding and the rest come from [chiaki-ng](https://github.com/streetpea/chiaki-ng), which this app is built on.

## How it works

```
                 Remote Play                    invite (UDP, encrypted)
  PS5 / PS4  <----------------->   Host   <------------------------------>  Guests
                                  (you)      picture, sound, haptics  -->
                                             <--  controllers
                                             <->  voices and webcams
```

The host runs a normal Remote Play session. What the console sends is passed on to the guests untouched; what the guests press is merged with the host's controller before it goes to the console. Voices and webcams go through the host to everybody else.

Guests need no PlayStation account and no access to the console. The invite contains the host's address and a session key; everything between host and guests is encrypted with it (ChaCha20-Poly1305).

## Getting started

### Share your console

1. Register your console as in any Remote Play app: your PSN account and the PIN from the console's Remote Play settings.
2. Open **Play Together**, switch on **Let friends join my sessions** and choose how the controller is shared.
3. Copy the invite and send it to your friends.
4. Connect to your console. Friends can join as long as you are connected.

Friends in your own network can join right away. For friends elsewhere, forward UDP port **9410** to your computer in your router and put your public address (or a DNS name) into the invite's address field.

### Join a friend

Open **Play Together**, paste the invite and press **Join**. Or from the command line:

```bash
PSRemotePlayTogether --name Alex join "psrpt://host.example.org:9410/…"
```

`PSRemotePlayTogether` is the program in the download: `PSRemotePlayTogether.exe` in the Windows zip, the `.AppImage` file itself on Linux, and `PSRemotePlayTogether.app/Contents/MacOS/PSRemotePlayTogether` on macOS.

Add `--controller-only` to join without picture and sound.

### While you play: the stream menu

Press **Ctrl+O** (**⌘O** on macOS), or hold **L1 + R1 + L3 + R3** on the controller, to open the stream menu on top of the game. Next to the usual Remote Play buttons it has the players, your microphone, the others' voices and your webcam, who may control, the invite, and **Overlays…** for everything shown on the game picture and in OBS. The host can click a player to mute them for everybody, let them only watch, or remove them. All of it can be changed while the game runs.

The controller shortcut can be changed in **Settings → General → Stream Menu Combo**.

### Voice chat and webcam

**Play Together → Voice & webcam** has the settings: take part in the voice chat, start muted, which webcam and how large its picture is. Microphone and speakers are the ones from the audio settings. During play, open the stream menu to mute yourself, switch voices or the webcam on and off, and click a player for more.

You never hear your own voice in the app, only the others. Use headphones: without them the others hear themselves through your microphone.

### Text chat

Press **⌘T** (Ctrl+T on Windows and Linux), or choose **Chat** in the stream menu, which also shows how many messages you have not read yet. The chat opens beside the game and takes the keyboard while it is open; Escape gives it back to the game. Anybody who joins later sees the last 30 messages.

- Links can be clicked. For the first link in your own message, your app looks at the page once and sends a small preview (title, site, picture) along, so your friends' apps never contact a site they did not choose. Switch this off under **Play Together → Voice & webcam**.
- Emoji come from the picker, from shortcodes such as `:fire:`, `:gg:`, `:thumbsup:`, and from `:)`, `:D`, `<3`, `xD`.
- New messages show on the game picture for a few seconds and, with **The chat, for the viewers**, in the OBS overlay (add `?chat=0` to the overlay address to hide it there).

### On the game picture

**Play Together → Overlays → On the game picture**, or **Overlays…** in the stream menu, decides what lies on top of the game for you while playing:

- the players with picture, name, a ring that lights up when they talk, and their controller's battery
- the webcams
- everybody's controller, live, either as one row or each one placed by itself
- the controller the console gets, with whoever is in control marked
- a short note when somebody joins or leaves

Choose **Arrange on the game picture** to drag the pieces into place; the mouse wheel or the + and − buttons change a piece's size. **Reset places and sizes** puts everything back.

Three looks: transparent without boxes (the default), glass boxes, dark boxes. A set of choices is a **preset**: make as many as you like, for example one for playing and one for watching, and switch between them at any time. Changes go into the selected preset by themselves.

### Stream overlay for OBS

1. **Play Together → Overlays → OBS overlay → Overlay on**.
2. Add the address shown there (`http://localhost:9412/` by default) to OBS as a **Browser Source**.
3. For the players' voices to reach your stream, tick **Control audio via OBS** in the Browser Source.

What the overlay shows, how it looks and where everything sits is set in the same place, also from the stream menu during play, and OBS follows at once. The background is transparent; players and controllers come without boxes unless you choose the glass or the dark ones.

To place things yourself, switch on **Place every player and the console's controller freely** and press **Arrange in the browser**: the overlay opens in your browser with made-up players if nobody is there yet, and you drag every player and the console's controller where you want them. Presets work as on the game picture, so that different scenes can have different arrangements.

Several sources can show different parts. Add to the address:

| Addition | Shows |
| --- | --- |
| `?show=players` | names, pictures, microphone state |
| `?show=pads` | each player's controller |
| `?show=cams` | the webcams |
| `?show=combined` | the combined controller the game gets |
| `&voice=0` | keeps this source silent |
| `&style=glass`, `&style=solid` | boxes around the players instead of the transparent look |
| `&battery=0` | hides the controller battery |
| `&layout=column`, `&scale=40`, `&skin=PS5 (nova pink)` | arrangement, size, controller look |
| `?demo=1` | made-up players, to set up a scene without a session |

Controller looks: DualSense in white, Midnight Black, Cosmic Red, Nova Pink, Starlight Blue, Galactic Purple, Gray Camouflage, Volcanic Red, Cobalt Blue, Sterling Silver, the three Chroma finishes and the anniversary gray, plus DualShock 4 and Xbox sets. The skins come from [CryptoPad](https://github.com/Crypto90/CryptoPad); the additional DualSense colours are derived from its white one.

## Good to know

- The host's upload carries the game picture to every guest, and every webcam to every other player. Plan with roughly the Remote Play bitrate per guest, plus 0.4 to 2.5 Mbit/s per webcam and receiver depending on the picture size chosen.
- There is no automatic NAT traversal yet: internet play needs the forwarded port.
- The controller battery is what the controller reports to the computer; some controllers and connections report nothing, and then nothing is shown.
- All players need the same version of the app.

## Building

macOS (Homebrew):

```bash
scripts/dev-build-macos.sh test
```

That builds the app and runs the tests of the sharing library. The library in `coplay/` has no dependency on the rest of the project and can be built on its own:

```bash
cmake -S coplay -B build-coplay && cmake --build build-coplay && build-coplay/coplay-tests
```

For Linux and Windows see the chiaki-ng build instructions; the dependencies are the same, plus Qt Multimedia for webcam capture (optional).

The downloads (Linux AppImage, Windows zip, macOS dmg) are built by the pipelines in `.forgejo/workflows/`. [docs/releasing.md](docs/releasing.md) describes them, how to publish a release, and how to run the same builds on your own machine with Docker.

### Trying it without a console

`coplay-sim` plays a video file as if it were a console and shares it, so that the guest side, the overlay, voices and webcams can be tried on one computer:

```bash
build/coplay/coplay-sim host sample.h264 --voice --cam a.jpg,b.jpg
```

It prints an invite to join with. `coplay-sim` without arguments lists all options.

`coplay-sim stun` asks the same servers the app asks for the invite for friends elsewhere, and prints the address this computer has on the internet.

## Credits and licence

PSRemotePlayTogether is built on [chiaki-ng](https://github.com/streetpea/chiaki-ng) by Street Pea and contributors, which continues [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki) by Florian Märkl. The controller skins of the overlay are from [CryptoPad](https://github.com/Crypto90/CryptoPad).

Licensed under the [GNU Affero General Public License v3](COPYING) with the OpenSSL exception chiaki uses (see `LICENSES/`). The app is free; whoever passes it on has to offer the source code with it.

This project is not endorsed or certified by Sony Interactive Entertainment. PlayStation, PS5, PS4, DualSense and DualShock are trademarks of Sony Interactive Entertainment.
