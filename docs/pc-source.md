# This computer as the game source

*Phase 1 (Windows) is built on the branch `pc-source` (see "Status" at the end); the decisions
are at the end too.*

Today a session always starts at a PS4 or PS5. The idea: the host can also share a game that runs on
the host's own computer, and the guests play it with their own controllers, in the app or in a
browser, exactly as they join a console today. Anything that runs in a window comes along with it:
PC games, Steam, emulators, GeForce Now, and the **Xbox app** with Remote Play to your own Xbox or
Xbox Cloud Gaming.

## What is already there

The rest of the app does not care where the game comes from, as long as it gets the same pieces:

| Piece | Today, from the console | From this computer |
| :--- | :--- | :--- |
| Picture | H.264 or HEVC, passed to the guests exactly as the console sends it (`Host::PushVideo`), described by `StreamInfo` (codec, size, frame rate) | encoded here into the same H.264, one access unit per frame |
| New guest needs a full picture | `on_keyframe_needed`, the app asks the console | the encoder makes the next frame a keyframe |
| Sound | Opus frames (`Host::PushAudio`) after an `AudioHeader` | the computer's sound, encoded to Opus here |
| Controllers | every player's input goes into `InputMixer`; the mix goes to the console as controller 1 | the mix goes to a **virtual controller** on this computer, or each player to their own |
| Your own picture and sound | decoded from the console's stream | the captured picture, shown as a preview |

Guests need no change at all: they get H.264 and Opus as before. Going live on Twitch, the
overlays, scenes and alerts work on top of it unchanged.

## The pieces

### 1. Choosing the source

On the Host page, next to your consoles: **This computer**, with the explaining line "Share a game
running here: a window or a whole screen". A picker lists open windows and screens, with a small
live preview. The window follows when it moves or changes size.

### 2. The picture

| System | How | Notes |
| :--- | :--- | :--- |
| Windows 10 1903+ / 11 | Windows Graphics Capture (one window or one screen) | Works with windowed and borderless games; exclusive fullscreen and DRM-protected windows can stay black. Desktop Duplication as the fallback for a whole screen. |
| Linux | xdg-desktop-portal ScreenCast + PipeWire | Wayland and X11; the system asks once which window or screen to share. |
| macOS 13+ | ScreenCaptureKit | Needs the Screen Recording permission. |

Encoding uses the hardware encoders the app already ships for going live: NVIDIA (NVENC), AMD
(AMF), Intel and others through Media Foundation on Windows, VideoToolbox on a Mac, VAAPI on Linux.
Settings for play, not for a recording: no B-frames, steady bit rate (up to about 20 Mbit/s at
1080p60), a keyframe only when a guest needs one. The first version copies frames through system
memory; handing the GPU texture straight to the encoder comes later, for less load on the PC.

### 3. The sound

The computer's sound, as 48 kHz stereo, encoded to Opus like the console's. Important: **only the
game's sound**, never the voice chat that the app itself plays, or guests would hear themselves
again. Windows 10 2004+ can capture a single program's sound (process loopback): the shared
window's program. Linux: the program's stream in PipeWire. macOS: ScreenCaptureKit leaves out the
app's own sound.

### 4. Controllers on this computer

| System | How | Notes |
| :--- | :--- | :--- |
| Windows | **ViGEmBus**, virtual Xbox 360 (or DualShock 4) controllers | A small driver, installed once with administrator rights; the app offers it when it is missing. The project is archived since 2023 but works on Windows 10 and 11 and is widely used (Steam Input, DS4Windows). Rumble from the game goes back to the guests. |
| Linux | uinput, a virtual Xbox-style controller | Built into the kernel; needs access to `/dev/uinput` (a udev rule the app explains). Rumble through force feedback. |
| macOS | none for games | Apple allows virtual game controllers only with a special driver permission. A Mac can share picture and sound, and the host plays with their own controller or keyboard; guests can watch, or play through keyboard keys later. |

### 5. A new way to play: each their own controller

The console only takes one Remote Play controller, so today everybody shares controller 1
(Together, One at a time, Only me). A computer can take four. New option, for this computer only:

**Each their own controller**, with the explaining line "Every player is a player of their own:
1, 2, 3, 4. For games with local multiplayer." The host is player 1, guests get 2, 3 and 4 in the
order they join, shown next to their names. This turns couch co-op games into online co-op.

The mixer already keeps every player's input apart (`InputMixer::Raw`), so this is mostly a new
`MixMode` that routes each player to their own virtual controller. Older guests do not know the new
mode: the protocol version goes up, and the list of public rooms shows it like the other modes.

### 6. The game's name, found by itself

The room says which game runs, as it does for a console. On this computer the app finds it by
itself, with the game detection of the GamerNova client ported over; without a match it takes the
shared window's title.

### 7. Mouse and keyboard together

For games played with mouse and keyboard, the guests' mouse and keys go to the shared game, as
Windows input (no driver needed; games that read raw mouse input get it too). Planned as the step
after the controllers:

- **Only into the shared window:** while another window is in front, nothing goes through, and the
  Windows key, Alt+Tab, Ctrl+Esc and the like never do. The host lets each player use mouse and
  keyboard, like "may play" today.
- **The mouse has one owner at a time** (whoever moved it last, after a short rest of the one
  before), keys from everybody count together, like the controller modes.
- Guests capture the mouse in the app and in the browser (pointer lock) and send movements, buttons,
  the wheel and keys; a new packet type, so the protocol version goes up again.
- Some anti-cheat systems refuse input that a program makes; the app says so where it can tell.

Built 2026-11-22: packet `KeyMouse` (0x17: buttons, running totals of movement and wheel, held
keys as USB HID usages; `coplay::KeyMouseState`), `kParticipantKeyMouse` per player, the host's
setting on the Host page and per player in the player's menu. Guests: the app keeps the pointer in
the window's middle and sends how far it moved (`QmlMainWindow::keyMouseEvent`), the page uses
pointer lock (`web/js/keymouse.js`). The host (`gui/src/pcinput.cpp`, `pcinput_mac.mm`):
Windows' SendInput with scan codes while the shared window is in front; Linux a uinput keyboard and
mouse while this app is not in front (Wayland does not say which window is); a Mac posts keys to
the shared program and moves the pointer while it is in front (Accessibility permission). Key
tables in `gui/src/keymap.cpp` (104 keys, checked both ways). Tested: the packet and the host's
permission in the library tests. **Not tested yet:** injection on a real Windows PC, Linux desktop
and Mac.

### 8. Xbox, and others

- **Xbox now:** run the official Xbox app on the host's Windows PC, start Remote Play to your own
  Xbox, or Xbox Cloud Gaming with Game Pass, and share its window. The Xbox app takes the virtual
  Xbox controllers, so the guests play. Nothing unofficial is involved. The docs get a step-by-step
  recipe, and the room says Xbox.
- **A built-in Xbox client later** (decided 2026-11-22): connecting to the Xbox console directly,
  like the app does with a PlayStation through its chiaki-ng base, with the open-source Greenlight
  and xbox-xcloud-player as the reference for Microsoft's streaming protocol and login. It is a
  project of its own: the protocol is undocumented and can change.
- **Nintendo Switch and others** only through a capture card, and their controllers would need extra
  hardware: not planned.

## What can go wrong

- Exclusive fullscreen, DRM-protected video and some anti-cheat systems can block capture or
  virtual controllers. The app says so when the picture stays black.
- Capture and encoding cost the gaming PC a few percent of GPU and CPU; the hardware encoders keep it
  small.
- Upload: like today, every guest gets their own stream from the host.
- ViGEmBus gets no more updates. There is no alternative without a driver that Microsoft signs for
  us, so the app uses it and says where it comes from.
- Tests need a real Windows PC with a graphics card and a controller; the Wine check here only shows
  that the app starts.

## Phases

1. **Windows:** This computer on the Host page, window and screen capture, hardware encoding,
   the game's sound, virtual controllers with ViGEmBus, Each their own controller, the Xbox recipe.
   Large: several working sessions, and a tester with a Windows PC.
2. **Linux:** PipeWire capture and sound, uinput controllers. Built 2026-11-22.
3. **macOS:** picture and sound only. Built 2026-11-22.
4. **Mouse and keyboard together** (section 7).
5. **Xbox built in** (section 8).

## Decided (2026-11-22)

1. Windows first.
2. ViGEmBus is needed for guests to play; the app says so and links it when it is missing.
3. Each their own controller is in phase 1.
4. Only the game's sound (a shared screen: everything but this app); the whole computer's sound
   may come later as an option.
5. The name on the Host page: **This computer**.
6. Public rooms say what the game runs on: PS5, PS4, PC or Xbox.

## Status (2026-11-22)

Built:

- `gui/src/pcsource.cpp`: the windows and screens to share (games first), the game's name (the
  GamerNova client's rules: store folders, WindowsApps engine marks, ignore list; Steam's own
  name from its manifests), capture through FFmpeg's `gfxcapture` and `scale_d3d11`, the encoder
  (NVENC or AMF straight from the graphics card, Media Foundation through memory), the program's
  sound through Windows' process loopback, Opus. A test picture with a beep on every system for
  trying it out: `PSRPT_PC_SOURCE_TEST=1`. `PSRPT_PC_DUMP=<file>` writes what goes to the guests.
- `gui/src/virtualpads.cpp`: Xbox 360 controllers through ViGEmClient (fetched at a fixed commit).
- `gui/src/streamsession_pc.cpp`: the session without a console; the guests' input to the virtual
  controllers (Each their own: guest in place n is player n + 1), rumble back to each guest.
- `coplay`: protocol 5 with `MixMode::EachOwn`, `GameSource` in the stream info, a guest's own
  rumble and player number, the guests' input without the host's.
- Host page (This computer, what to share, Picture, Each their own), Join page and browser page
  (console badge and filter), list server (`console`, `control=each`), the `share` command.

Tested on a Mac with the test picture: the app shares it, a guest (`coplay-sim guest`) gets 60
pictures a second that decode and the sound; the library tests cover the new mode. The Windows
build is cross-compiled in Docker. **Not tested yet:** capture, encoders, sound and virtual
controllers on a real Windows PC, the Xbox app, ultrawide windows on the guests' screens.

### Linux (built 2026-11-22)

- `gui/src/pcsource_linux.cpp`: the system's screen sharing (xdg-desktop-portal ScreenCast over
  D-Bus: CreateSession, SelectSources with the mouse pointer in the picture and `persist_mode` 2,
  Start, OpenPipeWireRemote). The Host page lists "A window or a screen" (the system asks) and,
  once shared, "The window/screen you chose" with the portal's restore token (shared again without
  asking). The picture comes through PipeWire in memory (BGRx and friends, the crop of a window),
  scaled into the picture size keeping its shape by `gui/src/pcscale.cpp` (bilinear, BT.709 video
  range; checked against libswscale: within 1 step of luma on average), and
  encoded with NVENC, VAAPI (uploaded to the card), or OpenH264. libpipewire is opened with
  `dlopen`, so the AppImage neither bundles it nor needs it to start.
- The sound: an unlinked PipeWire capture stream that the app links itself (link-factory) to the
  game's output streams, matched by process (the node's `application.process.id`, else its
  client's `pipewire.sec.pid`) or program name; without a game, to every program's streams but
  this app's and voice chats' (Discord, TeamSpeak, Mumble, WebRTC calls, …). Checked again every 3 s.
- The game: the newest process installed where stores put games (`/steamapps/common/`, `~/Games`,
  Lutris), Wine and Proton programs by their Windows path, the name from Steam's manifest for its
  `SteamAppId`. The portal does not say which window it shared, so the newest game counts.
- Controllers: uinput devices that look like the kernel's xpad driver's Xbox 360 controller
  (045e:028e, the same buttons and axes, rumble through force feedback). The app's own controller
  list leaves them out by their name. Without access to `/dev/uinput` the Host page shows a
  udev rule (`TAG+="uaccess"`, as Steam's) to copy.
- Build: `libpipewire-0.3-dev` headers in CI. (No libswscale: FFmpeg 9's x86 assembly for it does not
  build with the NASM of the CI image.)

Tested in a Debian container: the portal handshake up to Start (the headless compositor there
cannot capture), the picture from a PipeWire video source at 30 fps, decoded on the Mac with the
right colours and shape, the game's sound alone (a "game" in a Steam folder at 440 Hz heard, another
program at 1500 Hz left out), without a game every program but a "discord", the controllers
(buttons, sticks, triggers, d-pad, rumble with its length). **Not tested yet:** a real desktop's
portal (GNOME, KDE), VAAPI and NVENC, real games, the full AppImage (built by CI).

### macOS (built 2026-11-22)

- `gui/src/pcsource_mac.mm` (ARC): windows from `CGWindowListCopyWindowInfo` (titles need the
  Screen Recording permission, else the program's name), screens in pixels; ScreenCaptureKit
  scales into the picture size keeping the shape and gives NV12 (BT.709). A screen leaves out this
  app's windows. The sound is a second stream: a window's program alone, or a screen without this
  app and voice chats. Games: an app whose `LSApplicationCategoryType` is a games category, or one
  in a Steam folder (Steam's name). No controllers for friends: they watch.
- The Host page says when the Screen Recording permission is missing and asks for it (the system's
  question, else its settings page).

Tested on a Mac without the permission: the list of windows and screens, the texts, the game
check. **Not tested yet:** the capture itself, which needs the permission.

### Capture devices (built 2026-11-23)

A capture card is a third kind next to windows and screens (`kind: "device"`). Every system lists
a capture card as a camera and its sound as a microphone of the same name, so one source covers
Windows, Linux and macOS: `gui/src/pcdevice.cpp`, through Qt Multimedia as the players' webcams
(`coplaycamera.cpp`). Cameras are in the list too, since the system cannot tell them apart.

- **Picture:** the device's format closest to the chosen height at the chosen frame rate
  (uncompressed before MJPEG), turned into RGB by Qt and scaled into NV12 or I420 by `PcScaler`
  (BT.709, video range), then the same encoders as a window's. The device's objects live on a
  thread of their own with an event loop.
- **Sound:** the sound input named like the device (or one whose name contains the other's);
  16-bit stereo at 48 kHz goes through as it is, anything else is converted and resampled.
- **Controllers:** none are plugged in for a device: what is plugged into the card cannot take
  them from this computer, so the friends watch and hear. The Host page says so.
- **Permissions:** on a Mac the camera and the microphone (asked once; sharing again after the
  answer).

Tested: the colour conversion in both layouts and the sound conversion (16-bit stereo, mono
float, 44.1 kHz) in a test program. **Not tested:** a real capture card on any system, the
device thread with AVFoundation, Media Foundation and V4L2, and which name each card gives its
sound. The shell on the development Mac cannot list devices (CoreAudio is out of its reach).

**Later, if wanted:** a PlayStation on the card with the friends' controllers through Remote
Play: a console session for the controllers only, and the card's picture to the guests instead
of the console's (`CoPlayVideoSampleCb`).
