# The meeting point

`psrpt_meet.php` lets friends join a PSRemotePlayTogether session whose host has
no forwarded port. Host and guest each leave a short sealed note with their
addresses here, read the other's, and then connect to each other directly.
Nothing of a session passes through this file: no picture, no sound, no
controller. How the whole thing works is in
[docs/nat-traversal.md](../../docs/nat-traversal.md).

The app uses `https://gamernova.net/api/psrpt_meet.php` unless a user enters
another address under Play Together → Meeting point. You only need this folder
if you run that one, or want one of your own.

## What it needs

- PHP 7.4 or newer behind any web server, with HTTPS. No database, no
  extensions beyond what PHP ships with.
- A folder PHP can write to that is **not** served to the web. By default
  `psrpt-meet` in the system's temporary folder; set `PSRPT_MEET_DIR` (as an
  environment variable, or the constant at the top of the file) for another.
- One web server instance. The notes are files on that machine; two servers
  behind a load balancer would each know half of them. (For that, the storage
  part of the script, about 60 lines, is the piece to replace with a database.)

Load: a sharing host asks every 3 seconds, a guest a handful of times while it
connects. A request is about 400 bytes and takes a file lock, a read and a
write. A room is a file of a few hundred bytes that disappears half a minute
after the last note in it. Raise `PSRPT_MEET_POLL_MS` to slow all hosts down if
the server ever gets busy; the apps follow it.

## Installing

1. Copy `psrpt_meet.php` to the web server, for example as
   `https://example.org/psrpt_meet.php`.
2. Open that address in a browser. It answers:

       psrpt-meet 1
       info PSRemotePlayTogether meeting point. Nothing to see here: the app talks to it.

3. Run the self-test against it (22 checks, leaves only notes that expire):

       server/meeting-point/selftest.sh https://example.org/psrpt_meet.php

4. In the app: Play Together → Meeting point → `example.org/psrpt_meet.php`.
   Invites made after that carry the address (`...?meet=example.org/psrpt_meet.php`),
   so friends need not enter anything.

Without an argument, `selftest.sh` starts PHP's built-in server and tests the
file in this folder (24 checks, including that notes expire).

## What to tell a reverse proxy or web application firewall

- **Method and body:** `POST`, `application/x-www-form-urlencoded`, under 2 KB.
  `GET` is answered with 200 and one line of text.
- **Rate limit:** per client address, allow at least 2 requests per second.
  Several players behind one address (a household, a carrier's shared IPv4
  address) each need their own share, so more is better.
- **No bot challenge** (JavaScript, cookie or captcha) on this path: the client
  is an app, not a browser. It sends `User-Agent: PSRemotePlayTogether/<version>`.
- **Status codes:** malformed requests get 400. Everything an honest app can
  cause is answered with 200, including "this version is unknown to me", so that
  a proxy that bans addresses after a number of 4xx answers only ever bans what
  deserves it. 503 means "no room left" or "cannot write".
- **Caching:** none. The script sends `Cache-Control: no-store`.

The app is careful on its side: after any failed request it waits 15 seconds,
then twice as long each time up to 5 minutes. An endpoint that is missing or
down sees a few requests per minute and app, not one every 3 seconds.

## What it stores, and what it can see

- Stored: per room a list of `id`, `role`, sealed note and time, for 30 seconds
  after the note was last posted. No IP addresses, no names, no log.
- The notes are sealed with a key derived from the invite; the script cannot
  open them. The room name is derived from the invite's key in one direction
  only and is of no use for joining.
- Like any web server it sees the IP address a request comes from, and the web
  server in front of it may log that as it logs everything else.
- It cannot join a session, and it cannot make anybody send packets anywhere:
  for both it would have to write a valid note.

## On gamernova.net

What is specific to the site this project uses by default (checked against its
infrastructure notes on 2026-11-20):

- **Where:** `api/psrpt_meet.php` in the website's document root. BunkerWeb on
  the edge already gives `^/api/.*\.php` the higher rate limit (60 requests per
  second per address), ModSecurity is off for the site, `POST` is allowed, and
  only 400, 405 and 444 count towards a ban, which this script only sends for
  malformed requests. Nothing has to change there.
- **One web container** serves the site, so the file storage fits. The
  container's temporary folder is emptied when it is rebuilt, which costs
  nothing: notes live for 30 seconds.
- **The site's own rules for a new public entry point** apply if the file goes
  into the website's repository rather than being copied next to it: a row in
  the threat model (public `POST` without a session, so also in the register of
  CSRF exemptions), a line in the architecture overview (a new poll loop from
  outside), and the privacy note above (no personal data is stored; IP
  addresses only in the web server's usual log).
- **STUN:** the app asks Google's and Cloudflare's STUN servers by default. The
  site runs its own (`turn.gamernova.net:3478`); a user can enter it under Play
  Together → STUN server. With a single server the app can no longer tell
  which kind of router it is behind, because that takes two answers.

## Trying it with a friend

What one machine cannot prove is how real routers behave.

1. Host: make sure **no** port is forwarded for the app (remove the rule for
   UDP 9410, or try from a connection that never had one). Start sharing and
   open the stream menu (Ctrl+O / ⌘O). Under the buttons it says either
   "Friends over the internet can join without a forwarded port, through the
   meeting point", or what stands in the way.
2. Send the Internet invite to a friend on another internet connection, who
   joins as usual. It takes up to about five seconds longer than with a
   forwarded port.
3. If it fails, the guest's message says which case it was: the host was not
   at the meeting point (not sharing, or the meeting point cannot be reached
   from the host), or the host was found but the two routers did not let the
   connection through. The second one is the limit of this technique; mobile
   networks and some fibre connections are like that. Forwarding the port, or a
   mesh VPN such as Tailscale, still works then.

The session logs on both sides (`log` folder of the app's data folder) say
"joined through the meeting point" or "joined under the address in the invite",
and on the host "A friend is on the way in through the meeting point".

Without a second connection, `coplay-sim` plays both sides on one machine with
simulated routers in front of them:

    php -S 127.0.0.1:8765 -t server/meeting-point &
    build/coplay/coplay-sim host video.h264 --closed --no-address --stun off --meet http://127.0.0.1:8765/psrpt_meet.php
    build/coplay/coplay-sim guest 'psrpt://-/<key>' --closed --stun off --meet http://127.0.0.1:8765/psrpt_meet.php
