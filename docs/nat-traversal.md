# Joining without opening a port

How a friend reaches a host whose router has no forwarded port, what that needs
on a server, and where it ends. Decided on 2026-11-20: **the meeting point only
introduces the two sides; it never carries a session's traffic.** Connections
that cannot be made directly stay impossible, and the host then has to open the
port (or use a mesh VPN) as before.

## The short version

- A host with a forwarded port works exactly as before and does not depend on
  anything new. The invite still carries the host's address and a guest always
  tries it directly.
- In addition, both sides leave a short sealed note with their own addresses at
  a **meeting point** (one small HTTPS endpoint), read each other's note, and
  send to each other at the same time. Routers let the answer to something they
  have just seen going out come back in, so the two streams meet: *UDP hole
  punching*.
- The meeting point stores a few hundred bytes per session for half a minute.
  It cannot read the notes and never sees a session key.

## What works and what does not

| Host's connection | Guest's connection | Result |
| --- | --- | --- |
| Port forwarded, or public address | anything | direct, as before |
| Ordinary home router, no port forwarded | ordinary home router | hole punching |
| DS-Lite / carrier NAT that keeps one port per socket | ordinary router | hole punching (IPv4), or IPv6 if both have it |
| IPv6 on both sides, firewalls closed | | hole punching over IPv6 |
| "Symmetric" NAT (a new port for every destination) | router that only checks the address | usually works: the other side learns the port from the first packet |
| Symmetric NAT | router that checks address *and* port (most of them) | **does not work** |
| Symmetric NAT | symmetric NAT | **does not work** |
| A firewall that blocks UDP altogether (some companies, schools, hotels) | | **does not work** |

Only a relay that carries the whole stream would close the last three rows. That
is several Mbit/s per guest for as long as they play, and it is deliberately not
part of this project. The app says which case it is in (see "What the app
shows"), so that a host knows when forwarding the port is still necessary.

Mobile networks and some fibre providers use symmetric NAT; most cable and DSL
routers do not.

## The pieces

### 1. The session's own address (STUN)

The app already asks a STUN server for the computer's address on the internet.
For hole punching the question has to be asked **from the very socket the
session uses**, because a router's mapping belongs to one socket. Host and guest
now do that:

- a Binding Request from the session socket to the configured server (Google's
  and Cloudflare's by default, the same setting as before), repeated every 15
  seconds while sharing so that the router keeps the mapping;
- asking two servers tells the two kinds of router apart: the same public port
  from both means the mapping is stable (hole punching can work); different
  ports mean symmetric NAT.

For IPv6 no question is needed: the computer knows its global address. The
session socket now prefers the lasting address over the temporary one, the same
choice the IPv6 invite already makes.

### 2. The note

What each side leaves at the meeting point:

    version, role (host or guest), a random id, a counter,
    up to 8 addresses: public IPv4 (from STUN), address in the own network, global IPv6

The note is sealed with ChaCha20-Poly1305 under a key derived from the invite's
key (`HKDF(key, "psrpt coplay meet note")`), like the handshake packets. Whoever
does not hold the invite can neither read nor forge it.

### 3. The meeting point

One HTTPS endpoint, `POST` with form fields, plain-text answer:

    room   22 characters: HKDF(key, "psrpt coplay meet room"), so it cannot be turned back into the key
    id     16 hex digits, random per session, tells the notes in a room apart
    role   h or g
    note   the sealed note, base64url; empty removes it

    psrpt-meet 1
    poll 3000
    note <sealed note of somebody with the other role>
    ...

A host gets the guests' notes, a guest gets the host's. Notes expire after 30
seconds unless they are posted again. A sharing host posts every 3 seconds (the
server can ask for a slower pace with `poll`), a guest every 0.7 seconds while
it is connecting and not at all afterwards.

**Recent sessions** (the Join page of the app and the page for browsers) ask
whether a host shares now with one request as a guest whose note is empty: the
answer holds the host's note if there is one, and nothing of the guest stays in
the room (`coplay::LookForHost`). A browser cannot open an app's note
(ChaCha20-Poly1305), but its header says it is a host's note, and only who has
the invite knows the room. This works with every meeting point that speaks this
protocol; the server needs no change for it.

The reference implementation is one PHP file without dependencies,
`server/meeting-point/psrpt_meet.php`; `server/meeting-point/README.md` says how
to deploy it and what to tell a reverse proxy. The default in the app is
`https://gamernova.net/api/psrpt_meet.php`.

What the meeting point can see: the IP addresses of the computers that talk to
it (like any web server) and how many notes a room holds. What it cannot: who
the players are, which game, the session key, the addresses inside the notes.
What it cannot do: join a session, or make a host send packets anywhere, because
it cannot write a valid note.

Everybody who holds the invite can read the notes of that room, and so sees the
addresses of the others who are connecting at that moment. The host sees every
guest's address anyway; a guest with a modified app could see another guest's.
"New invite" makes a new key and with it a new room.

### 4. Punching

- The guest sends its join request (`Hello`) every 250 ms to every address it
  knows of the host: the one in the invite and the ones from the host's note.
- The host, when it finds a guest's note, sends a small sealed `Punch` packet to
  each of the guest's addresses every 200 ms for 6 seconds. That opens the
  host's router for the guest. A guest that receives a `Punch` from an address
  it did not know (symmetric NAT on the host's side) adds that address.
- As soon as a `Hello` gets through, the handshake is the usual one and the
  session runs directly between the two computers. Regular traffic keeps the
  routers' mappings open.

`Punch` is a new packet type that older versions ignore; the protocol version
stays 4. A guest gives the connection up to 12 seconds once it has found the
host's note, because the host only looks every 3 seconds.

### 5. The invite

Unchanged: `psrpt://203.0.113.7:9410/KEY`. Two additions that older versions
answer with "the invite is malformed":

- `psrpt://-/KEY` when the host has no address to put in (STUN switched off or
  failed): the host is found through the meeting point alone.
- `...?meet=example.org/psrpt_meet.php` when the host uses a meeting point of
  its own, so that guests need not configure anything.
  A guest's app then sends its request, which always has the same form, to that
  address: HTTPS only, and the address cannot carry a query.

## What the app shows

In the stream menu, while sharing, one of:

- "Friends over the internet can join without a forwarded port, through the
  meeting point."
- "Your router gives every connection a new port (symmetric NAT). Friends with
  an ordinary router may still get in; if not, forward UDP port 9410."
- "No STUN server answers, so friends cannot be told this computer's address on
  the internet. Over IPv6 it may still work; otherwise forward UDP port 9410."
- "The meeting point cannot be reached: friends over the internet need UDP port
  9410 forwarded to this computer."

Play Together has a field for the meeting point next to the STUN server. Empty
is the default, `off` never contacts one (neither when sharing nor when
joining).

A guest that cannot get through is told which of the cases it was: no answer at
all, host not at the meeting point, or host found but no way through the two
routers.

## Being a good citizen towards the server

- One request every 3 seconds per sharing host, a handful per join. Requests are
  about 400 bytes, answers smaller.
- After any failure the app waits (15 s, doubling up to 5 minutes), so a server
  that is down or an endpoint that does not exist never sees more than a few
  requests per minute from one app.
- Recent sessions are asked about every 15 seconds while the Join page is
  open, one request for each (at most eight).
- Nothing is sent when Play Together is not in use.

## Tested how

`build/coplay/coplay-tests` runs host and guest on one machine with a simulated
router in front of each socket (it drops what comes from an address the socket
has not sent to), a stand-in STUN server and a stand-in meeting point. That
proves the logic: closed ports fail without the meeting point and work with it,
an open port never waits for it, a wrong STUN answer is healed by the `Punch`.
`coplay-sim` can do the same against the real PHP file (`--meet`, `--closed`).

What one machine cannot show is how real routers behave. That needs a host and a
guest on two different internet connections, neither with a forwarded port; see
"Trying it with a friend" in `server/meeting-point/README.md`.

## Not done

- UPnP / NAT-PMP / PCP (asking the router to open the port). Independent of
  this and still in `docs/later.md`.
- A relay. By decision.
- Guessing the next port of a symmetric NAT. Unreliable, and noisy for routers.
