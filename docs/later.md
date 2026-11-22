# Later

Ideas that are agreed on but not scheduled yet.

## Joining without opening router ports

Hole punching through a meeting point is done (version 0.1.3,
`docs/nat-traversal.md`). What is left:

- **Automatic port opening (UPnP / NAT-PMP / PCP).** While sharing, the app asks
  the router to forward the Play Together port (UDP 9410 by default) and removes
  the forward when sharing stops. Works on most home routers with UPnP on,
  needs no server, and would also help a host behind a symmetric NAT of its
  own router. Does not help on connections without a public IPv4 address of
  their own (DS-Lite, carrier NAT). A first draft (`coplayupnp.h/.cpp`, built
  on miniupnpc like chiaki's PSN code) exists in the worktree of the
  public-address work and was never wired in.
- **A relay for the stream itself** is deliberately not planned (decided on
  2026-11-20): it would cost server bandwidth, several Mbit/s per friend.
  Connections that hole punching cannot make need the forwarded port or a VPN.

## Joining from a web browser

Done: friends join with only a browser, see `docs/web-guests.md`. What is left:

- **The stream menu with the controller.** It opens and closes with
  L1+R1+L3+R3 as in the app, but inside it only the keyboard, the mouse and a
  finger move around.
- **A DualSense opened directly** (touchpad, motion sensors, adaptive
  triggers, light bar; Chrome and Edge) is written but has only been tried
  without a real controller.
- **The game's haptics** stay with the app: a browser turns them into rumble,
  because browsers have nothing finer.

## Guests wake the console

Wanted on 2026-11-22 ("wenn der host das erlaubt, wäre das sinnvoll"); designed, not built yet.
Today the session and the room end when the console goes to sleep (also when a guest who may
puts it to sleep), so nobody is left to ask. The idea:

- **Host page, off by default:** "Keep the room while the console sleeps: friends may wake it".
- When the console sleeps, the host's app ends the console session but keeps the room: guests
  stay in (or join it from the list, which says "Console asleep"), see a still picture with
  "The console is asleep", and those allowed get a **Wake the console** button.
- A guest's wish to wake goes to the host's app (a new packet), which wakes the console the
  way it does itself (Wakeup with the registration key), connects again and hosts as before;
  guests come back on their own, as after a lost console today.
- The host's app must be running (on a computer that does not sleep); the app keeps the
  computer awake only while somebody is in the room.

## A game on this computer, and Xbox

See [pc-source.md](pc-source.md).

- **Mouse and keyboard together** for games played with them: the guests' mouse and keys go
  into the shared window only, the mouse with one owner at a time (section 7 of pc-source.md).
  Proposed on 2026-11-22, waiting for a go.
- **A built-in Xbox client**: connecting to the Xbox console directly, like the app does with a
  PlayStation, instead of through the Xbox app. Decided for later on 2026-11-22: there is no Xbox
  to test with yet (maybe a Series X|S later). Xbox Cloud Gaming speaks nearly the same protocol
  (WebRTC with H.264, Opus and input over data channels, libdatachannel is in the app already)
  and needs only a Microsoft account, so it is the way to start without a console: login, start a
  game, picture and sound to a file, one button press. Gains over sharing the Xbox app's window:
  friends play from a Mac too, no capture or encoding on the host, maybe a controller each.
- **Player numbers next to the names** in Each their own controller (today the controller's
  lights say it).
