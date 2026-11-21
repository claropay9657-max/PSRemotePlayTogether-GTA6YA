# The list of public rooms

`psrpt_rooms.php` keeps the list of public rooms: sessions whose hosts want
them to be found. A host who chooses **Public** on the Host page puts the room
on the list while the session runs (a title, a language, the game, how many
play, who controls, whether the host lets each player in) and takes it off
when the session ends or the host closes it to newcomers. Anybody can read the
list, in the app under **Join → Public rooms** and on the page for web
browsers, and join a room from it.

The list only says how to find a room. Joining works like with an invite, and
nothing of a session passes through this file: no picture, no sound, no
controller. A listed invite names no address (`psrpt://-/KEY`): the host is
found through the [meeting point](../meeting-point), and the list itself holds
nobody's address. Players still connect to the host directly and so learn its
internet address, as with every invite; with a listed invite that can be
anybody who reads the list, which the app says on its Host page.

The app uses `https://gamernova.net/api/psrpt_rooms.php` unless a user enters
another address under Play Together → Public rooms. You only need this folder
if you run that one, or want one of your own.

## What it needs

- PHP 7.4 or newer with `mbstring`, behind any web server, with HTTPS. No
  database.
- A folder PHP can write to that is **not** served to the web. By default
  `psrpt-rooms` in the system's temporary folder; set `PSRPT_ROOMS_DIR` (as an
  environment variable, or the constant at the top of the file) for another.
- One web server instance, as with the meeting point: a room is a file on that
  machine.
- A meeting point. Public rooms only work through one, because their invites
  name no address. Usually it is the same server.

Load: a host announces its room every 15 seconds while the session runs, and
at once when something changes (a player joins, the game changes). A list is
read when somebody opens it or presses Refresh, and then every 15 seconds while
it stays open. A room is a file of about 600 bytes that disappears 45 seconds
after its host last announced it. `PSRPT_ROOMS_POLL_MS` slows all hosts down,
and the page's list with them (the app's list reads every 15 seconds while it
is open). `PSRPT_ROOMS_MAX` (500) is how many rooms the list holds at once.

## Installing

1. Copy `psrpt_rooms.php` to the web server, next to the meeting point, for
   example as `https://example.org/psrpt_rooms.php`.
2. Open that address in a browser. It answers with the list, empty at first:

       {"v":1,"ok":true,"poll":15000,"rooms":[]}

3. In the app: Play Together → Public rooms → `example.org/psrpt_rooms.php`.
   **off** switches public rooms off: nobody can host one, and Join shows no
   list.

The page for browsers reads the built-in list; a copy of the page of your own
reads another one when `DEFAULT_ROOMS_URL` in `web/js/rooms.js` names it.

## What a room says

| Field | What it is |
| :--- | :--- |
| `title` | The host's title for the room, up to 60 characters. Empty in the app becomes "Alex's room". |
| `lang` | The language the room speaks, two letters (`de`, `en`), or empty for any. |
| `game` | What runs on the console, as the console names it; empty on the home screen. |
| `title_id`, `cover` | The game's id (`PPSA12345`, `CUSA12345`) and a picture of it. The host's app finds the picture in Sony's public catalog by the title id (`m.np.playstation.com/api/catalog/v2/titles/<id>_00/concepts`, no login) and sends its address. A cover can only be an address on `image.api.playstation.com`, so a list never makes anybody load a picture from anywhere else; viewers ask Sony's server for a small copy (`?w=192`). |
| `host` | The host's name. |
| `players`, `max` | How many are in the session, the host included, and how many fit. |
| `policy` | Who gets in: `ask` (the host lets each player in), `watch` (everybody, watching until the host lets them play), `open` (everybody, playing at once). |
| `control` | Who controls: `all` at once, `turns` one at a time, or only the `host`. |
| `web` | Whether browsers can join. |
| `invite` | `psrpt://-/KEY`, plus `?meet=` when the host uses another meeting point. |
| `proto` | The version of the session protocol; a room of another version is shown, but cannot be joined. |

The key in a listed invite is the room's own, not the host's private invite:
the app makes a new one for every public session. Whoever has it can only
knock; with `ask` the host still decides who gets in, and can close the room
to newcomers or remove and block a player for the rest of the session.

## The requests

```text
GET  ?v=1
  → {"v":1,"ok":true,"poll":15000,"rooms":[{"id","title","lang","game","host","players","max",
     "policy","control","web","invite","proto","title_id","cover","since"}, ...]}

POST v=1 action=announce id=<16 hex> token=<32 hex> title= lang= game= host= players= max=
     policy= control= web=0|1 invite= proto= [title_id=] [cover=]
POST v=1 action=remove id=<16 hex> token=<32 hex>
  → {"v":1,"ok":true,"poll":15000}  or  {"v":1,"ok":false,"error":"..."}
```

The first announce of an id keeps a hash of its token; only that host can
change or remove the room afterwards (403 otherwise). A host's app makes a new
id and token for every session.

## What to tell a reverse proxy or web application firewall

The same as for the meeting point ([its README](../meeting-point/README.md#what-to-tell-a-reverse-proxy-or-web-application-firewall)):

- **Methods:** `GET` for the list, `POST` with
  `application/x-www-form-urlencoded` under 2 KB for a host.
- **Rate limit:** per client address, at least 1 request per second.
- **No bot challenge** on this path: the client is an app or a script on the
  page. The app sends `User-Agent: PSRemotePlayTogether/<version>`.
- **Status codes:** 400 for malformed requests and rooms, 403 for a room that
  belongs to somebody else, 503 when the list is full or cannot write.
  Everything else is 200.
- **Caching:** none. The script sends `Cache-Control: no-store`.

## Web browsers

The page for browsers reads the list from another address than its own, so the
answers need `Access-Control-Allow-Origin`. Set `PSRPT_ROOMS_CORS` to `*` if
nothing in front of the script adds it; leave it empty if your reverse proxy
adds it already (two such headers count as none). The page only reads the list;
it never hosts a room.

## What it stores, and what it can see

- Stored: per room what the table above lists, a hash of the host's token, and
  when the room was announced first and last, until 45 seconds after the last
  announcement. No IP addresses, no log.
- Everything in a room is public by design: the host chose to list it. What a
  room says is written by its host; the script only checks that it is text of
  the right length, without control characters, and that the invite names no
  address.
- Like any web server it sees the IP address a request comes from, and the web
  server in front of it may log that as it logs everything else.

## On gamernova.net

Next to the meeting point, as `api/psrpt_rooms.php`. Everything said about the
meeting point there applies: BunkerWeb's rate limit for `^/api/.*\.php`, one
web container (the temporary folder is fine, rooms live for 45 seconds),
`Access-Control-Allow-Origin` from BunkerWeb (so `PSRPT_ROOMS_CORS` stays
empty), and the site's rules for a new public entry point if the file goes into
the website's repository: a row in the threat model (public `GET` and `POST`
without a session), a line in the architecture overview, and the privacy note
above.

## Trying it on one machine

```bash
mkdir -p /tmp/psrpt && ln -s "$PWD/server/meeting-point/psrpt_meet.php" "$PWD/server/rooms/psrpt_rooms.php" "$PWD/web" /tmp/psrpt/
PSRPT_ROOMS_CORS='*' PSRPT_MEET_CORS='*' php -S 127.0.0.1:8765 -t /tmp/psrpt &
build/coplay/coplay-sim host video.h264 --name Nova --stun off \
  --meet http://127.0.0.1:8765/psrpt_meet.php --web http://127.0.0.1:8765/web/ \
  --public "Evening session" --rooms http://127.0.0.1:8765/psrpt_rooms.php \
  --lang en --game "A game" --policy ask --admit-after 10
```

The simulated host lists its room, lets each player in after ten seconds
(`--refuse` turns them away instead) and takes the room off the list when it
stops. Then:

- **App:** start it with `PSRPT_ROOMS_URL=http://127.0.0.1:8765/psrpt_rooms.php`
  and `PSRPT_MEET_URL=http://127.0.0.1:8765/psrpt_meet.php` in the environment
  and open **Join… → Public rooms**.
- **Browser:** open
  `http://127.0.0.1:8765/web/?rooms=http://127.0.0.1:8765/psrpt_rooms.php&meet=http://127.0.0.1:8765/psrpt_meet.php`.
  The page takes `rooms=` and `meet=` from its address only when it is served
  from `127.0.0.1` or `localhost`.
