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

A "web invite": friends join with only a browser, without installing the app.
Looked at again on 2026-11-20 and kept for later: it is a project of its own.

- A browser cannot speak the app's UDP protocol. The host app would need a
  WebRTC stack (for example libdatachannel, on all three platforms) and would
  have to repackage the console's video and sound as RTP.
- The meeting point from hole punching is the piece that sets the connection
  up; WebRTC's offers and answers are larger than today's notes (2 to 4 KB).
- The join page has to be served over HTTPS (browsers allow microphone, camera
  and gamepads only there): gamernova.net or GitHub Pages.
- Works in a browser: picture, sound, controllers through the Gamepad API,
  voice chat, webcam, text chat, the existing HTML overlay.
- Does not: haptics, adaptive triggers, touchpad and motion; rumble only in
  some browsers. H.265 streams (the PS5's default) do not play in every
  browser, so the host may have to use H.264. A little more delay than in the
  app.
