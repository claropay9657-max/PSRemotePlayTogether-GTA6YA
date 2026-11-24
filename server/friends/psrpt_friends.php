<?php
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// PSRemotePlayTogether friends server.
//
// Keeps what friends need to find and reach each other while no session runs:
// every player's card (their public keys and devices), the prekeys that let a
// friend start a conversation with a device that is offline, and small sealed
// notes in boxes: friend requests, messages, group messages, statuses, and what
// two devices hand each other while one links the other.
//
// It cannot read any of it. Everything but the cards and prekeys is sealed end to
// end between the players' devices, and a box's name cannot be turned back into
// whom it belongs to. It checks the signatures on cards, and on every request
// that changes what belongs to a player, so that nobody but the player's own
// devices can change it. It stores no IP addresses and writes no log of its own:
// the limits per address are counters under a keyed hash that last an hour.
//
// One file, PHP 7.4 or newer with the openssl extension (curl is used for push
// messages when it is there). README.md next to it says how to install it. The
// protocol, with every operation: docs/friends-protocol.md of the project,
// https://github.com/Crypto90/PSRemotePlayTogether
//
// Request (POST, form fields): op=<operation> and what the operation needs.
// Answer (application/json):
//     {"ok":true,"poll":30000,...}  or  {"ok":false,"poll":30000,"error":"<a word>"}
// From the command line, "php psrpt_friends.php sweep" removes what has expired,
// for a cron job; the script also does it by itself now and then.

declare(strict_types=1);

// ------------------------------------------------------------------ settings
//
// Each one can also be set as an environment variable of the same name, or in a file
// psrpt_friends.config.php next to this one that returns them as an array, for example
//     <?php return ['PSRPT_FRIENDS_DIR' => '/var/lib/psrpt-friends'];
// (an environment variable wins over the file, the file over the value here), so that this file
// can be replaced by a newer one as it is.

/**
 * Folder for everything the server keeps. Empty: "psrpt-friends" in the system's temporary
 * folder, which some systems empty when they restart: set it on a real server, because cards
 * and undelivered messages are meant to last. Must not be reachable from the web.
 */
const PSRPT_FRIENDS_DIR = '';
/** How often a device is asked to come back, in milliseconds. Raise it if the server gets busy. */
const PSRPT_FRIENDS_POLL_MS = 30000;
/**
 * For players in a web browser: the page is served from somewhere else (GitHub Pages) and may
 * only read the answers if they carry "Access-Control-Allow-Origin". '*' sends it for every
 * page (fine: there are no cookies here, and what a page can read it could read without one),
 * an address such as 'https://example.org' for that page only. Empty sends none: for a server
 * whose reverse proxy adds the header already, because two of them count as none.
 */
const PSRPT_FRIENDS_CORS = '';
/**
 * Behind a reverse proxy every request comes from the proxy's address, and the limits per
 * address would count everybody together. Name the header in which the proxy passes the
 * client's address ('X-Forwarded-For' or 'X-Real-IP'); of X-Forwarded-For the last address is
 * taken, the one the proxy added. Only ever used for the counters below, never stored.
 */
const PSRPT_FRIENDS_CLIENT_HEADER = '';
/** The ban list, one fingerprint per line. Empty: banned.txt in the data folder. */
const PSRPT_FRIENDS_BANNED = '';

/** Days a card stays when none of its devices has been here. */
const PSRPT_FRIENDS_CARD_DAYS = 365;
/** Largest card, in bytes: ten devices and a sealed name and picture of 24 KB. */
const PSRPT_FRIENDS_MAX_CARD = 49152;
/** New identities from one address in an hour. */
const PSRPT_FRIENDS_IDENTITIES_PER_HOUR = 10;
/** One-time prekeys kept per device; the oldest go when more come. */
const PSRPT_FRIENDS_MAX_PREKEYS = 100;
/** Bundles (which take one-time prekeys away) one address may fetch in an hour. */
const PSRPT_FRIENDS_BUNDLES_PER_HOUR = 200;
/** Days a friend request waits in a request box. */
const PSRPT_FRIENDS_REQUEST_DAYS = 14;
/** Requests waiting in one request box. */
const PSRPT_FRIENDS_MAX_REQUESTS = 50;
/** Largest request, in bytes. A request carries the sender's card, picture included. */
const PSRPT_FRIENDS_MAX_REQUEST_NOTE = 65536;
/** Requests one address may drop in an hour. A request to a player with three devices is three. */
const PSRPT_FRIENDS_REQUESTS_PER_HOUR = 30;
/** Days an undelivered message waits in an inbox. */
const PSRPT_FRIENDS_INBOX_DAYS = 30;
/** Messages waiting in one device's inbox. */
const PSRPT_FRIENDS_MAX_INBOX = 1000;
/** Largest message in an inbox or a group box, in bytes. */
const PSRPT_FRIENDS_MAX_NOTE = 8192;
/** Write tokens per identity: one for each friend, and one for its own devices. */
const PSRPT_FRIENDS_MAX_TOKENS = 1000;
/** Seconds a status note counts as fresh. */
const PSRPT_FRIENDS_STATUS_SECONDS = 180;
/** Largest status note, in bytes. */
const PSRPT_FRIENDS_MAX_STATUS = 2048;
/** Days a group message stays, and a group box that nobody reads or writes. */
const PSRPT_FRIENDS_GROUP_DAYS = 30;
/** Messages kept in one group box; the oldest go when more come. */
const PSRPT_FRIENDS_MAX_GROUP = 2000;
/**
 * New write tokens or owner keys for one group box, from one address, in an hour: an admin who
 * was removed cannot keep the box busy (the remaining admins make a new owner key at once).
 */
const PSRPT_FRIENDS_GROUP_CHANGES_PER_HOUR = 20;
/** New group boxes from one address in an hour. */
const PSRPT_FRIENDS_GROUPS_PER_HOUR = 20;
/** Seconds a link box stays after its last write. */
const PSRPT_FRIENDS_LINK_SECONDS = 120;
/** Largest link box, in bytes, all parts together. */
const PSRPT_FRIENDS_MAX_LINK = 4194304;
/** New link boxes from one address in an hour. */
const PSRPT_FRIENDS_LINKS_PER_HOUR = 10;
/** Days a report is kept for the operator. */
const PSRPT_FRIENDS_REPORT_DAYS = 90;
/** Largest report, in bytes. */
const PSRPT_FRIENDS_MAX_REPORT = 65536;
/** Reports from one address in an hour. */
const PSRPT_FRIENDS_REPORTS_PER_HOUR = 5;
/**
 * Where push messages may go, comma-separated: host names, '*.' for every name under a
 * domain. These are the push services of Chrome, Edge, Firefox and Safari. '*' allows any
 * host on the public internet; the server never sends to an address of a private network.
 */
const PSRPT_FRIENDS_PUSH_HOSTS = 'fcm.googleapis.com,updates.push.services.mozilla.com,*.push.services.mozilla.com,*.notify.windows.com,web.push.apple.com,*.push.apple.com';
/** Who runs this server, for the push services (the "sub" of a push message): an https: or mailto: address. */
const PSRPT_FRIENDS_PUSH_CONTACT = 'https://github.com/Crypto90/PSRemotePlayTogether';
/** Megabytes the disk keeps free: below that, nothing new is taken. */
const PSRPT_FRIENDS_MIN_FREE_MB = 100;
/**
 * '1' only for the project's tests, never on a real server: the clock can then be moved on
 * through the file PSRPT_FRIENDS_TEST_CLOCK names, and push messages may go to the test's own
 * address over plain http.
 */
const PSRPT_FRIENDS_TEST = '';

/** Fixed by the protocol (docs/friends-protocol.md). */
const PSRPT_FRIENDS_TIME_WINDOW = 300;      // a signed request's time may be off by five minutes
const PSRPT_FRIENDS_REPLAY_SECONDS = 600;   // a signed message is remembered ten minutes
const PSRPT_FRIENDS_MAX_LINK_PART = 263168; // a part of a link box: 256 KB and what sealing adds
const PSRPT_FRIENDS_MAX_STATUS_BOXES = 400; // status boxes asked for at once (two per friend: today, yesterday)
const PSRPT_FRIENDS_MAX_LIST = 200;         // notes in one answer
const PSRPT_FRIENDS_MAX_ANSWER = 2097152;   // bytes of notes in one answer
const PSRPT_FRIENDS_MAX_PUSHES = 50;        // push messages one request sends
const PSRPT_FRIENDS_MAX_INBOX_ACK = 1000;   // ids taken away at once

/** The settings of psrpt_friends.config.php next to this file, if there is one. */
function psrpt_friends_config(): array
{
    static $config = null;
    if ($config === null) {
        $file = __DIR__ . DIRECTORY_SEPARATOR . 'psrpt_friends.config.php';
        $read = is_file($file) ? include $file : [];
        $config = is_array($read) ? $read : [];
    }
    return $config;
}

function psrpt_friends_setting(string $name)
{
    $default = constant($name);
    $value = getenv($name);
    if ($value === false || $value === '') {
        $config = psrpt_friends_config();
        $value = isset($config[$name]) && is_scalar($config[$name]) ? (string) $config[$name] : '';
    }
    if ($value === '') {
        return $default;
    }
    return is_int($default) ? max(1, (int) $value) : $value;
}

function psrpt_friends_int(string $name): int
{
    return (int) psrpt_friends_setting($name);
}

function psrpt_friends_testing(): bool
{
    return (string) psrpt_friends_setting('PSRPT_FRIENDS_TEST') === '1';
}

/** Seconds the tests have moved the clock on; always 0 on a real server. */
function psrpt_friends_offset(): int
{
    static $offset = null;
    if ($offset === null) {
        $offset = 0;
        $clock = getenv('PSRPT_FRIENDS_TEST_CLOCK');
        if (psrpt_friends_testing() && is_string($clock) && $clock !== '') {
            $value = @file_get_contents($clock);
            if (is_string($value) && preg_match('/^\s*-?[0-9]{1,10}\s*$/', $value)) {
                $offset = (int) trim($value);
            }
        }
    }
    return $offset;
}

function psrpt_friends_now(): int
{
    return time() + psrpt_friends_offset();
}

function psrpt_friends_now_ms(): int
{
    return (int) floor(microtime(true) * 1000) + psrpt_friends_offset() * 1000;
}

// ------------------------------------------------------------------ answers

$GLOBALS['psrpt_friends_locks'] = [];   // file locks held, let go before answering
$GLOBALS['psrpt_friends_pushes'] = [];  // "fp:device" (hex) to send a push message to, once answered
$GLOBALS['psrpt_friends_sweep'] = false; // whether to sweep everything, once answered
$GLOBALS['psrpt_friends_fields'] = [];

function psrpt_friends_cors(): void
{
    $cors = (string) psrpt_friends_setting('PSRPT_FRIENDS_CORS');
    if ($cors !== '' && preg_match('/^(\*|https?:\/\/[A-Za-z0-9.:\[\]-]+)$/', $cors)) {
        header('Access-Control-Allow-Origin: ' . $cors);
    }
}

function psrpt_friends_answer(int $status, array $body): void
{
    foreach ($GLOBALS['psrpt_friends_locks'] as $handle) {
        @flock($handle, LOCK_UN);
        @fclose($handle);
    }
    $GLOBALS['psrpt_friends_locks'] = [];
    $json = (string) json_encode(['ok' => $body['ok'] ?? false, 'poll' => psrpt_friends_int('PSRPT_FRIENDS_POLL_MS')] + $body,
        JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    $later = $GLOBALS['psrpt_friends_pushes'] || $GLOBALS['psrpt_friends_sweep'];
    if (PHP_SAPI !== 'cli') {
        http_response_code($status);
        header('Content-Type: application/json; charset=utf-8');
        header('Cache-Control: no-store');
        header('X-Content-Type-Options: nosniff');
        header('X-Robots-Tag: noindex, nofollow');
        psrpt_friends_cors();
        header('Content-Length: ' . strlen($json));
        if ($later) {
            // The answer goes out first; what follows must not keep the app waiting
            ignore_user_abort(true);
            header('Connection: close');
        }
    }
    echo $json;
    if ($later) {
        if (function_exists('fastcgi_finish_request')) {
            fastcgi_finish_request();
        } else {
            while (ob_get_level() > 0) {
                @ob_end_flush();
            }
            flush();
        }
        // Taken out first: should any of it end in an answer of its own, that one does nothing more
        $pushes = array_keys($GLOBALS['psrpt_friends_pushes']);
        $sweep = $GLOBALS['psrpt_friends_sweep'];
        $GLOBALS['psrpt_friends_pushes'] = [];
        $GLOBALS['psrpt_friends_sweep'] = false;
        if ($pushes) {
            psrpt_friends_push_all($pushes);
        }
        if ($sweep) {
            psrpt_friends_sweep(true);
        }
    }
    exit;
}

function psrpt_friends_ok(array $more = []): void
{
    psrpt_friends_answer(200, ['ok' => true] + $more);
}

/**
 * A refusal. Answered with 200 on purpose for everything an honest app can run into (a clock
 * that is off, a request sent twice, a full box): many reverse proxies ban an address after a
 * number of 4xx answers. The error is one word the apps know (README.md lists them).
 */
function psrpt_friends_fail(string $error, int $status = 200, array $more = []): void
{
    psrpt_friends_answer($status, ['ok' => false, 'error' => $error] + $more);
}

/** What no app sends: 400, which a proxy may count. */
function psrpt_friends_malformed(): void
{
    psrpt_friends_fail('malformed', 400);
}

// ------------------------------------------------------------------ bytes and keys

function psrpt_friends_b64(string $bytes): string
{
    return rtrim(strtr(base64_encode($bytes), '+/', '-_'), '=');
}

/** base64url without padding, written the one way it can be written; null otherwise. */
function psrpt_friends_unb64(string $text): ?string
{
    if (!preg_match('/^[A-Za-z0-9_-]*$/', $text) || strlen($text) % 4 === 1) {
        return null;
    }
    $bytes = base64_decode(strtr($text, '-_', '+/'), true);
    if ($bytes === false || psrpt_friends_b64($bytes) !== $text) {
        return null;
    }
    return $bytes;
}

/** A form field as sent, or null. */
function psrpt_friends_field(string $name): ?string
{
    return $GLOBALS['psrpt_friends_fields'][$name] ?? null;
}

/** A base64url field, decoded, of $min to $max bytes. Missing is fine only when $min is 0. */
function psrpt_friends_bytes(string $name, int $min, int $max): string
{
    $text = psrpt_friends_field($name);
    if ($text === null || $text === '') {
        if ($min === 0) {
            return '';
        }
        psrpt_friends_malformed();
    }
    // A note too big is something an honest client can send (a long message): answered with 200
    if (strlen($text) > intdiv($max * 4 + 2, 3)) {
        $name === 'note' ? psrpt_friends_fail('too_big') : psrpt_friends_malformed();
    }
    $bytes = psrpt_friends_unb64($text);
    if ($bytes === null || strlen($bytes) < $min) {
        psrpt_friends_malformed();
    }
    if (strlen($bytes) > $max) {
        $name === 'note' ? psrpt_friends_fail('too_big') : psrpt_friends_malformed();
    }
    return $bytes;
}

/** A P-256 public key (the uncompressed point, 65 bytes) for OpenSSL; null if it is no point on the curve. */
function psrpt_friends_public_key(string $point)
{
    if (strlen($point) !== 65 || $point[0] !== "\x04") {
        return null;
    }
    // SubjectPublicKeyInfo: id-ecPublicKey, prime256v1, the point
    $der = hex2bin('3059301306072a8648ce3d020106082a8648ce3d030107034200') . $point;
    $key = @openssl_pkey_get_public("-----BEGIN PUBLIC KEY-----\n" . chunk_split(base64_encode($der), 64, "\n") . "-----END PUBLIC KEY-----\n");
    while (openssl_error_string() !== false) {
        // OpenSSL keeps its errors in a queue; this one is answered already
    }
    return $key === false ? null : $key;
}

function psrpt_friends_valid_point(string $point): bool
{
    return psrpt_friends_public_key($point) !== null;
}

/** ECDSA P-256 with SHA-256; the signature as r || s (64 bytes), as WebCrypto makes it. */
function psrpt_friends_verify(string $point, string $message, string $signature): bool
{
    $key = psrpt_friends_public_key($point);
    if ($key === null || strlen($signature) !== 64) {
        return false;
    }
    // OpenSSL wants DER: SEQUENCE { INTEGER r, INTEGER s }, each as short as it can be
    $integer = function (string $value): string {
        $value = ltrim($value, "\x00");
        if ($value === '' || (ord($value[0]) & 0x80)) {
            $value = "\x00" . $value;
        }
        return "\x02" . chr(strlen($value)) . $value;
    };
    $body = $integer(substr($signature, 0, 32)) . $integer(substr($signature, 32, 32));
    $result = @openssl_verify($message, "\x30" . chr(strlen($body)) . $body, $key, OPENSSL_ALGO_SHA256);
    while (openssl_error_string() !== false) {
        // as above
    }
    return $result === 1;
}

/** OpenSSL's DER signature as r || s, for the JWT of a push message. */
function psrpt_friends_raw_signature(string $der): ?string
{
    $length = strlen($der);
    if ($length < 8 || $der[0] !== "\x30" || ord($der[1]) !== $length - 2) {
        return null;
    }
    $out = '';
    $position = 2;
    for ($i = 0; $i < 2; $i++) {
        if ($position + 2 > $length || $der[$position] !== "\x02") {
            return null;
        }
        $size = ord($der[$position + 1]);
        $value = ltrim(substr($der, $position + 2, $size), "\x00");
        if (strlen($value) > 32) {
            return null;
        }
        $out .= str_pad($value, 32, "\x00", STR_PAD_LEFT);
        $position += 2 + $size;
    }
    return $position === $length ? $out : null;
}

/**
 * Reads a card and checks everything in it (docs/friends-protocol.md, "The card"; the same as
 * DecodeCard in coplay/src/friends_crypto.cpp): the structure, ikd_sig, card_sig and every
 * device's spk_sig, all with the card's own IKs. Null when anything is wrong.
 */
function psrpt_friends_parse_card(string $card): ?array
{
    $size = strlen($card);
    // version, IKs, IKd, ikd_sig, seq, device count; per device id, kind, spk_id, SPK, spk_sig; profile; card_sig
    if ($size < 200 + 142 + 2 + 64 || $card[0] !== "\x01") {
        return null;
    }
    $iks = substr($card, 1, 65);
    $ikd = substr($card, 66, 65);
    $ikd_sig = substr($card, 131, 64);
    $seq = unpack('N', substr($card, 195, 4))[1];
    $count = ord($card[199]);
    if ($count < 1 || $count > 10) {
        return null;
    }
    $position = 200;
    $devices = [];
    for ($i = 0; $i < $count; $i++) {
        if ($position + 142 > $size) {
            return null;
        }
        $devices[] = [
            'id' => substr($card, $position, 8),
            'spk_id' => unpack('N', substr($card, $position + 9, 4))[1],
            'spk' => substr($card, $position + 13, 65),
            'spk_sig' => substr($card, $position + 78, 64),
        ];
        $position += 142;
    }
    if ($position + 2 > $size) {
        return null;
    }
    $profile = unpack('n', substr($card, $position, 2))[1];
    if ($position + 2 + $profile + 64 !== $size) {
        return null;
    }
    if (!psrpt_friends_verify($iks, 'psrpt ikd' . $ikd, $ikd_sig) || !psrpt_friends_valid_point($ikd)) {
        return null;
    }
    $fp = substr(hash('sha256', 'psrpt id' . $iks . $ikd, true), 0, 16);
    if (!psrpt_friends_verify($iks, 'psrpt card' . substr($card, 0, $size - 64), substr($card, $size - 64))) {
        return null;
    }
    $ids = [];
    foreach ($devices as $device) {
        $message = 'psrpt spk' . $fp . $device['id'] . pack('N', $device['spk_id']) . $device['spk'];
        if (!psrpt_friends_valid_point($device['spk']) || !psrpt_friends_verify($iks, $message, $device['spk_sig'])) {
            return null;
        }
        $ids[] = bin2hex($device['id']);
    }
    // Two devices of one name would share an inbox and prekeys
    if (count(array_unique($ids)) !== count($ids)) {
        return null;
    }
    return ['fp' => $fp, 'iks' => $iks, 'seq' => $seq, 'devices' => $ids];
}

// ------------------------------------------------------------------ files

/** The data folder, created on first use; null if that is not possible. */
function psrpt_friends_dir(): ?string
{
    static $dir = false;
    if ($dir !== false) {
        return $dir;
    }
    $dir = (string) psrpt_friends_setting('PSRPT_FRIENDS_DIR');
    if ($dir === '') {
        $dir = rtrim(sys_get_temp_dir(), '/\\') . DIRECTORY_SEPARATOR . 'psrpt-friends';
    }
    $dir = rtrim($dir, '/\\');
    if (!is_dir($dir) && !@mkdir($dir, 0700, true) && !is_dir($dir)) {
        $dir = null;
    } elseif (!is_writable($dir)) {
        $dir = null;
    } elseif (!is_file($dir . DIRECTORY_SEPARATOR . '.htaccess')) {
        // A second lock for a folder that ended up in the web root of an Apache server
        @file_put_contents($dir . DIRECTORY_SEPARATOR . '.htaccess', "Require all denied\n");
    }
    return $dir;
}

/** A path in the data folder. Every part is a word of the script's or the hex of checked bytes, never what a client wrote. */
function psrpt_friends_path(string ...$parts): string
{
    return psrpt_friends_dir() . DIRECTORY_SEPARATOR . implode(DIRECTORY_SEPARATOR, array_map('basename', $parts));
}

function psrpt_friends_mkdir(string $folder): void
{
    if (!is_dir($folder) && !@mkdir($folder, 0700, true) && !is_dir($folder)) {
        psrpt_friends_fail('cannot_write', 503);
    }
}

/**
 * Locks a folder (through a file ".lock" in it, created with the folder) until the answer.
 * The sweep may remove the folder between opening and locking: then what we hold is no longer
 * the folder's lock, and it is tried again.
 */
function psrpt_friends_lock(string $folder)
{
    $path = $folder . DIRECTORY_SEPARATOR . '.lock';
    for ($attempt = 0; $attempt < 3; $attempt++) {
        psrpt_friends_mkdir($folder);
        $handle = @fopen($path, 'c');
        if (!$handle) {
            continue;
        }
        if (!flock($handle, LOCK_EX)) {
            fclose($handle);
            break;
        }
        clearstatcache(true, $path);
        $held = fstat($handle);
        $named = @stat($path);
        if ($held && $named && $held['ino'] === $named['ino']) {
            $GLOBALS['psrpt_friends_locks'][] = $handle;
            return $handle;
        }
        flock($handle, LOCK_UN);
        fclose($handle);
    }
    psrpt_friends_fail('cannot_write', 503);
}

function psrpt_friends_unlock($handle): void
{
    $index = array_search($handle, $GLOBALS['psrpt_friends_locks'], true);
    if ($index !== false) {
        unset($GLOBALS['psrpt_friends_locks'][$index]);
    }
    flock($handle, LOCK_UN);
    fclose($handle);
}

/** Writes a file whole or not at all: readers never see half of it. */
function psrpt_friends_put(string $path, string $data): void
{
    $temp = $path . '.' . bin2hex(random_bytes(4)) . '.tmp';
    if (@file_put_contents($temp, $data) === false || !@rename($temp, $path)) {
        @unlink($temp);
        psrpt_friends_fail('cannot_write', 503);
    }
}

function psrpt_friends_read_json(string $path): ?array
{
    $raw = @file_get_contents($path);
    if (!is_string($raw) || $raw === '') {
        return null;
    }
    $data = json_decode($raw, true);
    return is_array($data) ? $data : null;
}

function psrpt_friends_put_json(string $path, array $data): void
{
    psrpt_friends_put($path, (string) json_encode($data, JSON_UNESCAPED_SLASHES));
}

/** Removes a file, or a folder with everything in it. */
function psrpt_friends_remove(string $path): void
{
    if (is_dir($path) && !is_link($path)) {
        foreach (@scandir($path) ?: [] as $name) {
            if ($name !== '.' && $name !== '..') {
                psrpt_friends_remove($path . DIRECTORY_SEPARATOR . $name);
            }
        }
        @rmdir($path);
    } else {
        @unlink($path);
    }
}

/** The names in a folder that match a pattern, sorted. */
function psrpt_friends_names(string $folder, string $pattern): array
{
    $names = [];
    foreach (@scandir($folder) ?: [] as $name) {
        if (preg_match($pattern, $name)) {
            $names[] = $name;
        }
    }
    return $names;
}

/** Refuses anything new when the disk is nearly full. */
function psrpt_friends_room_left(): void
{
    $free = @disk_free_space((string) psrpt_friends_dir());
    if (is_float($free) && $free < psrpt_friends_int('PSRPT_FRIENDS_MIN_FREE_MB') * 1048576.0) {
        psrpt_friends_fail('server_full', 503);
    }
}

/**
 * The server's own keys, made the first time they are needed: the VAPID key pair for push
 * messages, and the key under which the counters per address are hashed.
 */
function psrpt_friends_keys(): array
{
    static $keys = null;
    if ($keys !== null) {
        return $keys;
    }
    $path = psrpt_friends_path('server.json');
    $valid = function ($data): bool {
        return is_array($data) && is_string($data['vapid_private'] ?? null) && is_string($data['vapid_public'] ?? null)
            && is_string($data['salt'] ?? null) && strlen($data['salt']) === 64;
    };
    $data = psrpt_friends_read_json($path);
    if (!$valid($data)) {
        $lock = psrpt_friends_lock((string) psrpt_friends_dir());
        $data = psrpt_friends_read_json($path);
        if (!$valid($data)) {
            $pair = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
            $details = $pair ? openssl_pkey_get_details($pair) : false;
            if (!$pair || !$details || !openssl_pkey_export($pair, $pem)) {
                psrpt_friends_fail('cannot_write', 503);
            }
            $data = [
                'vapid_private' => $pem,
                'vapid_public' => psrpt_friends_b64("\x04" . str_pad($details['ec']['x'], 32, "\x00", STR_PAD_LEFT)
                    . str_pad($details['ec']['y'], 32, "\x00", STR_PAD_LEFT)),
                'salt' => bin2hex(random_bytes(32)),
            ];
            $temp = $path . '.' . bin2hex(random_bytes(4)) . '.tmp';
            if (@file_put_contents($temp, json_encode($data)) === false || !@chmod($temp, 0600) || !@rename($temp, $path)) {
                @unlink($temp);
                psrpt_friends_fail('cannot_write', 503);
            }
        }
        psrpt_friends_unlock($lock);
    }
    $keys = $data;
    return $keys;
}

// ------------------------------------------------------------------ limits, bans, signatures

/** The client's address, for the counters only. IPv6 by its first 64 bits: one household has at least that many. */
function psrpt_friends_address(): string
{
    $address = '';
    $header = (string) psrpt_friends_setting('PSRPT_FRIENDS_CLIENT_HEADER');
    if ($header !== '') {
        $passed = $_SERVER['HTTP_' . strtoupper(str_replace('-', '_', $header))] ?? '';
        $list = explode(',', is_string($passed) ? $passed : '');
        $address = trim((string) end($list));
    }
    if (filter_var($address, FILTER_VALIDATE_IP) === false) {
        $address = (string) ($_SERVER['REMOTE_ADDR'] ?? '');
    }
    if (filter_var($address, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6) !== false) {
        $address = bin2hex(substr((string) inet_pton($address), 0, 8));
    }
    return $address;
}

/**
 * Counts one more of $what from the client's address in this hour, or refuses when there were
 * $setting of them already. The counter's file is named by a keyed hash of the address and
 * removed after the hour; the address itself is written nowhere.
 */
function psrpt_friends_take(string $what, string $setting): void
{
    $hour = intdiv(psrpt_friends_now(), 3600);
    $folder = psrpt_friends_path('rate', (string) $hour);
    psrpt_friends_mkdir($folder);
    $keys = psrpt_friends_keys();
    $path = $folder . DIRECTORY_SEPARATOR . hash_hmac('sha256', $hour . ' ' . $what . ' ' . psrpt_friends_address(), $keys['salt']);
    $handle = @fopen($path, 'c+');
    if (!$handle || !flock($handle, LOCK_EX)) {
        psrpt_friends_fail('cannot_write', 503);
    }
    $count = (int) stream_get_contents($handle);
    if ($count >= psrpt_friends_int($setting)) {
        flock($handle, LOCK_UN);
        fclose($handle);
        psrpt_friends_fail('rate_limited', 200, ['retry' => 3600 - psrpt_friends_now() % 3600]);
    }
    rewind($handle);
    ftruncate($handle, 0);
    fwrite($handle, (string) ($count + 1));
    fflush($handle);
    flock($handle, LOCK_UN);
    fclose($handle);
}

/** Whether the operator has banned an identity (banned.txt: one fingerprint per line, base64url or hex, # for comments). */
function psrpt_friends_banned(string $fp): bool
{
    static $banned = null;
    if ($banned === null) {
        $banned = [];
        $path = (string) psrpt_friends_setting('PSRPT_FRIENDS_BANNED');
        if ($path === '') {
            $path = psrpt_friends_path('banned.txt');
        }
        $text = @file_get_contents($path);
        foreach (is_string($text) ? preg_split('/\r\n|\r|\n/', $text) : [] as $line) {
            $line = trim((string) preg_replace('/#.*/', '', $line));
            if ($line === '') {
                continue;
            }
            $word = preg_split('/\s+/', $line)[0];
            $bytes = preg_match('/^[0-9a-fA-F]{32}$/', $word) ? hex2bin($word) : psrpt_friends_unb64($word);
            if (is_string($bytes) && strlen($bytes) === 16) {
                $banned[$bytes] = true;
            }
        }
    }
    return isset($banned[$fp]);
}

/**
 * Who wrote an envelope (byte 2 on) or a group message (byte 17 on), as far as it says: a
 * banned identity cannot drop anything either. Somebody who lies about it gains nothing,
 * because the receivers open a note only in the conversation of the identity it names.
 */
function psrpt_friends_refuse_banned_sender(string $note, int $offset): void
{
    if (strlen($note) >= $offset + 16 && $note[0] === "\x01" && psrpt_friends_banned(substr($note, $offset, 16))) {
        psrpt_friends_fail('banned');
    }
}

/**
 * Checks a signed request (docs/friends-protocol.md, "Signed requests"): the time, the
 * signature with $point over the message RequestMessage makes, and that the same message has
 * not come before. The message is remembered by its hash, not by the signature, because an
 * ECDSA signature can be turned into another valid one for the same message.
 */
function psrpt_friends_check_signed(string $op, string $point): void
{
    $t = psrpt_friends_field('t');
    if ($t === null || !preg_match('/^[1-9][0-9]{0,11}$/', $t)) {
        psrpt_friends_malformed();
    }
    $signature = psrpt_friends_bytes('sig', 64, 64);
    // The nonce (8 random bytes), so that two requests alike in the same second are two messages;
    // signed like every other field
    $nonce = psrpt_friends_field('n');
    if ($nonce !== null && ($nonce === '' || strlen($nonce) > 43 || psrpt_friends_unb64($nonce) === null)) {
        psrpt_friends_malformed();
    }
    $now = psrpt_friends_now();
    if (abs((int) $t - $now) > PSRPT_FRIENDS_TIME_WINDOW) {
        // The device's clock is off; it may set its own by this
        psrpt_friends_fail('stale_time', 200, ['now' => $now]);
    }
    // Every field but v, op, t and sig, sorted by name, the values as sent
    $fields = $GLOBALS['psrpt_friends_fields'];
    unset($fields['v'], $fields['op'], $fields['t'], $fields['sig']);
    ksort($fields, SORT_STRING);
    $message = "psrpt friends 1\n" . $op . "\n" . $t . "\n";
    foreach ($fields as $name => $value) {
        $message .= $name . '=' . $value . "\n";
    }
    if (!psrpt_friends_verify($point, $message, $signature)) {
        psrpt_friends_fail('bad_signature');
    }
    // Remembered in a folder for the minute of its time: the sweep removes the minutes whose
    // requests would be refused for their time anyway
    $folder = psrpt_friends_path('seen', (string) intdiv((int) $t, 60));
    psrpt_friends_mkdir($folder);
    $path = $folder . DIRECTORY_SEPARATOR . hash('sha256', $message);
    $handle = @fopen($path, 'x');
    if ($handle === false) {
        if (file_exists($path)) {
            psrpt_friends_fail('replay');
        }
        psrpt_friends_fail('cannot_write', 503);
    }
    fclose($handle);
}

// ------------------------------------------------------------------ identities

function psrpt_friends_identity_folder(string $fp): string
{
    return psrpt_friends_path('ids', bin2hex($fp));
}

/**
 * What the server knows of an identity: seq, iks (hex), devices (hex), used (when one of its
 * devices was last here), box (its request box, hex or empty), tokens (hashes, hex). Null when
 * it has no card of it, or one nobody has used for a year.
 */
function psrpt_friends_identity(string $fp): ?array
{
    $meta = psrpt_friends_read_json(psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'meta.json');
    if ($meta === null || !is_int($meta['seq'] ?? null) || !is_string($meta['iks'] ?? null) || !is_array($meta['devices'] ?? null)
        || !is_int($meta['used'] ?? null) || $meta['used'] < psrpt_friends_now() - psrpt_friends_int('PSRPT_FRIENDS_CARD_DAYS') * 86400) {
        return null;
    }
    $meta['box'] = is_string($meta['box'] ?? null) ? $meta['box'] : '';
    $meta['tokens'] = is_array($meta['tokens'] ?? null) ? $meta['tokens'] : [];
    return $meta;
}

function psrpt_friends_put_identity(string $fp, array $meta): void
{
    psrpt_friends_put_json(psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'meta.json', $meta);
}

/**
 * The start of every signed operation of an identity: the fingerprint in "fp", not banned,
 * known, the request signed with its IKs. The identity stays locked until the answer.
 */
function psrpt_friends_signed_identity(string $op): array
{
    $fp = psrpt_friends_bytes('fp', 16, 16);
    if (psrpt_friends_banned($fp)) {
        psrpt_friends_fail('banned');
    }
    $folder = psrpt_friends_identity_folder($fp);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $meta = psrpt_friends_identity($fp);
    if ($meta === null) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_check_signed($op, (string) hex2bin($meta['iks']));
    // In use: the card stays another year (written at most once a day)
    if ($meta['used'] < psrpt_friends_now() - 86400) {
        $meta['used'] = psrpt_friends_now();
        psrpt_friends_put_identity($fp, $meta);
    }
    return [$fp, $meta];
}

/** The device named in "device", which has to be one of the card's. */
function psrpt_friends_card_device(array $meta, string $field = 'device'): string
{
    $device = psrpt_friends_bytes($field, 8, 8);
    if (!in_array(bin2hex($device), $meta['devices'], true)) {
        psrpt_friends_fail('unknown_device');
    }
    return $device;
}

/** A device's one-time prekeys: id => public key, oldest first. */
function psrpt_friends_prekeys(string $fp, string $device): array
{
    $raw = @file_get_contents(psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'opk-' . bin2hex($device));
    $keys = [];
    if (is_string($raw)) {
        for ($i = 0; $i + 69 <= strlen($raw); $i += 69) {
            $keys[unpack('N', substr($raw, $i, 4))[1]] = substr($raw, $i + 4, 65);
        }
    }
    return $keys;
}

function psrpt_friends_put_prekeys(string $fp, string $device, array $keys): void
{
    $path = psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'opk-' . bin2hex($device);
    if (!$keys) {
        @unlink($path);
        return;
    }
    $raw = '';
    foreach ($keys as $id => $pub) {
        $raw .= pack('N', $id) . $pub;
    }
    psrpt_friends_put($path, $raw);
}

/** Everything of a device that is no longer on its identity's card. */
function psrpt_friends_forget_device(string $fp, string $device_hex): void
{
    $folder = psrpt_friends_identity_folder($fp);
    @unlink($folder . DIRECTORY_SEPARATOR . 'opk-' . $device_hex);
    psrpt_friends_remove($folder . DIRECTORY_SEPARATOR . 'inbox-' . $device_hex);
    psrpt_friends_forget_push(bin2hex($fp), $device_hex);
}

// ------------------------------------------------------------------ notes in boxes

/**
 * The notes of an inbox or a request box, oldest first: id => path. A note's id is the time it
 * came (milliseconds, 13 digits) and 8 random hex digits. Notes older than $days are removed.
 */
function psrpt_friends_notes(string $folder, int $days): array
{
    $oldest = psrpt_friends_now_ms() - $days * 86400000;
    $notes = [];
    foreach (psrpt_friends_names($folder, '/^[0-9]{13}[0-9a-f]{8}\.note$/') as $name) {
        if ((int) substr($name, 0, 13) < $oldest) {
            @unlink($folder . DIRECTORY_SEPARATOR . $name);
            continue;
        }
        $notes[substr($name, 0, 21)] = $folder . DIRECTORY_SEPARATOR . $name;
    }
    return $notes;
}

/** Adds a note after the ones there are, so that ids keep the order in which notes came. */
function psrpt_friends_add_note(string $folder, array $notes, string $note): string
{
    $ms = psrpt_friends_now_ms();
    if ($notes) {
        $ms = max($ms, (int) substr((string) array_key_last($notes), 0, 13) + 1);
    }
    $id = sprintf('%013d', $ms) . bin2hex(random_bytes(4));
    psrpt_friends_put($folder . DIRECTORY_SEPARATOR . $id . '.note', $note);
    return $id;
}

/** Notes for an answer: [{id, note}], as many as fit, and whether there are more. */
function psrpt_friends_note_list(array $notes): array
{
    $list = [];
    $bytes = 0;
    $more = false;
    foreach ($notes as $id => $path) {
        if (count($list) >= PSRPT_FRIENDS_MAX_LIST || $bytes >= PSRPT_FRIENDS_MAX_ANSWER) {
            $more = true;
            break;
        }
        $data = @file_get_contents($path);
        if (!is_string($data)) {
            continue;
        }
        $bytes += strlen($data);
        $list[] = ['id' => (string) $id, 'note' => psrpt_friends_b64($data)];
    }
    return ['notes' => $list, 'more' => $more];
}

function psrpt_friends_request_folder(string $box): string
{
    return psrpt_friends_path('requests', bin2hex($box));
}

/** The identity a request box belongs to, if it still does. */
function psrpt_friends_request_owner(string $box): ?string
{
    $owner = psrpt_friends_read_json(psrpt_friends_request_folder($box) . DIRECTORY_SEPARATOR . 'owner.json');
    $fp = is_string($owner['fp'] ?? null) && preg_match('/^[0-9a-f]{32}$/', $owner['fp']) ? (string) hex2bin($owner['fp']) : null;
    if ($fp === null) {
        return null;
    }
    $meta = psrpt_friends_identity($fp);
    return $meta !== null && $meta['box'] === bin2hex($box) ? $fp : null;
}

function psrpt_friends_group_folder(string $box): string
{
    return psrpt_friends_path('groups', bin2hex($box));
}

/** A group box: owner (the owner key, hex), hash (of the write token, hex), next (id), used. Null if there is none. */
function psrpt_friends_group(string $box): ?array
{
    $meta = psrpt_friends_read_json(psrpt_friends_group_folder($box) . DIRECTORY_SEPARATOR . 'meta.json');
    if ($meta === null || !is_string($meta['owner'] ?? null) || !is_string($meta['hash'] ?? null) || !is_int($meta['next'] ?? null)
        || !is_int($meta['used'] ?? null) || $meta['used'] < psrpt_friends_now() - psrpt_friends_int('PSRPT_FRIENDS_GROUP_DAYS') * 86400) {
        return null;
    }
    return $meta;
}

/** A group box's messages, oldest first: id => path; those older than the group days are removed. */
function psrpt_friends_group_notes(string $folder): array
{
    $oldest = psrpt_friends_now() - psrpt_friends_int('PSRPT_FRIENDS_GROUP_DAYS') * 86400;
    $notes = [];
    foreach (psrpt_friends_names($folder, '/^[0-9]{12}-[0-9]{10,12}\.note$/') as $name) {
        if ((int) substr($name, 13) < $oldest) {
            @unlink($folder . DIRECTORY_SEPARATOR . $name);
            continue;
        }
        $notes[(int) substr($name, 0, 12)] = $folder . DIRECTORY_SEPARATOR . $name;
    }
    return $notes;
}

/** Whether $token opens a box whose token hash is $hash (hex). */
function psrpt_friends_token_opens(string $token, string $hash): bool
{
    return hash_equals($hash, hash('sha256', $token));
}

// ------------------------------------------------------------------ push messages

/** Whether push messages may go to this address (PSRPT_FRIENDS_PUSH_HOSTS). */
function psrpt_friends_endpoint_allowed(string $endpoint): bool
{
    $testing = psrpt_friends_testing();
    if (strlen($endpoint) > 1024 || !preg_match('/^https?:\/\/[\x21-\x7E]+$/', $endpoint)) {
        return false;
    }
    $parts = parse_url($endpoint);
    if (!is_array($parts) || !isset($parts['scheme'], $parts['host']) || isset($parts['user']) || isset($parts['pass'])) {
        return false;
    }
    $host = strtolower($parts['host']);
    $literal = filter_var(trim($host, '[]'), FILTER_VALIDATE_IP) !== false;
    if (!$testing && ($parts['scheme'] !== 'https' || (isset($parts['port']) && $parts['port'] !== 443) || $literal)) {
        return false;
    }
    $allowed = false;
    foreach (explode(',', (string) psrpt_friends_setting('PSRPT_FRIENDS_PUSH_HOSTS')) as $pattern) {
        $pattern = strtolower(trim($pattern));
        if ($pattern === '*' || $pattern === $host
            || (strncmp($pattern, '*.', 2) === 0 && substr($host, -strlen($pattern) + 1) === substr($pattern, 1))) {
            $allowed = $allowed || $pattern !== '*' || $testing || psrpt_friends_public_host($host);
        }
    }
    return $allowed;
}

/** For PSRPT_FRIENDS_PUSH_HOSTS '*': a name whose addresses are all on the public internet. */
function psrpt_friends_public_host(string $host): bool
{
    $addresses = @gethostbynamel($host);
    if (!$addresses) {
        return false;
    }
    foreach ($addresses as $address) {
        if (filter_var($address, FILTER_VALIDATE_IP, FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE) === false) {
            return false;
        }
    }
    return true;
}

function psrpt_friends_push_path(string $fp_hex, string $device_hex): string
{
    return psrpt_friends_path('ids', $fp_hex, 'push-' . $device_hex . '.json');
}

/** Removes a device's push subscription, and its place in group boxes. */
function psrpt_friends_forget_push(string $fp_hex, string $device_hex): void
{
    $path = psrpt_friends_push_path($fp_hex, $device_hex);
    $subscription = psrpt_friends_read_json($path);
    foreach (is_array($subscription['groups'] ?? null) ? $subscription['groups'] : [] as $box_hex) {
        if (is_string($box_hex) && preg_match('/^[0-9a-f]{32}$/', $box_hex)) {
            @unlink(psrpt_friends_path('groups', $box_hex, 'push', $fp_hex . '-' . $device_hex));
        }
    }
    @unlink($path);
}

/** The devices of an identity that have a push subscription, as "fp:device" (hex). */
function psrpt_friends_push_devices(string $fp): array
{
    $targets = [];
    foreach (psrpt_friends_names(psrpt_friends_identity_folder($fp), '/^push-[0-9a-f]{16}\.json$/') as $name) {
        $targets[] = bin2hex($fp) . ':' . substr($name, 5, 16);
    }
    return $targets;
}

function psrpt_friends_queue_push(array $targets): void
{
    foreach ($targets as $target) {
        $GLOBALS['psrpt_friends_pushes'][$target] = true;
    }
}

/** Sends an empty push message to each device ("fp:device", hex) that has a subscription; after the answer. */
function psrpt_friends_push_all(array $targets): void
{
    $keys = psrpt_friends_keys();
    $private = openssl_pkey_get_private($keys['vapid_private']);
    if ($private === false) {
        return;
    }
    $sent = 0;
    foreach ($targets as $target) {
        if ($sent >= PSRPT_FRIENDS_MAX_PUSHES || !preg_match('/^([0-9a-f]{32}):([0-9a-f]{16})$/', $target, $m)) {
            continue;
        }
        $subscription = psrpt_friends_read_json(psrpt_friends_push_path($m[1], $m[2]));
        $endpoint = $subscription['endpoint'] ?? null;
        if (!is_string($endpoint) || !psrpt_friends_endpoint_allowed($endpoint)) {
            continue;
        }
        $sent++;
        $status = psrpt_friends_push_one($endpoint, $private, $keys['vapid_public']);
        if ($status === 404 || $status === 410) {
            // The browser has dropped the subscription
            $lock = psrpt_friends_lock(psrpt_friends_path('ids', $m[1]));
            $again = psrpt_friends_read_json(psrpt_friends_push_path($m[1], $m[2]));
            if (($again['endpoint'] ?? null) === $endpoint) {
                psrpt_friends_forget_push($m[1], $m[2]);
            }
            psrpt_friends_unlock($lock);
        }
    }
}

/**
 * One empty Web Push message (RFC 8030, VAPID as in RFC 8292): no body, so nothing to seal, and
 * the push service learns only that something is new. The answer's status, 0 if none came.
 */
function psrpt_friends_push_one(string $endpoint, $private, string $public): int
{
    $parts = parse_url($endpoint);
    $default_port = $parts['scheme'] === 'https' ? 443 : 80;
    $audience = $parts['scheme'] . '://' . $parts['host'] . (isset($parts['port']) && $parts['port'] !== $default_port ? ':' . $parts['port'] : '');
    $contact = (string) psrpt_friends_setting('PSRPT_FRIENDS_PUSH_CONTACT');
    $claims = ['aud' => $audience, 'exp' => psrpt_friends_now() + 12 * 3600];
    if (preg_match('/^(https:\/\/|mailto:)[\x21-\x7E]+$/', $contact)) {
        $claims['sub'] = $contact;
    }
    $input = psrpt_friends_b64((string) json_encode(['typ' => 'JWT', 'alg' => 'ES256']))
        . '.' . psrpt_friends_b64((string) json_encode($claims, JSON_UNESCAPED_SLASHES));
    if (!openssl_sign($input, $der, $private, OPENSSL_ALGO_SHA256) || ($raw = psrpt_friends_raw_signature($der)) === null) {
        return 0;
    }
    $headers = [
        'TTL: 86400',
        'Content-Length: 0',
        'Authorization: vapid t=' . $input . '.' . psrpt_friends_b64($raw) . ', k=' . $public,
    ];
    $use_curl = function_exists('curl_init') && !(psrpt_friends_testing() && getenv('PSRPT_FRIENDS_TEST_NO_CURL') === '1');
    if ($use_curl) {
        $curl = curl_init($endpoint);
        curl_setopt_array($curl, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => '',
            CURLOPT_HTTPHEADER => $headers,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_FOLLOWLOCATION => false,
            CURLOPT_CONNECTTIMEOUT => 3,
            CURLOPT_TIMEOUT => 5,
        ]);
        curl_exec($curl);
        $status = (int) curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        // Closed as it goes (PHP 8 closes the handle with its last reference; curl_close is deprecated)
        unset($curl);
        return $status;
    }
    $context = stream_context_create(['http' => [
        'method' => 'POST',
        'header' => implode("\r\n", $headers),
        'content' => '',
        'timeout' => 5,
        'ignore_errors' => true,
        'follow_location' => 0,
    ]]);
    $stream = @fopen($endpoint, 'r', false, $context);
    if ($stream === false) {
        return 0;
    }
    $meta = stream_get_meta_data($stream);
    fclose($stream);
    $status = 0;
    foreach ($meta['wrapper_data'] ?? [] as $line) {
        if (is_string($line) && preg_match('/^HTTP\/\S+\s+([0-9]{3})/', $line, $m)) {
            $status = (int) $m[1];
        }
    }
    return $status;
}

// ------------------------------------------------------------------ the operations

function psrpt_friends_card_put(): void
{
    $card = psrpt_friends_bytes('card', 1, psrpt_friends_int('PSRPT_FRIENDS_MAX_CARD'));
    $fp = psrpt_friends_bytes('fp', 16, 16);
    $parsed = psrpt_friends_parse_card($card);
    if ($parsed === null || $parsed['fp'] !== $fp) {
        psrpt_friends_fail('bad_card');
    }
    if (psrpt_friends_banned($fp)) {
        psrpt_friends_fail('banned');
    }
    $folder = psrpt_friends_identity_folder($fp);
    psrpt_friends_lock($folder);
    psrpt_friends_check_signed('card_put', $parsed['iks']);
    $meta = psrpt_friends_identity($fp);
    if ($meta !== null && $parsed['seq'] <= $meta['seq']) {
        psrpt_friends_fail('old_card', 200, ['seq' => $meta['seq']]);
    }
    if ($meta === null) {
        // A new identity, or one that had expired: nothing of the old one stays
        psrpt_friends_take('identity', 'PSRPT_FRIENDS_IDENTITIES_PER_HOUR');
        psrpt_friends_room_left();
        foreach (@scandir($folder) ?: [] as $name) {
            if ($name !== '.' && $name !== '..' && $name !== '.lock') {
                if (strncmp($name, 'push-', 5) === 0 && preg_match('/^push-([0-9a-f]{16})\.json$/', $name, $m)) {
                    psrpt_friends_forget_push(bin2hex($fp), $m[1]);
                }
                psrpt_friends_remove($folder . DIRECTORY_SEPARATOR . $name);
            }
        }
        $meta = ['box' => '', 'tokens' => [], 'devices' => []];
    }
    psrpt_friends_put($folder . DIRECTORY_SEPARATOR . 'card', $card);
    foreach (array_diff($meta['devices'], $parsed['devices']) as $gone) {
        psrpt_friends_forget_device($fp, (string) $gone);
    }
    psrpt_friends_put_identity($fp, [
        'seq' => $parsed['seq'],
        'iks' => bin2hex($parsed['iks']),
        'devices' => $parsed['devices'],
        'used' => psrpt_friends_now(),
        'box' => $meta['box'],
        'tokens' => $meta['tokens'],
    ]);
    psrpt_friends_ok(['seq' => $parsed['seq']]);
}

/** The card of an identity that is known and not banned; null otherwise. */
function psrpt_friends_card_of(string $fp): ?string
{
    if (psrpt_friends_banned($fp) || psrpt_friends_identity($fp) === null) {
        return null;
    }
    $card = @file_get_contents(psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'card');
    return is_string($card) && $card !== '' ? $card : null;
}

function psrpt_friends_card_get(): void
{
    $card = psrpt_friends_card_of(psrpt_friends_bytes('fp', 16, 16));
    if ($card === null) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_ok(['card' => psrpt_friends_b64($card)]);
}

function psrpt_friends_bundle_get(): void
{
    $fp = psrpt_friends_bytes('fp', 16, 16);
    $wanted = psrpt_friends_field('devices');
    if ($wanted !== null && !preg_match('/^([A-Za-z0-9_-]{11}(,[A-Za-z0-9_-]{11}){0,9})?$/', $wanted)) {
        psrpt_friends_malformed();
    }
    if (psrpt_friends_card_of($fp) === null) {
        psrpt_friends_fail('unknown');
    }
    // Each bundle takes prekeys away: a few hundred an hour are plenty for an honest app
    psrpt_friends_take('bundle', 'PSRPT_FRIENDS_BUNDLES_PER_HOUR');
    psrpt_friends_lock(psrpt_friends_identity_folder($fp));
    $meta = psrpt_friends_identity($fp);
    $card = psrpt_friends_card_of($fp);
    if ($meta === null || $card === null) {
        psrpt_friends_fail('unknown');
    }
    $devices = $meta['devices'];
    if ($wanted !== null && $wanted !== '') {
        $asked = [];
        foreach (explode(',', $wanted) as $text) {
            $device = psrpt_friends_unb64($text);
            if ($device === null || strlen($device) !== 8) {
                psrpt_friends_malformed();
            }
            $asked[] = bin2hex($device);
        }
        $devices = array_values(array_intersect($devices, $asked));
    }
    $prekeys = [];
    foreach ($devices as $device_hex) {
        $device = (string) hex2bin($device_hex);
        $keys = psrpt_friends_prekeys($fp, $device);
        if (!$keys) {
            continue;
        }
        $id = (int) array_key_first($keys);
        $prekeys[psrpt_friends_b64($device)] = ['id' => $id, 'pub' => psrpt_friends_b64($keys[$id])];
        unset($keys[$id]);
        psrpt_friends_put_prekeys($fp, $device, $keys);
    }
    psrpt_friends_ok(['card' => psrpt_friends_b64($card), 'prekeys' => (object) $prekeys]);
}

function psrpt_friends_prekeys_put(): void
{
    [$fp, $meta] = psrpt_friends_signed_identity('prekeys_put');
    $device = psrpt_friends_card_device($meta);
    $most = psrpt_friends_int('PSRPT_FRIENDS_MAX_PREKEYS');
    $raw = psrpt_friends_bytes('keys', 69, 69 * $most);
    if (strlen($raw) % 69 !== 0) {
        psrpt_friends_malformed();
    }
    $keys = psrpt_friends_prekeys($fp, $device);
    for ($i = 0; $i < strlen($raw); $i += 69) {
        $id = unpack('N', substr($raw, $i, 4))[1];
        $pub = substr($raw, $i + 4, 65);
        // 0 stands for "no one-time prekey" in a first message
        if ($id === 0 || !psrpt_friends_valid_point($pub)) {
            psrpt_friends_fail('bad_key');
        }
        unset($keys[$id]);
        $keys[$id] = $pub;
    }
    psrpt_friends_room_left();
    // The newest stay
    $keys = array_slice($keys, -$most, null, true);
    psrpt_friends_put_prekeys($fp, $device, $keys);
    psrpt_friends_ok(['prekeys' => count($keys)]);
}

function psrpt_friends_requests_set(): void
{
    [$fp, $meta] = psrpt_friends_signed_identity('requests_set');
    $box = psrpt_friends_bytes('box', 0, 16);
    if ($box !== '' && strlen($box) !== 16) {
        psrpt_friends_malformed();
    }
    if ($box !== '' && bin2hex($box) !== $meta['box']) {
        $folder = psrpt_friends_request_folder($box);
        psrpt_friends_lock($folder);
        $owner = psrpt_friends_request_owner($box);
        if ($owner !== null && $owner !== $fp) {
            psrpt_friends_fail('box_taken');
        }
        // What an earlier owner left there is not for this one
        foreach (psrpt_friends_names($folder, '/\.note$/') as $name) {
            @unlink($folder . DIRECTORY_SEPARATOR . $name);
        }
        psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'owner.json', ['fp' => bin2hex($fp)]);
    }
    if ($meta['box'] !== '' && $meta['box'] !== bin2hex($box)) {
        // The old link stops working, and the requests in its box go with it
        $old = psrpt_friends_request_folder((string) hex2bin($meta['box']));
        $lock = psrpt_friends_lock($old);
        psrpt_friends_remove($old);
        psrpt_friends_unlock($lock);
    }
    $meta['box'] = bin2hex($box);
    psrpt_friends_put_identity($fp, $meta);
    psrpt_friends_ok();
}

function psrpt_friends_request_put(): void
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $note = psrpt_friends_bytes('note', 1, psrpt_friends_int('PSRPT_FRIENDS_MAX_REQUEST_NOTE'));
    psrpt_friends_refuse_banned_sender($note, 2);
    $folder = psrpt_friends_request_folder($box);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $owner = psrpt_friends_request_owner($box);
    if ($owner === null || psrpt_friends_banned($owner)) {
        psrpt_friends_fail('unknown');
    }
    $notes = psrpt_friends_notes($folder, psrpt_friends_int('PSRPT_FRIENDS_REQUEST_DAYS'));
    if (count($notes) >= psrpt_friends_int('PSRPT_FRIENDS_MAX_REQUESTS')) {
        psrpt_friends_fail('box_full');
    }
    psrpt_friends_take('request', 'PSRPT_FRIENDS_REQUESTS_PER_HOUR');
    psrpt_friends_room_left();
    psrpt_friends_add_note($folder, $notes, $note);
    psrpt_friends_queue_push(psrpt_friends_push_devices($owner));
    psrpt_friends_ok();
}

function psrpt_friends_token_change(string $op): void
{
    [$fp, $meta] = psrpt_friends_signed_identity($op);
    $hash = bin2hex(psrpt_friends_bytes('hash', 32, 32));
    $tokens = array_values(array_filter($meta['tokens'], function ($known) use ($hash): bool {
        return $known !== $hash;
    }));
    if ($op === 'token_add') {
        if (count($tokens) >= psrpt_friends_int('PSRPT_FRIENDS_MAX_TOKENS')) {
            psrpt_friends_fail('too_many');
        }
        $tokens[] = $hash;
    }
    $meta['tokens'] = $tokens;
    psrpt_friends_put_identity($fp, $meta);
    psrpt_friends_ok(['tokens' => count($tokens)]);
}

function psrpt_friends_inbox_put(): void
{
    $fp = psrpt_friends_bytes('fp', 16, 16);
    $token = psrpt_friends_bytes('token', 1, 64);
    $note = psrpt_friends_bytes('note', 1, psrpt_friends_int('PSRPT_FRIENDS_MAX_NOTE'));
    if (psrpt_friends_banned($fp)) {
        psrpt_friends_fail('banned');
    }
    psrpt_friends_refuse_banned_sender($note, 2);
    $folder = psrpt_friends_identity_folder($fp);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $meta = psrpt_friends_identity($fp);
    if ($meta === null) {
        psrpt_friends_fail('unknown');
    }
    $device = psrpt_friends_card_device($meta);
    $hash = hash('sha256', $token);
    $opens = false;
    foreach ($meta['tokens'] as $known) {
        $opens = (is_string($known) && hash_equals($known, $hash)) || $opens;
    }
    if (!$opens) {
        psrpt_friends_fail('bad_token');
    }
    $inbox = $folder . DIRECTORY_SEPARATOR . 'inbox-' . bin2hex($device);
    $notes = psrpt_friends_notes($inbox, psrpt_friends_int('PSRPT_FRIENDS_INBOX_DAYS'));
    if (count($notes) >= psrpt_friends_int('PSRPT_FRIENDS_MAX_INBOX')) {
        psrpt_friends_fail('box_full');
    }
    psrpt_friends_room_left();
    psrpt_friends_mkdir($inbox);
    psrpt_friends_add_note($inbox, $notes, $note);
    psrpt_friends_queue_push([bin2hex($fp) . ':' . bin2hex($device)]);
    psrpt_friends_ok();
}

/** The folder inbox_get and inbox_ack work on: the device's inbox, or with "box" the identity's request box. */
function psrpt_friends_own_box(string $fp, array $meta): array
{
    if (psrpt_friends_field('box') !== null && psrpt_friends_field('box') !== '') {
        $box = psrpt_friends_bytes('box', 16, 16);
        if (bin2hex($box) !== $meta['box']) {
            psrpt_friends_fail('unknown');
        }
        $folder = psrpt_friends_request_folder($box);
        psrpt_friends_lock($folder);
        return [$folder, psrpt_friends_int('PSRPT_FRIENDS_REQUEST_DAYS'), null];
    }
    $device = psrpt_friends_card_device($meta);
    return [psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'inbox-' . bin2hex($device),
        psrpt_friends_int('PSRPT_FRIENDS_INBOX_DAYS'), $device];
}

function psrpt_friends_inbox_get(): void
{
    [$fp, $meta] = psrpt_friends_signed_identity('inbox_get');
    [$folder, $days, $device] = psrpt_friends_own_box($fp, $meta);
    $answer = psrpt_friends_note_list(psrpt_friends_notes($folder, $days));
    if ($device !== null) {
        // So that the device knows when to make more
        $answer['prekeys'] = count(psrpt_friends_prekeys($fp, $device));
    }
    psrpt_friends_ok($answer);
}

function psrpt_friends_inbox_ack(): void
{
    [$fp, $meta] = psrpt_friends_signed_identity('inbox_ack');
    $ids = explode(',', psrpt_friends_field('ids') ?? '');
    if (count($ids) > PSRPT_FRIENDS_MAX_INBOX_ACK) {
        psrpt_friends_malformed();
    }
    foreach ($ids as $id) {
        if (!preg_match('/^[0-9]{13}[0-9a-f]{8}$/', $id)) {
            psrpt_friends_malformed();
        }
    }
    [$folder] = psrpt_friends_own_box($fp, $meta);
    $removed = 0;
    foreach (array_unique($ids) as $id) {
        $removed += @unlink($folder . DIRECTORY_SEPARATOR . basename($id) . '.note') ? 1 : 0;
    }
    psrpt_friends_ok(['removed' => $removed]);
}

function psrpt_friends_status_put(): void
{
    [$fp] = psrpt_friends_signed_identity('status_put');
    $note = psrpt_friends_bytes('note', 0, psrpt_friends_int('PSRPT_FRIENDS_MAX_STATUS'));
    $path = psrpt_friends_identity_folder($fp) . DIRECTORY_SEPARATOR . 'status.json';
    // A status is kept under a name only friends can work out (from the friends key and the day):
    // whoever knows a fingerprint learns nothing of when its player is online
    $before = psrpt_friends_read_json($path);
    $old_box = is_string($before['box'] ?? null) && preg_match('/^[0-9a-f]{32}$/', $before['box']) ? $before['box'] : '';
    if ($note === '') {
        // Invisible, or going away: no status at all
        @unlink($path);
        if ($old_box !== '') {
            @unlink(psrpt_friends_path('statuses', $old_box . '.json'));
        }
        psrpt_friends_ok();
    }
    $box = bin2hex(psrpt_friends_bytes('box', 16, 16));
    if ($old_box !== '' && $old_box !== $box) {
        @unlink(psrpt_friends_path('statuses', $old_box . '.json'));
    }
    psrpt_friends_mkdir(psrpt_friends_path('statuses'));
    psrpt_friends_put_json(psrpt_friends_path('statuses', $box . '.json'),
        ['t' => psrpt_friends_now(), 'note' => psrpt_friends_b64($note), 'fp' => bin2hex($fp)]);
    psrpt_friends_put_json($path, ['t' => psrpt_friends_now(), 'box' => $box]);
    psrpt_friends_ok();
}

function psrpt_friends_status_get(): void
{
    $list = psrpt_friends_field('boxes');
    if ($list === null || !preg_match('/^[A-Za-z0-9_-]{22}(,[A-Za-z0-9_-]{22})*$/', $list)) {
        psrpt_friends_malformed();
    }
    $boxes = array_unique(explode(',', $list));
    if (count($boxes) > PSRPT_FRIENDS_MAX_STATUS_BOXES) {
        psrpt_friends_malformed();
    }
    $fresh = psrpt_friends_now() - psrpt_friends_int('PSRPT_FRIENDS_STATUS_SECONDS');
    $notes = [];
    foreach ($boxes as $text) {
        $box = psrpt_friends_unb64($text);
        if ($box === null || strlen($box) !== 16) {
            psrpt_friends_malformed();
        }
        $status = psrpt_friends_read_json(psrpt_friends_path('statuses', bin2hex($box) . '.json'));
        if (!is_int($status['t'] ?? null) || $status['t'] < $fresh || !is_string($status['note'] ?? null)
            || !is_string($status['fp'] ?? null) || !preg_match('/^[0-9a-f]{32}$/', $status['fp'])) {
            continue;
        }
        if (psrpt_friends_banned((string) hex2bin($status['fp']))) {
            continue;
        }
        $notes[$text] = $status['note'];
    }
    psrpt_friends_ok(['notes' => (object) $notes]);
}

function psrpt_friends_group_new(): void
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $owner = psrpt_friends_bytes('owner', 65, 65);
    $hash = psrpt_friends_bytes('hash', 32, 32);
    if (!psrpt_friends_valid_point($owner)) {
        psrpt_friends_fail('bad_key');
    }
    $folder = psrpt_friends_group_folder($box);
    psrpt_friends_lock($folder);
    if (psrpt_friends_group($box) !== null) {
        psrpt_friends_fail('box_taken');
    }
    psrpt_friends_take('group', 'PSRPT_FRIENDS_GROUPS_PER_HOUR');
    psrpt_friends_room_left();
    // A box that had expired starts empty
    foreach (@scandir($folder) ?: [] as $name) {
        if ($name !== '.' && $name !== '..' && $name !== '.lock') {
            psrpt_friends_remove($folder . DIRECTORY_SEPARATOR . $name);
        }
    }
    psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json',
        ['owner' => bin2hex($owner), 'hash' => bin2hex($hash), 'next' => 1, 'used' => psrpt_friends_now()]);
    psrpt_friends_ok();
}

/**
 * The group box named in "box", with a token that opens it: the current one for writing; for
 * reading (with $read) also one of the tokens before it, for 30 days. A member who had not
 * looked yet when an admin made a new token (removing somebody) still reads the box, and with
 * it the new token, sealed for them. Whoever was removed reads only sealed notes, with no keys
 * for the new ones.
 */
function psrpt_friends_open_group(bool $read = false): array
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $token = psrpt_friends_bytes('token', 1, 64);
    $folder = psrpt_friends_group_folder($box);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $meta = psrpt_friends_group($box);
    if ($meta === null) {
        psrpt_friends_fail('unknown');
    }
    if (!($read ? psrpt_friends_group_reads($meta, $token) : psrpt_friends_token_opens($token, $meta['hash']))) {
        psrpt_friends_fail('bad_token');
    }
    return [$box, $folder, $meta];
}

/** Whether a token reads a group box: the current one, or one of those before it for 30 days. */
function psrpt_friends_group_reads(array $meta, string $token): bool
{
    if (psrpt_friends_token_opens($token, $meta['hash'])) {
        return true;
    }
    foreach (is_array($meta['old'] ?? null) ? $meta['old'] : [] as $old) {
        if (is_array($old) && is_string($old['hash'] ?? null) && (int) ($old['until'] ?? 0) > psrpt_friends_now()
            && psrpt_friends_token_opens($token, $old['hash'])) {
            return true;
        }
    }
    return false;
}

function psrpt_friends_group_token(): void
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $hash = psrpt_friends_bytes('hash', 32, 32);
    $folder = psrpt_friends_group_folder($box);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $meta = psrpt_friends_group($box);
    if ($meta === null) {
        psrpt_friends_fail('unknown');
    }
    // Only the owner key, which the group's admins have, changes who may write
    psrpt_friends_check_signed('group_token', (string) hex2bin($meta['owner']));
    psrpt_friends_take('group ' . bin2hex($box), 'PSRPT_FRIENDS_GROUP_CHANGES_PER_HOUR');
    // The token before still reads for a while (psrpt_friends_open_group)
    $old = is_array($meta['old'] ?? null) ? $meta['old'] : [];
    $old[] = ['hash' => $meta['hash'], 'until' => psrpt_friends_now() + 30 * 86400];
    $meta['old'] = array_slice(array_values(array_filter($old, static function ($o): bool {
        return is_array($o) && (int) ($o['until'] ?? 0) > psrpt_friends_now();
    })), -8);
    $meta['hash'] = bin2hex($hash);
    $meta['used'] = psrpt_friends_now();
    psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json', $meta);
    psrpt_friends_ok();
}

/**
 * A new owner key for a group box, signed with the one before: when an admin is removed or
 * stops being one, the remaining admins make a new one that the former admin does not have.
 */
function psrpt_friends_group_owner(): void
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $owner = psrpt_friends_bytes('owner', 65, 65);
    if (!psrpt_friends_valid_point($owner)) {
        psrpt_friends_fail('bad_key');
    }
    $folder = psrpt_friends_group_folder($box);
    if (!is_dir($folder)) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_lock($folder);
    $meta = psrpt_friends_group($box);
    if ($meta === null) {
        psrpt_friends_fail('unknown');
    }
    psrpt_friends_check_signed('group_owner', (string) hex2bin($meta['owner']));
    psrpt_friends_take('group ' . bin2hex($box), 'PSRPT_FRIENDS_GROUP_CHANGES_PER_HOUR');
    $meta['owner'] = bin2hex($owner);
    $meta['used'] = psrpt_friends_now();
    psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json', $meta);
    psrpt_friends_ok();
}

function psrpt_friends_group_put(): void
{
    $note = psrpt_friends_bytes('note', 1, psrpt_friends_int('PSRPT_FRIENDS_MAX_NOTE'));
    // Two kinds of notes: group messages (the group id from byte 1, whose first byte always has
    // its top bit set) and envelopes between the members (kind 1 or 2 in byte 1: sender keys and
    // the group's changes, for members who are no friends of each other)
    $envelope = strlen($note) >= 2 && ($note[1] === "\x01" || $note[1] === "\x02");
    $writer_at = $envelope ? 2 : 17;
    psrpt_friends_refuse_banned_sender($note, $writer_at);
    [, $folder, $meta] = psrpt_friends_open_group();
    psrpt_friends_room_left();
    $id = $meta['next'];
    psrpt_friends_put($folder . DIRECTORY_SEPARATOR . sprintf('%012d-%010d.note', $id, psrpt_friends_now()), $note);
    $notes = psrpt_friends_group_notes($folder);
    foreach (array_slice($notes, 0, max(0, count($notes) - psrpt_friends_int('PSRPT_FRIENDS_MAX_GROUP'))) as $path) {
        @unlink($path);
    }
    $meta['next'] = $id + 1;
    $meta['used'] = psrpt_friends_now();
    psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json', $meta);
    // Everybody whose browser asked for it, except the device that wrote
    $writer = strlen($note) >= $writer_at + 24 ? bin2hex(substr($note, $writer_at, 16)) . '-' . bin2hex(substr($note, $writer_at + 16, 8)) : '';
    $targets = [];
    foreach (psrpt_friends_names($folder . DIRECTORY_SEPARATOR . 'push', '/^[0-9a-f]{32}-[0-9a-f]{16}$/') as $name) {
        if ($name !== $writer) {
            $targets[] = str_replace('-', ':', $name);
        }
    }
    psrpt_friends_queue_push($targets);
    psrpt_friends_ok(['id' => $id]);
}

function psrpt_friends_group_get(): void
{
    $after = psrpt_friends_field('after') ?? '0';
    if (!preg_match('/^[0-9]{1,12}$/', $after)) {
        psrpt_friends_malformed();
    }
    [, $folder, $meta] = psrpt_friends_open_group(true);
    $notes = [];
    foreach (psrpt_friends_group_notes($folder) as $id => $path) {
        if ($id > (int) $after) {
            $notes[$id] = $path;
        }
    }
    $answer = psrpt_friends_note_list($notes);
    foreach ($answer['notes'] as $index => $entry) {
        $answer['notes'][$index]['id'] = (int) $entry['id'];
    }
    if ($meta['used'] < psrpt_friends_now() - 3600) {
        $meta['used'] = psrpt_friends_now();
        psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json', $meta);
    }
    psrpt_friends_ok($answer);
}

/**
 * The link box in "box" and the side in "side", locked (and created, for $create); its meta
 * (t: last write, parts: "side-part" => bytes), or null when there is none or it has expired.
 */
function psrpt_friends_open_link(bool $create): array
{
    $box = psrpt_friends_bytes('box', 16, 16);
    $side = psrpt_friends_field('side');
    if ($side !== '1' && $side !== '2') {
        psrpt_friends_malformed();
    }
    $folder = psrpt_friends_path('links', bin2hex($box));
    $meta = null;
    if ($create || is_dir($folder)) {
        psrpt_friends_lock($folder);
        $meta = psrpt_friends_read_json($folder . DIRECTORY_SEPARATOR . 'meta.json');
        if ($meta === null || !is_int($meta['t'] ?? null) || !is_array($meta['parts'] ?? null)
            || $meta['t'] < psrpt_friends_now() - psrpt_friends_int('PSRPT_FRIENDS_LINK_SECONDS')) {
            $meta = null;
        }
    }
    return [$folder, (int) $side, $meta];
}

function psrpt_friends_link_put(): void
{
    $part = psrpt_friends_field('part');
    if ($part === null || !preg_match('/^(0|[1-9][0-9]{0,2})$/', $part)) {
        psrpt_friends_malformed();
    }
    $data = psrpt_friends_bytes('data', 0, PSRPT_FRIENDS_MAX_LINK_PART);
    [$folder, $side, $meta] = psrpt_friends_open_link($data !== '');
    if ($data === '') {
        // Linked: either device removes the box
        if (is_dir($folder)) {
            psrpt_friends_remove($folder);
        }
        psrpt_friends_ok();
    }
    if ($meta === null) {
        // A new box (or one that had expired, which starts empty)
        psrpt_friends_take('link', 'PSRPT_FRIENDS_LINKS_PER_HOUR');
        foreach (psrpt_friends_names($folder, '/\.part$|^meta\.json$/') as $name) {
            @unlink($folder . DIRECTORY_SEPARATOR . $name);
        }
        $meta = ['t' => 0, 'parts' => []];
    }
    $key = $side . '-' . $part;
    $size = array_sum($meta['parts']) - (int) ($meta['parts'][$key] ?? 0) + strlen($data);
    if ($size > psrpt_friends_int('PSRPT_FRIENDS_MAX_LINK')) {
        psrpt_friends_fail('box_full');
    }
    psrpt_friends_room_left();
    psrpt_friends_put($folder . DIRECTORY_SEPARATOR . $key . '.part', $data);
    $meta['parts'][$key] = strlen($data);
    $meta['t'] = psrpt_friends_now();
    psrpt_friends_put_json($folder . DIRECTORY_SEPARATOR . 'meta.json', $meta);
    psrpt_friends_ok();
}

function psrpt_friends_link_get(): void
{
    $part = psrpt_friends_field('part');
    if ($part !== null && $part !== '' && !preg_match('/^(0|[1-9][0-9]{0,2})$/', $part)) {
        psrpt_friends_malformed();
    }
    [$folder, $side, $meta] = psrpt_friends_open_link(false);
    $parts = [];
    foreach ($meta['parts'] ?? [] as $key => $size) {
        if (strncmp((string) $key, $side . '-', 2) === 0) {
            $parts[] = (int) substr((string) $key, 2);
        }
    }
    sort($parts);
    $answer = ['parts' => $parts];
    if ($part !== null && $part !== '') {
        $data = in_array((int) $part, $parts, true) ? @file_get_contents($folder . DIRECTORY_SEPARATOR . $side . '-' . $part . '.part') : false;
        $answer['data'] = is_string($data) ? psrpt_friends_b64($data) : null;
    }
    psrpt_friends_ok($answer);
}

function psrpt_friends_push_set(): void
{
    [$fp, $meta] = psrpt_friends_signed_identity('push_set');
    $device = psrpt_friends_card_device($meta);
    $endpoint = psrpt_friends_field('endpoint') ?? '';
    $fp_hex = bin2hex($fp);
    $device_hex = bin2hex($device);
    if ($endpoint === '') {
        psrpt_friends_forget_push($fp_hex, $device_hex);
        psrpt_friends_ok();
    }
    $p256dh = psrpt_friends_bytes('p256dh', 65, 65);
    $auth = psrpt_friends_bytes('auth', 16, 16);
    if (!psrpt_friends_valid_point($p256dh)) {
        psrpt_friends_fail('bad_key');
    }
    if (!psrpt_friends_endpoint_allowed($endpoint)) {
        psrpt_friends_fail('not_allowed');
    }
    // Group boxes whose messages should wake this browser too (optional), each as "box.token":
    // only whoever may read a box hears that something came
    $list = psrpt_friends_field('groups') ?? '';
    if (!preg_match('/^([A-Za-z0-9_-]{22}\.[A-Za-z0-9_-]{2,86}(,[A-Za-z0-9_-]{22}\.[A-Za-z0-9_-]{2,86}){0,199})?$/', $list)) {
        psrpt_friends_malformed();
    }
    $groups = [];
    foreach ($list === '' ? [] : explode(',', $list) as $text) {
        [$box_text, $token_text] = explode('.', $text, 2);
        $box = psrpt_friends_unb64($box_text);
        $token = psrpt_friends_unb64($token_text);
        if ($box === null || strlen($box) !== 16 || $token === null || $token === '') {
            psrpt_friends_malformed();
        }
        $groups[bin2hex($box)] = [$box, $token];
    }
    psrpt_friends_forget_push($fp_hex, $device_hex);
    $listed = [];
    foreach ($groups as $box_hex => [$box, $token]) {
        $meta = psrpt_friends_group($box);
        if ($meta !== null && psrpt_friends_group_reads($meta, $token)) {
            $folder = psrpt_friends_group_folder($box) . DIRECTORY_SEPARATOR . 'push';
            psrpt_friends_mkdir($folder);
            @touch($folder . DIRECTORY_SEPARATOR . $fp_hex . '-' . $device_hex);
            $listed[] = $box_hex;
        }
    }
    psrpt_friends_put_json(psrpt_friends_push_path($fp_hex, $device_hex), [
        'endpoint' => $endpoint,
        'p256dh' => psrpt_friends_b64($p256dh),
        'auth' => psrpt_friends_b64($auth),
        'groups' => $listed,
    ]);
    psrpt_friends_ok(['groups' => count($listed)]);
}

function psrpt_friends_report(): void
{
    $text = psrpt_friends_field('text');
    if ($text === null || $text === '' || strlen($text) > psrpt_friends_int('PSRPT_FRIENDS_MAX_REPORT') || !preg_match('//u', $text)) {
        psrpt_friends_malformed();
    }
    psrpt_friends_take('report', 'PSRPT_FRIENDS_REPORTS_PER_HOUR');
    psrpt_friends_room_left();
    $folder = psrpt_friends_path('reports');
    psrpt_friends_mkdir($folder);
    $now = psrpt_friends_now();
    psrpt_friends_put($folder . DIRECTORY_SEPARATOR . sprintf('%010d-%s.txt', $now, bin2hex(random_bytes(4))),
        'Report of ' . gmdate('Y-m-d H:i', $now) . " UTC, as the reporter's app sent it:\n\n" . $text . "\n");
    psrpt_friends_ok();
}

// ------------------------------------------------------------------ the sweep

/**
 * Removes what has expired. The cheap part (replay memory, counters, link boxes) runs now and
 * then; everything else at most once an hour, after an answer, or from the command line.
 */
function psrpt_friends_sweep(bool $everything, bool $force = false): void
{
    $now = psrpt_friends_now();
    foreach (psrpt_friends_names(psrpt_friends_path('seen'), '/^[0-9]+$/') as $minute) {
        if (((int) $minute + 1) * 60 < $now - PSRPT_FRIENDS_REPLAY_SECONDS) {
            psrpt_friends_remove(psrpt_friends_path('seen', $minute));
        }
    }
    foreach (psrpt_friends_names(psrpt_friends_path('rate'), '/^[0-9]+$/') as $hour) {
        if ((int) $hour < intdiv($now, 3600)) {
            psrpt_friends_remove(psrpt_friends_path('rate', $hour));
        }
    }
    // Status notes are fresh for minutes; their files go within a day
    foreach (psrpt_friends_names(psrpt_friends_path('statuses'), '/^[0-9a-f]{32}\.json$/') as $name) {
        $status = psrpt_friends_read_json(psrpt_friends_path('statuses', $name));
        if (!is_int($status['t'] ?? null) || $status['t'] < $now - 86400) {
            @unlink(psrpt_friends_path('statuses', $name));
        }
    }
    foreach (psrpt_friends_names(psrpt_friends_path('links'), '/^[0-9a-f]{32}$/') as $box) {
        $meta = psrpt_friends_read_json(psrpt_friends_path('links', $box, 'meta.json'));
        if (!is_int($meta['t'] ?? null) || $meta['t'] < $now - psrpt_friends_int('PSRPT_FRIENDS_LINK_SECONDS')) {
            psrpt_friends_remove_locked(psrpt_friends_path('links', $box));
        }
    }
    if (!$everything) {
        $last = psrpt_friends_read_json(psrpt_friends_path('sweep.json'));
        if (!is_int($last['t'] ?? null) || $last['t'] < $now - 3600 || $last['t'] > $now) {
            $GLOBALS['psrpt_friends_sweep'] = true;
        }
        return;
    }
    $sweeping = @fopen(psrpt_friends_path('sweep.lock'), 'c');
    if (!$sweeping || !flock($sweeping, LOCK_EX | LOCK_NB)) {
        return; // another request is at it
    }
    $last = psrpt_friends_read_json(psrpt_friends_path('sweep.json'));
    if (!$force && is_int($last['t'] ?? null) && $last['t'] >= $now - 3600 && $last['t'] <= $now) {
        flock($sweeping, LOCK_UN);
        fclose($sweeping);
        return; // another request has just done it
    }
    @file_put_contents(psrpt_friends_path('sweep.json'), json_encode(['t' => $now]));

    foreach (psrpt_friends_names(psrpt_friends_path('ids'), '/^[0-9a-f]{32}$/') as $fp_hex) {
        $fp = (string) hex2bin($fp_hex);
        $folder = psrpt_friends_path('ids', $fp_hex);
        $meta = psrpt_friends_identity($fp);
        if ($meta === null) {
            // Nobody has used it for a year (or it never got a card): everything goes
            $handle = @fopen($folder . DIRECTORY_SEPARATOR . '.lock', 'c');
            if ($handle && flock($handle, LOCK_EX | LOCK_NB)) {
                if (psrpt_friends_identity($fp) === null) {
                    foreach (psrpt_friends_names($folder, '/^push-[0-9a-f]{16}\.json$/') as $name) {
                        psrpt_friends_forget_push($fp_hex, substr($name, 5, 16));
                    }
                    psrpt_friends_remove($folder);
                }
                flock($handle, LOCK_UN);
            }
            if ($handle) {
                fclose($handle);
            }
            continue;
        }
        $status = psrpt_friends_read_json($folder . DIRECTORY_SEPARATOR . 'status.json');
        if (!is_int($status['t'] ?? null) || $status['t'] < $now - psrpt_friends_int('PSRPT_FRIENDS_STATUS_SECONDS')) {
            @unlink($folder . DIRECTORY_SEPARATOR . 'status.json');
        }
        foreach (psrpt_friends_names($folder, '/^inbox-[0-9a-f]{16}$/') as $inbox) {
            if (!in_array(substr($inbox, 6), $meta['devices'], true)) {
                psrpt_friends_remove($folder . DIRECTORY_SEPARATOR . $inbox);
                continue;
            }
            psrpt_friends_notes($folder . DIRECTORY_SEPARATOR . $inbox, psrpt_friends_int('PSRPT_FRIENDS_INBOX_DAYS'));
        }
        psrpt_friends_remove_temp($folder);
    }

    foreach (psrpt_friends_names(psrpt_friends_path('requests'), '/^[0-9a-f]{32}$/') as $box_hex) {
        $box = (string) hex2bin($box_hex);
        if (psrpt_friends_request_owner($box) === null) {
            psrpt_friends_remove_locked(psrpt_friends_request_folder($box));
            continue;
        }
        psrpt_friends_notes(psrpt_friends_request_folder($box), psrpt_friends_int('PSRPT_FRIENDS_REQUEST_DAYS'));
        psrpt_friends_remove_temp(psrpt_friends_request_folder($box));
    }

    foreach (psrpt_friends_names(psrpt_friends_path('groups'), '/^[0-9a-f]{32}$/') as $box_hex) {
        $box = (string) hex2bin($box_hex);
        if (psrpt_friends_group($box) === null) {
            psrpt_friends_remove_locked(psrpt_friends_group_folder($box));
            continue;
        }
        psrpt_friends_group_notes(psrpt_friends_group_folder($box));
        psrpt_friends_remove_temp(psrpt_friends_group_folder($box));
    }

    $oldest = $now - psrpt_friends_int('PSRPT_FRIENDS_REPORT_DAYS') * 86400;
    foreach (psrpt_friends_names(psrpt_friends_path('reports'), '/^[0-9]{10}-[0-9a-f]{8}\.txt$/') as $name) {
        if ((int) substr($name, 0, 10) < $oldest) {
            @unlink(psrpt_friends_path('reports', $name));
        }
    }
    flock($sweeping, LOCK_UN);
    fclose($sweeping);
}

/** Removes a folder when nobody is working in it right now. */
function psrpt_friends_remove_locked(string $folder): void
{
    $handle = @fopen($folder . DIRECTORY_SEPARATOR . '.lock', 'c');
    if (!$handle) {
        return;
    }
    if (flock($handle, LOCK_EX | LOCK_NB)) {
        psrpt_friends_remove($folder);
        flock($handle, LOCK_UN);
    }
    fclose($handle);
}

/** Half-written files of a request that was stopped (older than an hour). */
function psrpt_friends_remove_temp(string $folder): void
{
    foreach (psrpt_friends_names($folder, '/\.tmp$/') as $name) {
        $path = $folder . DIRECTORY_SEPARATOR . $name;
        if (@filemtime($path) < time() - 3600) {
            @unlink($path);
        }
    }
}

// ------------------------------------------------------------------ the request

if (PHP_SAPI === 'cli') {
    if (($argv[1] ?? '') === 'sweep') {
        if (psrpt_friends_dir() === null) {
            fwrite(STDERR, "The data folder cannot be used: set PSRPT_FRIENDS_DIR to a folder this user can write to.\n");
            exit(1);
        }
        psrpt_friends_sweep(true, true);
        exit(0);
    }
    fwrite(STDERR, "php psrpt_friends.php sweep    removes what has expired (the web server runs everything else)\n");
    exit(2);
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method === 'OPTIONS') {
    // A browser asking before a request; plain form posts do not need it, but it costs nothing
    header('Access-Control-Allow-Methods: POST, GET');
    header('Access-Control-Allow-Headers: Content-Type');
    header('Access-Control-Max-Age: 86400');
    psrpt_friends_ok();
}
if ($method !== 'POST') {
    // Somebody looking with a browser, or a monitoring check. Answered with 200 on
    // purpose: many reverse proxies ban an address after a number of 4xx answers.
    psrpt_friends_ok(['info' => 'PSRemotePlayTogether friends server. Nothing to see here: the app talks to it.']);
}
if ((int) ($_SERVER['CONTENT_LENGTH'] ?? 0) > 600 * 1024) {
    psrpt_friends_malformed();
}
if (psrpt_friends_dir() === null) {
    psrpt_friends_fail('cannot_write', 503);
}

$fields = [];
foreach ($_POST as $name => $value) {
    if (!is_string($name) || !preg_match('/^[a-z][a-z0-9_]{0,31}$/', $name) || !is_string($value)) {
        psrpt_friends_malformed();
    }
    $fields[$name] = $value;
}
if (count($fields) > 16) {
    psrpt_friends_malformed();
}
$GLOBALS['psrpt_friends_fields'] = $fields;
if (($fields['v'] ?? '1') !== '1') {
    // 200: an app that is newer than this file has done nothing wrong
    psrpt_friends_fail('version');
}

if (random_int(1, 50) === 1) {
    psrpt_friends_sweep(false);
}

switch ($fields['op'] ?? '') {
    case 'card_put':
        psrpt_friends_card_put();
        break;
    case 'card_get':
        psrpt_friends_card_get();
        break;
    case 'bundle_get':
        psrpt_friends_bundle_get();
        break;
    case 'prekeys_put':
        psrpt_friends_prekeys_put();
        break;
    case 'requests_set':
        psrpt_friends_requests_set();
        break;
    case 'request_put':
        psrpt_friends_request_put();
        break;
    case 'token_add':
    case 'token_remove':
        psrpt_friends_token_change($fields['op']);
        break;
    case 'inbox_put':
        psrpt_friends_inbox_put();
        break;
    case 'inbox_get':
        psrpt_friends_inbox_get();
        break;
    case 'inbox_ack':
        psrpt_friends_inbox_ack();
        break;
    case 'status_put':
        psrpt_friends_status_put();
        break;
    case 'status_get':
        psrpt_friends_status_get();
        break;
    case 'group_new':
        psrpt_friends_group_new();
        break;
    case 'group_token':
        psrpt_friends_group_token();
        break;
    case 'group_owner':
        psrpt_friends_group_owner();
        break;
    case 'group_put':
        psrpt_friends_group_put();
        break;
    case 'group_get':
        psrpt_friends_group_get();
        break;
    case 'link_put':
        psrpt_friends_link_put();
        break;
    case 'link_get':
        psrpt_friends_link_get();
        break;
    case 'push_key':
        psrpt_friends_ok(['key' => psrpt_friends_keys()['vapid_public']]);
        break;
    case 'push_set':
        psrpt_friends_push_set();
        break;
    case 'report':
        psrpt_friends_report();
        break;
    default:
        // 200: an app that is newer than this file has done nothing wrong
        psrpt_friends_fail('unknown_op');
}
