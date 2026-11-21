# The list of public rooms

`psrpt_rooms.php` keeps the list of public rooms: sessions whose hosts want
them to be found. A host who chooses **Public** on the Host page puts the room
on the list while the session runs (a title, a language, the game, how many
play, who controls, whether the host lets each player in) and takes it off
when the session ends or the host closes it to newcomers. Anybody can read the
list, in the app under **Join → Public rooms** and on the page for web
browsers, and join a room from it.

The list only says how to find a room. Joining works like with an invite, and
nothing of a session passes through this file: no stream, no sound, no
controller. The one picture it keeps is a small still of the game that a host's
app sends now and then, so that people see what is played (below). A listed invite names no address (`psrpt://-/KEY`): the host is
found through the [meeting point](../meeting-point), and the list itself holds
nobody's address. Players still connect to the host directly and so learn its
internet address, as with every invite; with a listed invite that can be
anybody who reads the list, which the app says on its Host page.

The app uses `https://gamernova.net/api/psrpt_rooms.php` unless a user enters
another address under Settings → Network & invites → Public rooms. You only need this folder
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

Pictures: a host whose room shows a live picture sends one JPEG of at most
320×180 every 45 seconds (`PSRPT_ROOMS_PREVIEW_MS`), usually 5 to 20 KB, at
most `PSRPT_ROOMS_PREVIEW_MAX` (48 KB). Whoever looks at the list loads each
new picture once. `PSRPT_ROOMS_PREVIEW_MS=0` switches pictures off: the list
then tells hosts not to send any and shows covers only.

## Installing

1. Copy `psrpt_rooms.php` to the web server, next to the meeting point, for
   example as `https://example.org/psrpt_rooms.php`.
2. Open that address in a browser. It answers with the list, empty at first:

       {"v":1,"ok":true,"poll":15000,"rooms":[]}

3. In the app: Settings → Network & invites → Public rooms → Use my own… →
   `example.org/psrpt_rooms.php`. The switch beside **Public rooms** turns them
   off: nobody can host one, and Join shows no list.

The page for browsers reads the built-in list; a copy of the page of your own
reads another one when `DEFAULT_ROOMS_URL` in `web/js/rooms.js` names it.

## What a room says

| Field | What it is |
| :--- | :--- |
| `title` | The host's title for the room, up to 60 characters. Empty in the app becomes "Alex's room". |
| `lang` | The language the room speaks, two letters (`de`, `en`), or empty for any. |
| `game` | What runs on the console, as the console names it; empty on the home screen. |
| `preview` | 0, or when the host last sent a picture of the game (Unix time). The picture is served by the list itself at `?v=1&preview=<id>&t=<that time>`, so the list never makes anybody load a picture from anywhere else but Sony's image server and the list. |
| `title_id`, `cover` | The game's id (`PPSA12345`, `CUSA12345`) and a picture of it. The host's app finds the picture in Sony's public catalog by the title id (`m.np.playstation.com/api/catalog/v2/titles/<id>_00/concepts`, no login) and sends its address. A cover can only be an address on `image.api.playstation.com`, so a list never makes anybody load a picture from anywhere else; viewers ask Sony's server for a small copy (`?w=192`). |
| `host` | The host's name. |
| `initials`, `faces` | Who plays: one letter for each player, the host first (`?` for a player without a name), and for each the name of their picture on the list, or empty. Initials only, never the other players' names; a picture only where a player has chosen one in the app. The list serves the pictures itself, at `?v=1&face=<id>&h=<name>`, as JPEGs of at most 128×128. |
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
     "policy","control","web","invite","proto","title_id","cover","preview","since"}, ...]}
GET  ?v=1&preview=<id>&t=<time>
  → the room's picture (image/jpeg), 404 when it has none
GET  ?v=1&face=<id>&h=<name>
  → a player's picture (image/jpeg)

POST v=1 action=announce id=<16 hex> token=<32 hex> title= lang= game= host= players= max=
     policy= control= web=0|1 invite= proto= [title_id=] [cover=]
POST v=1 action=preview id=<16 hex> token=<32 hex> image=<JPEG, base64url without padding>
POST v=1 action=faces id=<16 hex> token=<32 hex> faces=<for each player: a JPEG or nothing, base64url, comma-separated>
POST v=1 action=remove id=<16 hex> token=<32 hex>
  → {"v":1,"ok":true,"poll":15000,"preview":45000}  or  {"v":1,"ok":false,"error":"..."}
```

`preview` in the answer is how often a host sends a picture, in milliseconds, or 0
for none; a list that does not answer with it (an older copy of this file) gets
no pictures. A picture is taken only for a room on the list, from its own host,
as a JPEG of at least 16×16 and at most 640×360 pixels, and at most one every
half that time (429 otherwise). It goes when the room goes.

The players' pictures come with `action=faces`, from the room's host, whenever
the players change: for each player in the order of `initials` a JPEG of at
most 128×128 and `PSRPT_ROOMS_FACE_MAX` (8 KB; the app sends 64×64, about 2 to
4 KB) or nothing. They go when the room goes, and a picture nobody has any more
goes at once.

The first announce of an id keeps a hash of its token; only that host can
change or remove the room afterwards (403 otherwise). A host's app makes a new
id and token for every session.

## What to tell a reverse proxy or web application firewall

The same as for the meeting point ([its README](../meeting-point/README.md#what-to-tell-a-reverse-proxy-or-web-application-firewall)):

- **Methods:** `GET` for the list and the pictures, `POST` with
  `application/x-www-form-urlencoded` for a host: under 2 KB for a room, under
  64 KB for a picture, under 96 KB for the players' pictures.
- **Rate limit:** per client address, at least 1 request per second.
- **No bot challenge** on this path: the client is an app or a script on the
  page. The app sends `User-Agent: PSRemotePlayTogether/<version>`.
- **Status codes:** 400 for malformed requests and rooms, 403 for a room that
  belongs to somebody else, 503 when the list is full or cannot write.
  Everything else is 200.
- **Caching:** none for the list (`Cache-Control: no-store`). A picture may be
  kept for ten minutes (`Cache-Control: public, max-age=600`): its address names
  the moment it was taken.

## Web browsers

The page for browsers reads the list from another address than its own, so the
answers need `Access-Control-Allow-Origin`. Set `PSRPT_ROOMS_CORS` to `*` if
nothing in front of the script adds it; leave it empty if your reverse proxy
adds it already (two such headers count as none). The page only reads the list;
it never hosts a room. It fetches the pictures the same way (and shows them from
memory), so they need the header as well, and the page's
Content-Security-Policy needs no address of the list.

## What it stores, and what it can see

- Stored: per room what the table above lists, a hash of the host's token, when
  the room was announced first and last, its newest picture and its players'
  pictures, until 45 seconds after the last announcement. No IP addresses, no log.
- Everything in a room is public by design: the host chose to list it. What a
  room says is written by its host; the script only checks that it is text of
  the right length, without control characters, and that the invite names no
  address. The same holds for its picture: a still of whatever the host's screen
  shows of the game, checked only for being a small JPEG. The app sends one only
  while the host leaves "Live picture in the list" on, and never in streamer
  mode. The players' pictures are the ones they chose in the app; whoever joins a
  public room is shown in its list with that picture and the first letter of
  their name.
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
  --lang en --game "A game" --policy ask --admit-after 10 --preview picture.jpg
```

`--preview` sends that JPEG as the room's picture, as often as the list asks;
`--face small.jpg` gives the simulated host a picture in the list. Guests from
`coplay-sim guest INVITE --name Mia` show as initials.

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
