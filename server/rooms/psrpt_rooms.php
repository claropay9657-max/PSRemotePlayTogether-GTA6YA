<?php
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// PSRemotePlayTogether public rooms.
//
// A list of sessions whose hosts want them to be found: a title, a language, the
// game that runs, how many play. A host's app puts its room on the list while it
// shares and takes it off when it stops; anybody can read the list, in the app or
// on the web page, and join a room from it.
//
// The list only says how to find a room. Joining works like with an invite, and
// nothing of a session ever passes through this file. A listed invite names no
// address: the host is found through the meeting point, so the list does not
// publish anybody's address either. It stores no IP addresses and writes no log
// of its own.
//
// One file, no dependencies, PHP 7.4 or newer. README.md next to it says how to
// install it. The project: https://github.com/Crypto90/PSRemotePlayTogether
//
// Reading the list (GET):
//     ?v=1
//   answer (application/json):
//     {"v":1,"poll":15000,"rooms":[{"id","title","lang","game","host","players","max",
//      "policy","control","web","invite","proto","title_id","cover","initials","preview","since"}, ...]}
//   "preview" is 0, or when the room's host last sent a picture of its game (Unix time).
//   The picture itself (image/jpeg):
//     ?v=1&preview=<id>&t=<that time>
//   "faces": for each player (as "initials"), the name of their picture, or "" for none;
//   a picture (image/jpeg):
//     ?v=1&face=<id>&h=<that name>
//
// A host (POST, form fields):
//     v=1 action=announce id=<16 hex> token=<32 hex> title= lang= game= host= players= max=
//         policy=ask|watch|open control=all|turns|host web=0|1 invite=<psrpt://-/KEY[?meet=...]>
//         proto=<protocol version>
//         title_id=<CUSA12345|PPSA12345, optional> cover=<https://image.api.playstation.com/..., optional>
//         initials=<one letter or digit (or "?") for each player, the host first, optional>
//   (control: everybody plays at once, one at a time, or only the host. A cover can only be
//   a picture on Sony's image server, so that the list never makes a viewer load anything else.)
//     v=1 action=preview id=<16 hex> token=<32 hex> image=<JPEG, base64url without padding>
//   (a small picture of the game, at most 640x360 and PSRPT_ROOMS_PREVIEW_MAX bytes; only for a
//   room that is on the list, and at most one every half PSRPT_ROOMS_PREVIEW_MS)
//     v=1 action=faces id=<16 hex> token=<32 hex> faces=<for each player as in initials: a JPEG of
//         at most 128x128 and PSRPT_ROOMS_FACE_MAX bytes, base64url without padding, or nothing; comma-separated>
//     v=1 action=remove id=<16 hex> token=<32 hex>
//   answer: {"v":1,"ok":true,"poll":15000,"preview":45000} or {"v":1,"ok":false,"error":"..."}
//   ("preview": how often a host sends a picture, in milliseconds; 0 for none)
//
// The first announce of an id keeps a hash of its token: only that host can change
// or remove its room afterwards. A room that is not announced again disappears.

declare(strict_types=1);

// ------------------------------------------------------------------ settings
//
// Each one can also be set as an environment variable of the same name.

/** Folder for the rooms. Empty: "psrpt-rooms" in the system's temporary folder. Must not be reachable from the web. */
const PSRPT_ROOMS_DIR = '';
/** Seconds a room stays listed when its host does not announce it again. */
const PSRPT_ROOMS_TTL = 45;
/** How often a host announces its room, in milliseconds, and how often a list is worth reading again. */
const PSRPT_ROOMS_POLL_MS = 15000;
/** Rooms on the list at the same time. One takes about 600 bytes, and its picture up to PSRPT_ROOMS_PREVIEW_MAX. */
const PSRPT_ROOMS_MAX = 500;
/** How often a host sends a picture of its game, in milliseconds. 0: no pictures, the list shows covers only. */
const PSRPT_ROOMS_PREVIEW_MS = 45000;
/** Largest picture taken, in bytes (a 320x180 JPEG is about 10 to 20 KB). */
const PSRPT_ROOMS_PREVIEW_MAX = 49152;
/** Largest player picture taken, in bytes (a 64x64 JPEG is about 2 to 4 KB). */
const PSRPT_ROOMS_FACE_MAX = 8192;
/**
 * For the web page, which is served from somewhere else: the header
 * "Access-Control-Allow-Origin". '*' for every page (fine: there are no cookies here
 * and the list is public anyway), an address such as 'https://example.org' for that
 * page only, empty for none (when a reverse proxy adds it already).
 */
const PSRPT_ROOMS_CORS = '';

function psrpt_rooms_setting(string $name, $default)
{
    $value = getenv($name);
    if ($value === false || $value === '') {
        return $default;
    }
    return is_int($default) ? max(1, (int) $value) : $value;
}

/** How often a host sends a picture, 0 for never; unlike the other numbers this one may be 0. */
function psrpt_rooms_preview_ms(): int
{
    $value = getenv('PSRPT_ROOMS_PREVIEW_MS');
    $ms = ($value === false || $value === '') ? PSRPT_ROOMS_PREVIEW_MS : (int) $value;
    return $ms <= 0 ? 0 : max(15000, $ms);
}

function psrpt_rooms_cors(): void
{
    $cors = (string) psrpt_rooms_setting('PSRPT_ROOMS_CORS', PSRPT_ROOMS_CORS);
    if ($cors !== '' && preg_match('/^(\*|https?:\/\/[A-Za-z0-9.:\[\]-]+)$/', $cors)) {
        header('Access-Control-Allow-Origin: ' . $cors);
    }
}

function psrpt_rooms_answer(int $status, array $body): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Content-Type-Options: nosniff');
    header('X-Robots-Tag: noindex, nofollow');
    psrpt_rooms_cors();
    echo json_encode(['v' => 1] + $body, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function psrpt_rooms_dir(): ?string
{
    $dir = (string) psrpt_rooms_setting('PSRPT_ROOMS_DIR', PSRPT_ROOMS_DIR);
    if ($dir === '') {
        $dir = rtrim(sys_get_temp_dir(), '/\\') . DIRECTORY_SEPARATOR . 'psrpt-rooms';
    }
    if (!is_dir($dir) && !@mkdir($dir, 0700, true) && !is_dir($dir)) {
        return null;
    }
    return is_writable($dir) ? $dir : null;
}

/**
 * A line of text as a person wrote it: valid UTF-8, no control characters, spaces
 * collapsed, at most $most characters. Null if it is not text at all.
 */
function psrpt_rooms_text($value, int $most): ?string
{
    if (!is_string($value) || strlen($value) > 4 * $most + 16 || !mb_check_encoding($value, 'UTF-8')) {
        return null;
    }
    $value = preg_replace('/[\x{0000}-\x{001F}\x{007F}-\x{009F}\x{200B}-\x{200F}\x{202A}-\x{202E}\x{2066}-\x{2069}]/u', ' ', $value);
    $value = trim((string) preg_replace('/\s+/u', ' ', (string) $value));
    if (mb_strlen($value, 'UTF-8') > $most) {
        $value = rtrim(mb_substr($value, 0, $most, 'UTF-8'));
    }
    return $value;
}

function psrpt_rooms_int($value, int $low, int $high): ?int
{
    if (!is_string($value) || !preg_match('/^[0-9]{1,3}$/', $value)) {
        return null;
    }
    $number = (int) $value;
    return $number >= $low && $number <= $high ? $number : null;
}

/** The picture of a room's game next to the room's file: same name, ".jpg". */
function psrpt_rooms_picture_path(string $room_path): string
{
    return substr($room_path, 0, -strlen('.room')) . '.jpg';
}

/** Which picture each player has (a list of names, "" for none): same name, ".faces". */
function psrpt_rooms_faces_path(string $room_path): string
{
    return substr($room_path, 0, -strlen('.room')) . '.faces';
}

/** A player's picture: the room's name, the picture's own name (16 hex), ".face". */
function psrpt_rooms_face_path(string $room_path, string $name): string
{
    return substr($room_path, 0, -strlen('.room')) . '.' . $name . '.face';
}

/** Everything that belongs to a room, besides the room itself. */
function psrpt_rooms_remove_extras(string $room_path): void
{
    @unlink(psrpt_rooms_picture_path($room_path));
    @unlink(psrpt_rooms_faces_path($room_path));
    foreach (glob(substr($room_path, 0, -strlen('.room')) . '.*.face') ?: [] as $face) {
        @unlink($face);
    }
}

/** A small JPEG of a sensible size, nothing else: it is served to whoever reads the list. */
function psrpt_rooms_jpeg($data, int $most_bytes, int $most_w, int $most_h): bool
{
    if (!is_string($data) || $data === '' || strlen($data) > $most_bytes) {
        return false;
    }
    $info = @getimagesizefromstring($data);
    return $info !== false && ($info[2] ?? 0) === IMAGETYPE_JPEG
        && $info[0] >= 16 && $info[1] >= 16 && $info[0] <= $most_w && $info[1] <= $most_h
        && substr($data, 0, 3) === "\xFF\xD8\xFF" && substr($data, -2) === "\xFF\xD9";
}

/** The rooms that are still there; old files (and the pictures of old rooms) are removed on the way. */
function psrpt_rooms_read(string $dir, int $ttl): array
{
    $rooms = [];
    $now = time();
    foreach (new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS) as $file) {
        if ($file->isFile() && in_array($file->getExtension(), ['jpg', 'faces', 'face'], true)) {
            // A picture (or the list of pictures) whose room is gone: all start with the room's name
            $room_path = $dir . DIRECTORY_SEPARATOR . substr($file->getFilename(), 0, 64) . '.room';
            if (!is_file($room_path) && $file->getMTime() < $now - $ttl) {
                @unlink($file->getPathname());
            }
            continue;
        }
        if (!$file->isFile() || $file->getExtension() !== 'room') {
            continue;
        }
        if ($file->getMTime() < $now - $ttl) {
            @unlink($file->getPathname());
            psrpt_rooms_remove_extras($file->getPathname());
            continue;
        }
        $room = json_decode((string) @file_get_contents($file->getPathname()), true);
        if (is_array($room) && isset($room['id'], $room['t']) && $now - (int) $room['t'] <= $ttl) {
            $picture = psrpt_rooms_picture_path($file->getPathname());
            $room['preview'] = is_file($picture) ? (int) filemtime($picture) : 0;
            $faces = json_decode((string) @file_get_contents(psrpt_rooms_faces_path($file->getPathname())), true);
            $room['faces'] = is_array($faces) ? $faces : [];
            $rooms[] = $room;
        }
    }
    return $rooms;
}

// ------------------------------------------------------------------ the request

$ttl = (int) psrpt_rooms_setting('PSRPT_ROOMS_TTL', PSRPT_ROOMS_TTL);
$poll = (int) psrpt_rooms_setting('PSRPT_ROOMS_POLL_MS', PSRPT_ROOMS_POLL_MS);
$max_rooms = (int) psrpt_rooms_setting('PSRPT_ROOMS_MAX', PSRPT_ROOMS_MAX);
// A room has to outlive the pause between two announcements of its host
$ttl = max($ttl, (int) ceil($poll * 3 / 1000));
$preview_ms = psrpt_rooms_preview_ms();
$preview_max = (int) psrpt_rooms_setting('PSRPT_ROOMS_PREVIEW_MAX', PSRPT_ROOMS_PREVIEW_MAX);
$face_max = (int) psrpt_rooms_setting('PSRPT_ROOMS_FACE_MAX', PSRPT_ROOMS_FACE_MAX);

$dir = psrpt_rooms_dir();
if ($dir === null) {
    psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list has nowhere to keep rooms']);
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method !== 'POST') {
    if (($_GET['v'] ?? '1') !== '1') {
        psrpt_rooms_answer(200, ['ok' => false, 'error' => 'this list only knows version 1 of the protocol', 'rooms' => []]);
    }
    if (isset($_GET['preview']) || isset($_GET['face'])) {
        // The picture of a room that is on the list, or of one of its players
        $id = $_GET['preview'] ?? $_GET['face'];
        $name = $_GET['h'] ?? '';
        if (!is_string($id) || !preg_match('/^[0-9a-f]{16}$/', $id)
            || (isset($_GET['face']) && (!is_string($name) || !preg_match('/^[0-9a-f]{16}$/', $name)))) {
            psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed request']);
        }
        // basename(): what a request sends never names a folder (a hash, and 16 checked hex digits, cannot either)
        $room_path = $dir . DIRECTORY_SEPARATOR . basename(hash('sha256', $id)) . '.room';
        $picture = isset($_GET['face']) ? psrpt_rooms_face_path($room_path, basename($name)) : psrpt_rooms_picture_path($room_path);
        $data = (is_file($room_path) && filemtime($room_path) >= time() - $ttl && is_file($picture))
            ? @file_get_contents($picture) : false;
        if ($data === false || $data === '') {
            psrpt_rooms_answer(404, ['ok' => false, 'error' => 'no picture']);
        }
        header('Content-Type: image/jpeg');
        header('Content-Length: ' . strlen($data));
        // Its address names the picture's time (t=) or its content (h=), so a copy can be kept
        header('Cache-Control: public, max-age=600');
        header('X-Content-Type-Options: nosniff');
        header("Content-Security-Policy: default-src 'none'");
        header('X-Robots-Tag: noindex, nofollow');
        psrpt_rooms_cors();
        echo $data; // nosemgrep: php.lang.security.injection.echoed-request.echoed-request - a stored JPEG, checked when it came, served as image/jpeg with nosniff and default-src 'none'
        exit;
    }
    $now = time();
    $list = [];
    foreach (psrpt_rooms_read($dir, $ttl) as $room) {
        $list[] = [
            'id' => $room['id'],
            'title' => $room['title'],
            'lang' => $room['lang'],
            'game' => $room['game'],
            'host' => $room['host'],
            'players' => $room['players'],
            'max' => $room['max'],
            'policy' => $room['policy'],
            'control' => $room['control'] ?? 'all',
            'web' => $room['web'],
            'invite' => $room['invite'],
            'proto' => $room['proto'],
            'title_id' => $room['title_id'] ?? '',
            'cover' => $room['cover'] ?? '',
            'initials' => $room['initials'] ?? '',
            'faces' => $room['faces'] ?? [],
            'preview' => $preview_ms > 0 ? (int) ($room['preview'] ?? 0) : 0,
            'since' => max(0, $now - (int) $room['created']),
        ];
    }
    // The ones that have room for more first, then the ones that have been open longest
    usort($list, function (array $a, array $b): int {
        $full_a = $a['players'] >= $a['max'] ? 1 : 0;
        $full_b = $b['players'] >= $b['max'] ? 1 : 0;
        return $full_a <=> $full_b ?: $b['since'] <=> $a['since'];
    });
    psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll, 'rooms' => $list]);
}

$version = $_POST['v'] ?? '';
$action = $_POST['action'] ?? '';
$id = $_POST['id'] ?? '';
$token = $_POST['token'] ?? '';
if (!is_string($version) || !is_string($action) || !is_string($id) || !is_string($token)
    || !preg_match('/^[0-9a-f]{16}$/', $id) || !preg_match('/^[0-9a-f]{32}$/', $token)
    || !in_array($action, ['announce', 'remove', 'preview', 'faces'], true)) {
    psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed request']);
}
if ($version !== '1') {
    psrpt_rooms_answer(200, ['ok' => false, 'error' => 'this list only knows version 1 of the protocol']);
}

// Named after a hash: nothing a client sends becomes part of a path
$path = $dir . DIRECTORY_SEPARATOR . basename(hash('sha256', $id)) . '.room';
$token_hash = hash('sha256', $token);
$existing = null;
if (is_file($path)) {
    $existing = json_decode((string) @file_get_contents($path), true);
    if (!is_array($existing) || time() - (int) ($existing['t'] ?? 0) > $ttl) {
        $existing = null;
    }
}
if ($existing !== null && !hash_equals((string) ($existing['token'] ?? ''), $token_hash)) {
    psrpt_rooms_answer(403, ['ok' => false, 'error' => 'this room belongs to somebody else']);
}

if ($action === 'remove') {
    if ($existing !== null) {
        @unlink($path);
        psrpt_rooms_remove_extras($path);
    }
    psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll, 'preview' => $preview_ms]);
}

if ($action === 'faces') {
    if ($existing === null) {
        psrpt_rooms_answer(200, ['ok' => false, 'error' => 'announce the room first', 'poll' => $poll, 'preview' => $preview_ms]);
    }
    $list = $_POST['faces'] ?? '';
    if (!is_string($list) || strlen($list) > 9 * ((int) ceil($face_max * 4 / 3) + 5) || !preg_match('/^[A-Za-z0-9_,-]*$/', $list)) {
        psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed pictures']);
    }
    $parts = $list === '' ? [] : explode(',', $list);
    if (count($parts) > 9) {
        psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed pictures']);
    }
    $names = [];
    $pictures = [];
    foreach ($parts as $part) {
        if ($part === '') {
            $names[] = '';
            continue;
        }
        $data = base64_decode(strtr($part, '-_', '+/'), true);
        if (!psrpt_rooms_jpeg($data, $face_max, 128, 128)) {
            psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed pictures']);
        }
        $name = basename(substr(hash('sha256', $data), 0, 16));
        $names[] = $name;
        $pictures[$name] = $data;
    }
    // The new pictures, then which player has which, then away with the ones nobody has any more
    foreach ($pictures as $name => $data) {
        $face = psrpt_rooms_face_path($path, basename($name));
        if (is_file($face)) {
            continue;
        }
        $temp = $face . '.' . bin2hex(random_bytes(4)) . '.tmp';
        if (@file_put_contents($temp, $data) === false || !@rename($temp, $face)) {
            @unlink($temp);
            psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list cannot keep the pictures']);
        }
    }
    $faces_path = psrpt_rooms_faces_path($path);
    $temp = $faces_path . '.' . bin2hex(random_bytes(4)) . '.tmp';
    if (@file_put_contents($temp, json_encode($names)) === false || !@rename($temp, $faces_path)) {
        @unlink($temp);
        psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list cannot keep the pictures']);
    }
    foreach (glob(substr($path, 0, -strlen('.room')) . '.*.face') ?: [] as $face) {
        $name = substr(basename($face), 65, 16);
        if (!isset($pictures[$name])) {
            @unlink($face);
        }
    }
    psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll, 'preview' => $preview_ms]);
}

if ($action === 'preview') {
    if ($preview_ms === 0) {
        psrpt_rooms_answer(200, ['ok' => false, 'error' => 'this list takes no pictures', 'poll' => $poll, 'preview' => 0]);
    }
    if ($existing === null) {
        psrpt_rooms_answer(200, ['ok' => false, 'error' => 'announce the room first', 'poll' => $poll, 'preview' => $preview_ms]);
    }
    $picture = psrpt_rooms_picture_path($path);
    if (is_file($picture) && filemtime($picture) > time() - (int) floor($preview_ms / 2000)) {
        header('Retry-After: ' . (int) ceil($preview_ms / 2000));
        psrpt_rooms_answer(429, ['ok' => false, 'error' => 'one picture at a time', 'code' => 'rate_limited', 'poll' => $poll, 'preview' => $preview_ms]);
    }
    $encoded = $_POST['image'] ?? '';
    if (!is_string($encoded) || strlen($encoded) > (int) ceil($preview_max * 4 / 3) + 4
        || !preg_match('/^[A-Za-z0-9_-]+$/', $encoded)) {
        psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed picture']);
    }
    $data = base64_decode(strtr($encoded, '-_', '+/'), true);
    if (!psrpt_rooms_jpeg($data, $preview_max, 640, 360)) {
        psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed picture']);
    }
    $temp = $picture . '.' . bin2hex(random_bytes(4)) . '.tmp';
    if (@file_put_contents($temp, $data) === false || !@rename($temp, $picture)) {
        @unlink($temp);
        psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list cannot keep the picture']);
    }
    psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll, 'preview' => $preview_ms]);
}

$title = psrpt_rooms_text($_POST['title'] ?? null, 60);
$lang = $_POST['lang'] ?? '';
$game = psrpt_rooms_text($_POST['game'] ?? '', 80);
$host = psrpt_rooms_text($_POST['host'] ?? null, 32);
$players = psrpt_rooms_int($_POST['players'] ?? null, 1, 9);
$max = psrpt_rooms_int($_POST['max'] ?? null, 2, 9);
$policy = $_POST['policy'] ?? '';
$control = $_POST['control'] ?? 'all';
$web = $_POST['web'] ?? '0';
$invite = $_POST['invite'] ?? '';
$proto = psrpt_rooms_int($_POST['proto'] ?? null, 1, 255);
$title_id = $_POST['title_id'] ?? '';
$cover = $_POST['cover'] ?? '';
// Who plays, as initials only: never names
$initials = $_POST['initials'] ?? '';
if ($title === null || $title === '' || $host === null || $host === '' || $game === null
    || !is_string($lang) || !preg_match('/^([a-z]{2})?$/', $lang)
    || $players === null || $max === null || $players > $max || $proto === null
    || !in_array($policy, ['ask', 'watch', 'open'], true)
    || !in_array($control, ['all', 'turns', 'host'], true)
    || ($web !== '0' && $web !== '1')
    || !is_string($title_id) || !preg_match('/^([A-Z]{4}[0-9]{5})?$/', $title_id)
    || !is_string($cover) || !preg_match('/^(https:\/\/image\.api\.playstation\.com\/[A-Za-z0-9._~\/%-]{1,300})?$/', $cover)
    || !is_string($initials) || !preg_match('/^[\p{L}\p{N}?]{0,9}$/u', $initials) || mb_strlen($initials, 'UTF-8') > $players
    // No address in a listed invite: the host is found through the meeting point
    || !is_string($invite) || !preg_match('/^psrpt:\/\/-\/[A-Za-z0-9_-]{22}(\?meet=[A-Za-z0-9._~:\/%-]{1,200})?$/', $invite)) {
    psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed room']);
}

if ($existing === null) {
    $count = 0;
    foreach (new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS) as $file) {
        $count += $file->isFile() && $file->getExtension() === 'room' ? 1 : 0;
    }
    if ($count >= $max_rooms) {
        $count = count(psrpt_rooms_read($dir, $ttl)); // takes away what is old
        if ($count >= $max_rooms) {
            header('Retry-After: 60');
            psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list is full, try again in a minute']);
        }
    }
}

$room = [
    'id' => $id,
    'token' => $token_hash,
    'title' => $title,
    'lang' => $lang,
    'game' => $game,
    'host' => $host,
    'players' => $players,
    'max' => $max,
    'policy' => $policy,
    'control' => $control,
    'web' => $web === '1',
    'invite' => $invite,
    'proto' => $proto,
    'title_id' => $title_id,
    'cover' => $cover,
    'initials' => $initials,
    'created' => $existing !== null ? (int) $existing['created'] : time(),
    't' => time(),
];
$temp = $path . '.' . bin2hex(random_bytes(4)) . '.tmp';
if (@file_put_contents($temp, json_encode($room, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)) === false || !@rename($temp, $path)) {
    @unlink($temp);
    psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list cannot keep the room']);
}
psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll, 'preview' => $preview_ms]);
