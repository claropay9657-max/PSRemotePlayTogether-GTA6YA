<?php
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// PSRemotePlayTogether meeting point.
//
// Lets a friend join a host whose router has no forwarded port: host and guest
// each leave a short note here that says under which addresses they can be
// reached, read the other side's note, and then connect to each other directly.
// Nothing of a session ever passes through this file; it only keeps the notes,
// for half a minute.
//
// The notes are sealed with a key that only the people with the invite have.
// This script cannot read them, and the name of a "room" cannot be turned back
// into that key. It stores no IP addresses and writes no log of its own.
//
// One file, no dependencies, PHP 7.4 or newer. README.md next to it says how to
// install it and what to tell a reverse proxy. The protocol is described in
// coplay/include/coplay/meet.h and docs/nat-traversal.md of the project:
// https://github.com/Crypto90/PSRemotePlayTogether
//
// Request (POST, form fields):
//     v=1  room=<22 characters>  id=<16 hex digits>  role=h|g  note=<sealed note, or empty to remove it>
// Answer (text/plain):
//     psrpt-meet 1
//     poll 3000
//     note <a note of the other role>      (one line per note)

declare(strict_types=1);

// ------------------------------------------------------------------ settings
//
// Each one can also be set as an environment variable of the same name.

/** Folder for the notes. Empty: "psrpt-meet" in the system's temporary folder. Must not be reachable from the web. */
const PSRPT_MEET_DIR = '';
/** Seconds a note stays when it is not posted again. Hosts post every PSRPT_MEET_POLL_MS. */
const PSRPT_MEET_TTL = 30;
/** How often a sharing host is asked to come back, in milliseconds. Raise it if the server gets busy. */
const PSRPT_MEET_POLL_MS = 3000;
/** Sessions that can have notes here at the same time. One uses a few hundred bytes, at most 12 KB. */
const PSRPT_MEET_MAX_ROOMS = 5000;
/** Notes per room: of hosts (there is one; a second slot covers a host that restarted) and of guests. */
const PSRPT_MEET_MAX_HOSTS = 2;
const PSRPT_MEET_MAX_GUESTS = 10;
/** Longest note, in characters. The app's notes have about 150. */
const PSRPT_MEET_MAX_NOTE = 1024;

function psrpt_meet_setting(string $name, $default)
{
    $value = getenv($name);
    if ($value === false || $value === '') {
        return $default;
    }
    return is_int($default) ? max(1, (int) $value) : $value;
}

function psrpt_meet_answer(int $status, array $lines): void
{
    http_response_code($status);
    header('Content-Type: text/plain; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Content-Type-Options: nosniff');
    header('X-Robots-Tag: noindex, nofollow');
    echo "psrpt-meet 1\n";
    foreach ($lines as $line) {
        echo $line, "\n";
    }
    exit;
}

/** The folder with the notes, created on first use; null if that is not possible. */
function psrpt_meet_dir(): ?string
{
    $dir = (string) psrpt_meet_setting('PSRPT_MEET_DIR', PSRPT_MEET_DIR);
    if ($dir === '') {
        $dir = rtrim(sys_get_temp_dir(), '/\\') . DIRECTORY_SEPARATOR . 'psrpt-meet';
    }
    if (!is_dir($dir) && !@mkdir($dir, 0700, true) && !is_dir($dir)) {
        return null;
    }
    return is_writable($dir) ? $dir : null;
}

/** Now and then: removes the files of rooms that nobody has written to for a while. */
function psrpt_meet_sweep(string $dir, int $ttl): void
{
    $limit = time() - 2 * $ttl;
    foreach (new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS) as $file) {
        if (!$file->isFile() || $file->getExtension() !== 'room' || $file->getMTime() >= $limit) {
            continue;
        }
        // Only when nobody is working on it right now
        $handle = @fopen($file->getPathname(), 'c+');
        if (!$handle) {
            continue;
        }
        if (flock($handle, LOCK_EX | LOCK_NB)) {
            clearstatcache(true, $file->getPathname());
            if (@filemtime($file->getPathname()) < $limit) {
                @unlink($file->getPathname());
            }
            flock($handle, LOCK_UN);
        }
        fclose($handle);
    }
}

// ------------------------------------------------------------------ the request

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method !== 'POST') {
    // Somebody looking with a browser, or a monitoring check. Answered with 200 on
    // purpose: many reverse proxies ban an address after a number of 4xx answers.
    psrpt_meet_answer(200, ['info PSRemotePlayTogether meeting point. Nothing to see here: the app talks to it.']);
}

$version = $_POST['v'] ?? '';
$room = $_POST['room'] ?? '';
$id = $_POST['id'] ?? '';
$role = $_POST['role'] ?? '';
$note = $_POST['note'] ?? '';
$max_note = PSRPT_MEET_MAX_NOTE;

if (!is_string($version) || !is_string($room) || !is_string($id) || !is_string($role) || !is_string($note)
    || !preg_match('/^[A-Za-z0-9_-]{22}$/', $room)
    || !preg_match('/^[0-9a-f]{16}$/', $id)
    || ($role !== 'h' && $role !== 'g')
    || strlen($note) > $max_note
    || ($note !== '' && !preg_match('/^[A-Za-z0-9_-]+$/', $note))) {
    psrpt_meet_answer(400, ['error malformed request']);
}
if ($version !== '1') {
    // 200: an app that is newer than this file has done nothing wrong
    psrpt_meet_answer(200, ['error this meeting point does not know version ' . preg_replace('/[^0-9]/', '', substr($version, 0, 6)) . ' of the protocol']);
}

$ttl = (int) psrpt_meet_setting('PSRPT_MEET_TTL', PSRPT_MEET_TTL);
$poll = (int) psrpt_meet_setting('PSRPT_MEET_POLL_MS', PSRPT_MEET_POLL_MS);
$max_rooms = (int) psrpt_meet_setting('PSRPT_MEET_MAX_ROOMS', PSRPT_MEET_MAX_ROOMS);
// A note has to outlive the pause between two visits of its host
$ttl = max($ttl, (int) ceil($poll * 4 / 1000));

$dir = psrpt_meet_dir();
if ($dir === null) {
    psrpt_meet_answer(503, ['error the meeting point has nowhere to keep notes']);
}
if (random_int(1, 50) === 1) {
    psrpt_meet_sweep($dir, $ttl);
}

// The file is named after a hash of the room: nothing a client sends becomes part
// of a path, and it works the same on file systems that ignore upper and lower case
$path = $dir . DIRECTORY_SEPARATOR . hash('sha256', $room) . '.room';
$now = time();

$handle = null;
for ($attempt = 0; $attempt < 3; $attempt++) {
    $existed = file_exists($path);
    if (!$existed) {
        if ($note === '') {
            // Nothing to remove and nobody there
            psrpt_meet_answer(200, ['poll ' . $poll]);
        }
        if (iterator_count(new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS)) >= $max_rooms) {
            psrpt_meet_sweep($dir, $ttl);
            if (iterator_count(new FilesystemIterator($dir, FilesystemIterator::SKIP_DOTS)) >= $max_rooms) {
                header('Retry-After: 60');
                psrpt_meet_answer(503, ['error the meeting point is full, try again in a minute']);
            }
        }
    }
    $handle = @fopen($path, 'c+');
    if (!$handle || !flock($handle, LOCK_EX)) {
        if ($handle) {
            fclose($handle);
        }
        psrpt_meet_answer(503, ['error the meeting point cannot keep the note']);
    }
    // The sweep may have removed the file between opening and locking it: then what
    // we hold is no longer the room's file
    clearstatcache(true, $path);
    $held = fstat($handle);
    $named = @stat($path);
    if ($held && $named && $held['ino'] === $named['ino']) {
        break;
    }
    flock($handle, LOCK_UN);
    fclose($handle);
    $handle = null;
}
if (!$handle) {
    psrpt_meet_answer(503, ['error the meeting point cannot keep the note']);
}

$entries = [];
$raw = stream_get_contents($handle);
if (is_string($raw) && $raw !== '') {
    $decoded = json_decode($raw, true);
    if (is_array($decoded)) {
        foreach ($decoded as $entry) {
            if (is_array($entry) && isset($entry['id'], $entry['role'], $entry['note'], $entry['t'])
                && is_string($entry['id']) && is_string($entry['note']) && is_int($entry['t'])
                && ($entry['role'] === 'h' || $entry['role'] === 'g')
                && $entry['id'] !== $id          // the sender's own note is replaced below
                && $now - $entry['t'] <= $ttl) { // and what is too old is forgotten
                $entries[] = $entry;
            }
        }
    }
}

if ($note !== '') {
    $entries[] = ['id' => $id, 'role' => $role, 'note' => $note, 't' => $now];
    // Never more than a handful per role: the oldest make room
    $limit = ['h' => PSRPT_MEET_MAX_HOSTS, 'g' => PSRPT_MEET_MAX_GUESTS];
    foreach ($limit as $which => $most) {
        $count = 0;
        foreach ($entries as $entry) {
            $count += $entry['role'] === $which ? 1 : 0;
        }
        foreach ($entries as $index => $entry) {
            if ($count <= $most) {
                break;
            }
            if ($entry['role'] === $which) {
                unset($entries[$index]);
                $count--;
            }
        }
    }
    $entries = array_values($entries);
}

$lines = ['poll ' . $poll];
foreach ($entries as $entry) {
    if ($entry['role'] !== $role) {
        $lines[] = 'note ' . $entry['note'];
    }
}

if ($entries) {
    rewind($handle);
    ftruncate($handle, 0);
    fwrite($handle, json_encode($entries));
    fflush($handle);
} else {
    @unlink($path);
}
flock($handle, LOCK_UN);
fclose($handle);

psrpt_meet_answer(200, $lines);
