# Later

Ideas that are agreed on but not scheduled yet.

## Joining without opening router ports

1. **Automatic port opening (UPnP / NAT-PMP).** While sharing, the app asks the
   router to forward the Play Together port (UDP 9410 by default) and removes
   the forward when sharing stops. Works on most home routers with UPnP on,
   needs no server. Does not help on connections without a public IPv4
   address of their own (DS-Lite, carrier NAT). A first draft
   (`coplayupnp.h/.cpp`, built on miniupnpc like chiaki's PSN code) exists in
   the worktree of the public-address work and was never wired in.
2. **Hole punching through a small meeting-point server.** Both apps learn
   their public address with STUN (already in the app) and swap it through a
   tiny relay that only passes addresses, then send to each other at the same
   time. Works with DS-Lite and IPv6 too. Needs a small server, for example on
   gamernova.net; the invite would carry a session id instead of an address.
   A relay for the stream itself would only be the last fallback, because it
   costs server bandwidth (several Mbit/s per friend).

## Joining from a web browser

A "web invite": friends join with only a browser (WebRTC for picture, sound,
voice and webcam, the Gamepad API for controllers, the existing HTML overlay).
Browsers allow microphone, camera and gamepads only on HTTPS pages, so this
needs a join page and the same meeting-point server as hole punching.
