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
//      "policy","web","invite","proto","since"}, ...]}
//
// A host (POST, form fields):
//     v=1 action=announce id=<16 hex> token=<32 hex> title= lang= game= host= players= max=
//         policy=ask|watch|open web=0|1 invite=<psrpt://-/KEY[?meet=...]> proto=<protocol version>
//     v=1 action=remove id=<16 hex> token=<32 hex>
//   answer: {"v":1,"ok":true,"poll":15000} or {"v":1,"ok":false,"error":"..."}
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
/** Rooms on the list at the same time. One takes about 600 bytes. */
const PSRPT_ROOMS_MAX = 500;
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

function psrpt_rooms_answer(int $status, array $body): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Content-Type-Options: nosniff');
    header('X-Robots-Tag: noindex, nofollow');
    $cors = (string) psrpt_rooms_setting('PSRPT_ROOMS_CORS', PSRPT_ROOMS_CORS);
    if ($cors !== '' && preg_match('/^(\*|https?:\/\/[A-Za-z0-9.:\[\]-]+)$/', $cors)) {
        header('Access-Control-Allow-Origin: ' . $cors);
    }
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

/** The rooms that are still there; old files are removed on the way. */
function psrpt_rooms_read(string $dir, int $ttl): array
{
    $rooms = [];
    $now = time();
    foreach (new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS) as $file) {
        if (!$file->isFile() || $file->getExtension() !== 'room') {
            continue;
        }
        if ($file->getMTime() < $now - $ttl) {
            @unlink($file->getPathname());
            continue;
        }
        $room = json_decode((string) @file_get_contents($file->getPathname()), true);
        if (is_array($room) && isset($room['id'], $room['t']) && $now - (int) $room['t'] <= $ttl) {
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

$dir = psrpt_rooms_dir();
if ($dir === null) {
    psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list has nowhere to keep rooms']);
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method !== 'POST') {
    if (($_GET['v'] ?? '1') !== '1') {
        psrpt_rooms_answer(200, ['ok' => false, 'error' => 'this list only knows version 1 of the protocol', 'rooms' => []]);
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
            'web' => $room['web'],
            'invite' => $room['invite'],
            'proto' => $room['proto'],
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
    || ($action !== 'announce' && $action !== 'remove')) {
    psrpt_rooms_answer(400, ['ok' => false, 'error' => 'malformed request']);
}
if ($version !== '1') {
    psrpt_rooms_answer(200, ['ok' => false, 'error' => 'this list only knows version 1 of the protocol']);
}

// Named after a hash: nothing a client sends becomes part of a path
$path = $dir . DIRECTORY_SEPARATOR . hash('sha256', $id) . '.room';
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
    }
    psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll]);
}

$title = psrpt_rooms_text($_POST['title'] ?? null, 60);
$lang = $_POST['lang'] ?? '';
$game = psrpt_rooms_text($_POST['game'] ?? '', 80);
$host = psrpt_rooms_text($_POST['host'] ?? null, 32);
$players = psrpt_rooms_int($_POST['players'] ?? null, 1, 9);
$max = psrpt_rooms_int($_POST['max'] ?? null, 2, 9);
$policy = $_POST['policy'] ?? '';
$web = $_POST['web'] ?? '0';
$invite = $_POST['invite'] ?? '';
$proto = psrpt_rooms_int($_POST['proto'] ?? null, 1, 255);
if ($title === null || $title === '' || $host === null || $host === '' || $game === null
    || !is_string($lang) || !preg_match('/^([a-z]{2})?$/', $lang)
    || $players === null || $max === null || $players > $max || $proto === null
    || !in_array($policy, ['ask', 'watch', 'open'], true)
    || ($web !== '0' && $web !== '1')
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
        psrpt_rooms_read($dir, $ttl); // takes away what is old
        $count = iterator_count(new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS));
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
    'web' => $web === '1',
    'invite' => $invite,
    'proto' => $proto,
    'created' => $existing !== null ? (int) $existing['created'] : time(),
    't' => time(),
];
$temp = $path . '.' . bin2hex(random_bytes(4)) . '.tmp';
if (@file_put_contents($temp, json_encode($room, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)) === false || !@rename($temp, $path)) {
    @unlink($temp);
    psrpt_rooms_answer(503, ['ok' => false, 'error' => 'the list cannot keep the room']);
}
psrpt_rooms_answer(200, ['ok' => true, 'poll' => $poll]);
