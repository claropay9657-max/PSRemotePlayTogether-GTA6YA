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
