# This computer as the game source

*Design for discussion, not built yet.*

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

### 6. Xbox, and others

- **Xbox:** run the official Xbox app on the host's Windows PC, start Remote Play to your own Xbox,
  or Xbox Cloud Gaming with Game Pass, and share its window. The Xbox app takes the virtual Xbox
  controllers, so the guests play. Nothing unofficial is involved. The docs get a step-by-step
  recipe.
- **A built-in Xbox client** (like the app is for PlayStation) would need Microsoft's undocumented
  streaming protocol and a Microsoft login, as the open-source Greenlight does it. It can change
  any time and is a lot of work: later at most, if the recipe above is not enough.
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
2. **Linux:** PipeWire capture and sound, uinput controllers.
3. **macOS:** picture and sound only.
4. **Xbox built in:** only if the recipe is not enough.

## Open questions

1. Windows first, as above?
2. ViGEmBus as a requirement for guests to play, installed by the host once when the app asks?
3. Each their own controller already in phase 1, or after the basics work?
4. The game's sound only (recommended), or the whole computer's sound as an option too?
5. The name on the Host page: **This computer**, or **This PC**?
