<?php
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Checks server/friends/psrpt_friends.php over HTTP, the way the app and the page use it.
//
//     php test/server/friends-server.test.php
//
// Starts PHP's built-in server on a free port with a data folder of its own (removed at the
// end), and a second one that plays a browser's push service, so that no push message ever
// leaves this machine. Signs as the identities of test/web/friends-vectors.json, and as new
// ones it makes. Moves the server's clock on through PSRPT_FRIENDS_TEST_CLOCK, which works only
// with PSRPT_FRIENDS_TEST=1. Needs PHP 7.4 or newer with openssl and curl.

declare(strict_types=1);

$root = dirname(__DIR__, 2);
$script = $root . '/server/friends/psrpt_friends.php';
$vectors = json_decode((string) file_get_contents($root . '/test/web/friends-vectors.json'), true);
if (!extension_loaded('openssl') || !extension_loaded('curl') || !is_array($vectors)) {
    fwrite(STDERR, "Needs the openssl and curl extensions, and test/web/friends-vectors.json.\n");
    exit(2);
}

$work = rtrim(sys_get_temp_dir(), '/') . '/psrpt-friends-test-' . bin2hex(random_bytes(4));
$data = $work . '/data';
mkdir($data, 0700, true);
mkdir($work . '/push');
// The clock starts at the beginning of an hour, so that the counters per hour do not roll over
// in the middle of a part of this test
$offset = 3600 - time() % 3600 + 5;
file_put_contents($work . '/clock', (string) $offset);
$processes = [];

register_shutdown_function(function () use (&$processes, $work): void {
    foreach ($processes as $process) {
        proc_terminate($process);
        proc_close($process);
    }
    remove_tree($work);
});

function remove_tree(string $path): void
{
    if (is_dir($path) && !is_link($path)) {
        foreach (scandir($path) ?: [] as $name) {
            if ($name !== '.' && $name !== '..') {
                remove_tree($path . '/' . $name);
            }
        }
        @rmdir($path);
    } else {
        @unlink($path);
    }
}

function free_port(): int
{
    $socket = stream_socket_server('tcp://127.0.0.1:0');
    $name = (string) stream_socket_get_name($socket, false);
    fclose($socket);
    return (int) substr((string) strrchr($name, ':'), 1);
}

/** Starts PHP's built-in server; waits until it answers. */
function serve(int $port, array $args, array $env, string $log)
{
    global $processes;
    // Warnings and notices go to the log, where the end of this test looks for them
    $process = proc_open(array_merge([PHP_BINARY, '-d', 'display_errors=0', '-d', 'log_errors=1', '-d', 'error_reporting=-1', '-S', '127.0.0.1:' . $port], $args),
        [0 => ['file', '/dev/null', 'r'], 1 => ['file', $log, 'a'], 2 => ['file', $log, 'a']], $pipes, null, $env + getenv());
    $processes[] = $process;
    for ($i = 0; $i < 100; $i++) {
        $socket = @fsockopen('127.0.0.1', $port, $errno, $error, 0.2);
        if ($socket) {
            fclose($socket);
            return $process;
        }
        usleep(50000);
    }
    fwrite(STDERR, "The server on port $port did not start:\n" . @file_get_contents($log));
    exit(1);
}

// What plays a push service: writes down each request and answers 201, or 410 ("gone") for an
// address with /gone in it
file_put_contents($work . '/push/receiver.php', '<?php
$headers = array_change_key_case(function_exists("getallheaders") ? getallheaders() : [], CASE_LOWER);
file_put_contents(__DIR__ . "/received.jsonl", json_encode(["method" => $_SERVER["REQUEST_METHOD"], "uri" => $_SERVER["REQUEST_URI"],
    "headers" => $headers, "body" => file_get_contents("php://input")]) . "\n", FILE_APPEND | LOCK_EX);
http_response_code(strpos($_SERVER["REQUEST_URI"], "/gone") !== false ? 410 : 201);
');
$push_port = free_port();
serve($push_port, [$work . '/push/receiver.php'], [], $work . '/push.log');
$push_base = 'http://127.0.0.1:' . $push_port;

// The friends server, with small limits so that they can be reached quickly
$server_env = [
    'PSRPT_FRIENDS_DIR' => $data,
    'PSRPT_FRIENDS_TEST' => '1',
    'PSRPT_FRIENDS_TEST_CLOCK' => $work . '/clock',
    'PSRPT_FRIENDS_PUSH_HOSTS' => '127.0.0.1',
    'PSRPT_FRIENDS_CORS' => '*',
    'PSRPT_FRIENDS_CLIENT_HEADER' => 'X-Forwarded-For',
    'PSRPT_FRIENDS_MAX_INBOX' => '6',
    'PSRPT_FRIENDS_MAX_REQUESTS' => '5',
    'PSRPT_FRIENDS_REQUESTS_PER_HOUR' => '8',
    'PSRPT_FRIENDS_MAX_GROUP' => '5',
    'PSRPT_FRIENDS_GROUPS_PER_HOUR' => '4',
    'PSRPT_FRIENDS_LINKS_PER_HOUR' => '3',
    'PSRPT_FRIENDS_MAX_LINK' => '300000',
    'PSRPT_FRIENDS_IDENTITIES_PER_HOUR' => '6',
    'PSRPT_FRIENDS_REPORTS_PER_HOUR' => '2',
    'PSRPT_FRIENDS_BUNDLES_PER_HOUR' => '40',
    'PSRPT_FRIENDS_MAX_TOKENS' => '3',
];
$port = free_port();
serve($port, ['-t', dirname($script)], $server_env, $work . '/server.log');
$url = 'http://127.0.0.1:' . $port . '/psrpt_friends.php';

// ------------------------------------------------------------------ checks

$checks = 0;
$failed = 0;

function check(string $what, bool $good, $detail = null): void
{
    global $checks, $failed;
    $checks++;
    if (!$good) {
        $failed++;
        echo "FAILED: $what\n";
        if ($detail !== null) {
            echo '  got: ', is_string($detail) ? $detail : json_encode($detail, JSON_UNESCAPED_SLASHES), "\n";
        }
    }
}

/** An answer is ok, or refused with that error (and that HTTP status). */
function expect(string $what, array $answer, ?string $error = null, int $status = 200): void
{
    $json = $answer['json'];
    $good = $answer['status'] === $status && is_array($json) && is_int($json['poll'] ?? null)
        && ($error === null ? ($json['ok'] ?? null) === true : (($json['ok'] ?? null) === false && ($json['error'] ?? null) === $error));
    check($what, $good, $answer['status'] . ' ' . $answer['body']);
}

function post(array $fields, array $headers = [], ?string $to = null): array
{
    global $url;
    $curl = curl_init($to ?? $url);
    curl_setopt_array($curl, [
        CURLOPT_POST => true,
        CURLOPT_POSTFIELDS => http_build_query($fields, '', '&', PHP_QUERY_RFC3986),
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_HEADER => true,
        CURLOPT_HTTPHEADER => $headers,
        CURLOPT_TIMEOUT => 30,
    ]);
    $raw = (string) curl_exec($curl);
    $size = (int) curl_getinfo($curl, CURLINFO_HEADER_SIZE);
    $answer = ['status' => (int) curl_getinfo($curl, CURLINFO_RESPONSE_CODE), 'head' => substr($raw, 0, $size), 'body' => substr($raw, $size)];
    $answer['json'] = json_decode($answer['body'], true);
    return $answer;
}

function advance(int $seconds): void
{
    global $offset, $work;
    $offset += $seconds;
    file_put_contents($work . '/clock', (string) $offset);
}

function now(): int
{
    global $offset;
    return time() + $offset;
}

/** Runs the sweep from the command line, as a cron job would. */
function sweep(): void
{
    global $script, $server_env;
    $process = proc_open([PHP_BINARY, $script, 'sweep'], [0 => ['file', '/dev/null', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
        $pipes, null, $server_env + getenv());
    $out = stream_get_contents($pipes[1]) . stream_get_contents($pipes[2]);
    $code = proc_close($process);
    check('the sweep runs from the command line', $code === 0, $out);
}

// ------------------------------------------------------------------ keys and signatures

function b64(string $bytes): string
{
    return rtrim(strtr(base64_encode($bytes), '+/', '-_'), '=');
}

function unb64(string $text): string
{
    return (string) base64_decode(strtr($text, '-_', '+/'));
}

function public_pem(string $point): string
{
    $der = hex2bin('3059301306072a8648ce3d020106082a8648ce3d030107034200') . $point;
    return "-----BEGIN PUBLIC KEY-----\n" . chunk_split(base64_encode($der), 64, "\n") . "-----END PUBLIC KEY-----\n";
}

/** A key pair of the vectors: the private key as RFC 5915 ECPrivateKey, built from d and the point. */
function key_of(string $d_hex, string $pub_hex): array
{
    $der = hex2bin('30770201010420' . $d_hex . 'a00a06082a8648ce3d030107a144034200' . $pub_hex);
    return ['pub' => (string) hex2bin($pub_hex),
        'pem' => "-----BEGIN EC PRIVATE KEY-----\n" . chunk_split(base64_encode($der), 64, "\n") . "-----END EC PRIVATE KEY-----\n"];
}

/** A status box name for an identity, as its friends would work it out (any 16 bytes do for the server). */
function status_box(array $identity): string
{
    return substr(hash('sha256', 'status box ' . $identity['fp'], true), 0, 16);
}

function new_key(): array
{
    $pair = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
    $details = openssl_pkey_get_details($pair);
    openssl_pkey_export($pair, $pem);
    return ['pub' => "\x04" . str_pad($details['ec']['x'], 32, "\0", STR_PAD_LEFT) . str_pad($details['ec']['y'], 32, "\0", STR_PAD_LEFT), 'pem' => $pem];
}

function der_to_raw(string $der): string
{
    $out = '';
    $position = 2;
    for ($i = 0; $i < 2; $i++) {
        $size = ord($der[$position + 1]);
        $out .= str_pad(ltrim(substr($der, $position + 2, $size), "\0"), 32, "\0", STR_PAD_LEFT);
        $position += 2 + $size;
    }
    return $out;
}

function raw_to_der(string $raw): string
{
    $integer = function (string $value): string {
        $value = ltrim($value, "\0");
        if ($value === '' || (ord($value[0]) & 0x80)) {
            $value = "\0" . $value;
        }
        return "\x02" . chr(strlen($value)) . $value;
    };
    $body = $integer(substr($raw, 0, 32)) . $integer(substr($raw, 32));
    return "\x30" . chr(strlen($body)) . $body;
}

function sign(array $key, string $message): string
{
    openssl_sign($message, $der, $key['pem'], OPENSSL_ALGO_SHA256);
    return der_to_raw($der);
}

function verify(string $point, string $message, string $raw): bool
{
    return openssl_verify($message, raw_to_der($raw), public_pem($point), OPENSSL_ALGO_SHA256) === 1;
}

/** The other valid signature of the same message: s becomes n - s. */
function malleate(string $raw): string
{
    $n = (string) hex2bin('ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551');
    $s = substr($raw, 32);
    $out = '';
    $borrow = 0;
    for ($i = 31; $i >= 0; $i--) {
        $digit = ord($n[$i]) - ord($s[$i]) - $borrow;
        $borrow = $digit < 0 ? 1 : 0;
        $out = chr(($digit + 256) % 256) . $out;
    }
    return substr($raw, 0, 32) . $out;
}

/** RequestMessage of coplay/src/friends_crypto.cpp, written again here. */
function request_message(string $op, string $t, array $fields): string
{
    unset($fields['v'], $fields['op'], $fields['t'], $fields['sig']);
    ksort($fields, SORT_STRING);
    $text = "psrpt friends 1\n" . $op . "\n" . $t . "\n";
    foreach ($fields as $name => $value) {
        $text .= $name . '=' . $value . "\n";
    }
    return $text;
}

/**
 * A signed request. Two requests with the same fields in the same second would be one signed
 * message, which the server takes once; like an app, this moves t on by a second then.
 */
function signed(string $op, array $fields, array $key, ?int $t = null): array
{
    static $sent = [];
    $t = $t ?? now();
    while (isset($sent[request_message($op, (string) $t, $fields)])) {
        $t++;
    }
    $sent[request_message($op, (string) $t, $fields)] = true;
    return ['op' => $op, 't' => (string) $t] + $fields + ['sig' => b64(sign($key, request_message($op, (string) $t, $fields)))];
}

// ------------------------------------------------------------------ identities and cards

function identity_of(array $vector): array
{
    return [
        'iks' => key_of($vector['iks']['d'], $vector['iks']['pub']),
        'ikd' => (string) hex2bin($vector['ikd']['pub']),
        'ikd_sig' => (string) hex2bin($vector['ikd_sig']),
        'fp' => (string) hex2bin($vector['fp']),
    ];
}

function new_identity(): array
{
    $iks = new_key();
    $ikd = new_key()['pub'];
    return ['iks' => $iks, 'ikd' => $ikd, 'ikd_sig' => sign($iks, 'psrpt ikd' . $ikd),
        'fp' => substr(hash('sha256', 'psrpt id' . $iks['pub'] . $ikd, true), 0, 16)];
}

/** A card as in docs/friends-protocol.md, signed by the identity; devices: [id (8 bytes), spk (65 bytes)]. */
function make_card(array $identity, int $seq, array $devices, string $profile = ''): string
{
    $card = "\x01" . $identity['iks']['pub'] . $identity['ikd'] . $identity['ikd_sig'] . pack('N', $seq) . chr(count($devices));
    foreach ($devices as [$id, $spk]) {
        $card .= $id . "\x01" . pack('N', 1) . $spk . sign($identity['iks'], 'psrpt spk' . $identity['fp'] . $id . pack('N', 1) . $spk);
    }
    $card .= pack('n', strlen($profile)) . $profile;
    return $card . sign($identity['iks'], 'psrpt card' . $card);
}

function card_put(array $identity, string $card, ?array $signer = null): array
{
    return post(signed('card_put', ['fp' => b64($identity['fp']), 'card' => b64($card)], $signer ?? $identity['iks']));
}

function received(): array
{
    global $work;
    $lines = @file($work . '/push/received.jsonl', FILE_IGNORE_NEW_LINES) ?: [];
    return array_map(function (string $line): array {
        return json_decode($line, true);
    }, $lines);
}

/** Waits until the push service has had $count requests (pushes go out after the answer). */
function wait_pushes(int $count): array
{
    for ($i = 0; $i < 100 && count(received()) < $count; $i++) {
        usleep(50000);
    }
    return received();
}

$alice = identity_of($vectors['alice']);
$bob = identity_of($vectors['bob']);
$alice_device = (string) hex2bin($vectors['names']['alice_device']);
$bob_device = (string) hex2bin($vectors['names']['bob_device']);
$bob_spk = (string) hex2bin($vectors['card']['spk']['pub']);
$vector_card = (string) hex2bin($vectors['card']['bytes']);
$envelope = (string) hex2bin($vectors['envelope']['bytes']); // from Alice's device to Bob's
$group_message = (string) hex2bin($vectors['group']['messages'][0]['bytes']); // from Alice's device

// ------------------------------------------------------------------ the basics

$get = curl_init($url);
curl_setopt_array($get, [CURLOPT_RETURNTRANSFER => true, CURLOPT_HEADER => true]);
$raw = (string) curl_exec($get);
check('GET is answered with 200', curl_getinfo($get, CURLINFO_RESPONSE_CODE) === 200);
check('GET says what this is', strpos($raw, 'friends server') !== false, $raw);
check('answers carry the CORS header the setting asks for', stripos($raw, "access-control-allow-origin: *\r\n") !== false, $raw);
check('answers are not cached', stripos($raw, "cache-control: no-store\r\n") !== false);
check('answers are JSON', stripos($raw, "content-type: application/json") !== false);
$options = curl_init($url);
curl_setopt_array($options, [CURLOPT_CUSTOMREQUEST => 'OPTIONS', CURLOPT_RETURNTRANSFER => true, CURLOPT_HEADER => true]);
$raw = (string) curl_exec($options);
check('OPTIONS is answered with 200 and the CORS header', curl_getinfo($options, CURLINFO_RESPONSE_CODE) === 200
    && stripos($raw, 'access-control-allow-origin: *') !== false, $raw);

$r = $vectors['request'];
check('the request message is built as RequestMessage builds it (vector)',
    request_message($r['op'], $r['t'], $r['fields']) === $r['message']);
check('the test signs and checks as the clients do (vector signature)',
    verify((string) hex2bin($vectors['primitives']['a']['pub']), (string) hex2bin($vectors['primitives']['signed']),
        (string) hex2bin($vectors['primitives']['sig'])));

expect('an unknown operation is refused with 200', post(['op' => 'frobnicate']), 'unknown_op');
expect('a later version of the protocol is refused with 200', post(['op' => 'card_get', 'v' => '2', 'fp' => b64($bob['fp'])]), 'version');
expect('a field that is not text is malformed', post(['op' => 'card_get', 'fp' => ['a' => 'b']]), 'malformed', 400);
expect('a field name of other characters is malformed', post(['op' => 'card_get', 'FP' => b64($bob['fp'])]), 'malformed', 400);
expect('a fingerprint of the wrong length is malformed', post(['op' => 'card_get', 'fp' => b64('short')]), 'malformed', 400);
expect('base64 with padding is malformed', post(['op' => 'card_get', 'fp' => base64_encode($bob['fp'])]), 'malformed', 400);

// ------------------------------------------------------------------ cards

$answer = card_put($bob, $vector_card);
expect('card_put takes the vector card', $answer);
check('card_put answers the seq', ($answer['json']['seq'] ?? null) === 2, $answer['body']);
$answer = post(['op' => 'card_get', 'fp' => b64($bob['fp'])]);
expect('card_get finds the card', $answer);
check('card_get answers the card byte for byte', unb64((string) ($answer['json']['card'] ?? '')) === $vector_card);
expect('card_get of an unknown identity', post(['op' => 'card_get', 'fp' => b64(random_bytes(16))]), 'unknown');

$answer = card_put($bob, $vector_card);
expect('the same card again is refused (seq not larger)', $answer, 'old_card');
check('the refusal says which seq the server has', ($answer['json']['seq'] ?? null) === 2, $answer['body']);
expect('a card with a lower seq is refused', card_put($bob, make_card($bob, 1, [[$bob_device, $bob_spk]])), 'old_card');

$broken = [
    'a changed byte in the profile (card_sig)' => 350,
    'a changed byte in a prekey signature (spk_sig)' => 200 + 78 + 5,
    'a changed byte in a signed prekey' => 200 + 13 + 20,
    'a changed byte in IKd (ikd_sig, fingerprint)' => 70,
    'a changed byte in ikd_sig' => 140,
    'a changed byte in card_sig' => strlen($vector_card) - 3,
    'a changed seq' => 198,
    'another version' => 0,
];
foreach ($broken as $what => $index) {
    $card = $vector_card;
    $card[$index] = chr(ord($card[$index]) ^ 0x01);
    expect("card_put refuses $what", card_put($bob, $card), 'bad_card');
}
expect('card_put refuses a card with a byte more', card_put($bob, $vector_card . "\x00"), 'bad_card');
expect('card_put refuses a card with a byte less', card_put($bob, substr($vector_card, 0, -1)), 'bad_card');
expect('card_put refuses a card of another identity than fp says',
    post(signed('card_put', ['fp' => b64($alice['fp']), 'card' => b64(make_card($bob, 5, [[$bob_device, $bob_spk]]))], $bob['iks'])), 'bad_card');
expect('card_put refuses a request signed by another key', card_put($bob, make_card($bob, 5, [[$bob_device, $bob_spk]]), $alice['iks']), 'bad_signature');
$eleven = [];
for ($i = 0; $i < 11; $i++) {
    $eleven[] = [random_bytes(8), new_key()['pub']];
}
expect('card_put refuses eleven devices', card_put($bob, make_card($bob, 5, $eleven)), 'bad_card');
expect('card_put takes ten devices', card_put($bob, make_card($bob, 3, array_slice($eleven, 0, 10))));
expect('card_put refuses two devices of one name', card_put($bob, make_card($bob, 5, [[$bob_device, $bob_spk], [$bob_device, new_key()['pub']]])), 'bad_card');
expect('card_put refuses a prekey that is no point on the curve',
    card_put($bob, make_card($bob, 5, [[$bob_device, substr($bob_spk, 0, 64) . chr(ord($bob_spk[64]) ^ 1)]])), 'bad_card');
expect('a card larger than the limit is malformed', card_put($bob, make_card($bob, 5, [[$bob_device, $bob_spk]], random_bytes(50000))), 'malformed', 400);
$bob_card = make_card($bob, 4, [[$bob_device, $bob_spk]]);
expect('card_put takes a newer card (seq 4)', card_put($bob, $bob_card));
check('card_get answers the newer card', unb64((string) (post(['op' => 'card_get', 'fp' => b64($bob['fp'])])['json']['card'] ?? '')) === $bob_card);

$alice_spk = new_key()['pub'];
expect('card_put takes Alice\'s card', card_put($alice, make_card($alice, 1, [[$alice_device, $alice_spk]])));

// ------------------------------------------------------------------ signed requests

$fields = ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('status one')];
$first = signed('status_put', $fields, $bob['iks']);
expect('a signed request', post($first));
expect('the same request again is a replay', post($first), 'replay');
$twin = $first;
$twin['sig'] = b64(malleate(unb64($first['sig'])));
check('the test made a second valid signature of the same message',
    $twin['sig'] !== $first['sig'] && verify($bob['iks']['pub'], request_message('status_put', $first['t'], $fields), unb64($twin['sig'])));
expect('the same message with the other valid signature is still a replay', post($twin), 'replay');
expect('v is not signed', post(['v' => '1'] + signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('status two')], $bob['iks'])));
$bad = signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('status three')], $bob['iks']);
$sig = unb64($bad['sig']);
$sig[10] = chr(ord($sig[10]) ^ 0x40);
$bad['sig'] = b64($sig);
expect('a changed signature is refused', post($bad), 'bad_signature');
$other = signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('what was signed')], $bob['iks']);
$other['note'] = b64('what was sent');
expect('a changed field is refused', post($other), 'bad_signature');
$extra = signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('four')], $bob['iks']);
$extra['zz'] = '1';
expect('a field that was not signed is refused', post($extra), 'bad_signature');
expect('a request signed by another identity is refused',
    post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('five')], $alice['iks'])), 'bad_signature');
expect('signed as another operation is refused',
    post(['op' => 'status_put'] + signed('token_add', ['fp' => b64($bob['fp']), 'note' => b64('six')], $bob['iks'])), 'bad_signature');
$answer = post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('seven')], $bob['iks'], now() - 301));
expect('a time more than five minutes ago is refused', $answer, 'stale_time');
check('the refusal says the server\'s time', abs(($answer['json']['now'] ?? 0) - now()) <= 2, $answer['body']);
expect('a time more than five minutes ahead is refused',
    post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('eight')], $bob['iks'], now() + 301)), 'stale_time');
expect('a time four minutes ago is fine', post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('nine')], $bob['iks'], now() - 240)));
expect('a time four minutes ahead is fine', post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('ten')], $bob['iks'], now() + 240)));
$no_sig = signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('eleven')], $bob['iks']);
unset($no_sig['sig']);
expect('a signed operation without a signature is malformed', post($no_sig), 'malformed', 400);
$bad_t = signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('twelve')], $bob['iks']);
$bad_t['t'] = 'soon';
expect('a time that is no number is malformed', post($bad_t), 'malformed', 400);
// The nonce n: two requests alike in the same second are two messages when their n differ
$same_t = now();
$poll = ['fp' => b64($bob['fp']), 'device' => b64($bob_device)];
$first_n = $poll + ['n' => b64(random_bytes(8))];
$second_n = $poll + ['n' => b64(random_bytes(8))];
expect('a request with a nonce', post(signed('inbox_get', $first_n, $bob['iks'], $same_t)));
expect('the same request in the same second with another nonce', post(signed('inbox_get', $second_n, $bob['iks'], $same_t)));
$again = ['op' => 'inbox_get', 't' => (string) $same_t] + $first_n
    + ['sig' => b64(sign($bob['iks'], request_message('inbox_get', (string) $same_t, $first_n)))];
expect('the same request with the same nonce is a replay', post($again), 'replay');
$changed_n = $again;
$changed_n['n'] = b64(random_bytes(8));
expect('a nonce that was not signed is refused', post($changed_n), 'bad_signature');
expect('a nonce that is not base64url is malformed',
    post(['op' => 'inbox_get', 't' => (string) $same_t] + $poll + ['n' => 'a.b', 'sig' => b64(sign($bob['iks'], request_message('inbox_get', (string) $same_t, $poll + ['n' => 'a.b'])))]),
    'malformed', 400);
$carol = new_identity();
expect('a signed request of an identity without a card', post(signed('status_put', ['fp' => b64($carol['fp']), 'box' => b64(status_box($carol)), 'note' => b64('x')], $carol['iks'])), 'unknown');

// ------------------------------------------------------------------ prekeys and bundles

function prekeys(int $from, int $count): array
{
    $keys = [];
    for ($id = $from; $id < $from + $count; $id++) {
        $keys[$id] = new_key()['pub'];
    }
    return $keys;
}

function packed(array $keys): string
{
    $raw = '';
    foreach ($keys as $id => $pub) {
        $raw .= pack('N', $id) . $pub;
    }
    return $raw;
}

function bob_inbox_get(array $more = []): array
{
    global $bob, $bob_device;
    return post(signed('inbox_get', ['fp' => b64($bob['fp']), 'device' => b64($bob_device)] + $more, $bob['iks']));
}

$keys = prekeys(1, 3);
$answer = post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => b64($bob_device), 'keys' => b64(packed($keys))], $bob['iks']));
expect('prekeys_put takes three one-time prekeys', $answer);
check('prekeys_put answers how many there are', ($answer['json']['prekeys'] ?? null) === 3, $answer['body']);
$answer = bob_inbox_get();
check('inbox_get of a device says how many prekeys are left', ($answer['json']['prekeys'] ?? null) === 3, $answer['body']);
$bob_device_text = b64($bob_device);
foreach ([1, 2] as $id) {
    $answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])]);
    expect("bundle_get $id", $answer);
    check("bundle_get $id answers the card", unb64((string) ($answer['json']['card'] ?? '')) === $bob_card);
    check("bundle_get $id takes prekey $id",
        ($answer['json']['prekeys'][$bob_device_text]['id'] ?? null) === $id
        && unb64((string) ($answer['json']['prekeys'][$bob_device_text]['pub'] ?? '')) === $keys[$id], $answer['body']);
}
$answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp']), 'devices' => b64(random_bytes(8))]);
check('bundle_get for devices not on the card takes nothing', ($answer['json']['prekeys'] ?? null) === [], $answer['body']);
$answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp']), 'devices' => $bob_device_text]);
check('bundle_get for the device asked for takes prekey 3', ($answer['json']['prekeys'][$bob_device_text]['id'] ?? null) === 3, $answer['body']);
$answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])]);
expect('bundle_get with no prekeys left', $answer);
check('a device without prekeys is missing from the bundle', strpos($answer['body'], '"prekeys":{}') !== false, $answer['body']);
check('inbox_get says no prekeys are left', (bob_inbox_get()['json']['prekeys'] ?? null) === 0);
expect('bundle_get of an unknown identity', post(['op' => 'bundle_get', 'fp' => b64(random_bytes(16))]), 'unknown');
expect('prekeys_put for a device not on the card',
    post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => b64(random_bytes(8)), 'keys' => b64(packed(prekeys(5, 1)))], $bob['iks'])), 'unknown_device');
expect('prekeys_put refuses id 0',
    post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed([0 => new_key()['pub']]))], $bob['iks'])), 'bad_key');
$off = new_key()['pub'];
$off[64] = chr(ord($off[64]) ^ 1);
expect('prekeys_put refuses a key that is no point on the curve',
    post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed([7 => $off]))], $bob['iks'])), 'bad_key');
expect('prekeys_put refuses a list of the wrong length',
    post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed(prekeys(8, 1)) . 'x')], $bob['iks'])), 'malformed', 400);
expect('prekeys_put refuses more than 100 at once',
    post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed(prekeys(1000, 101)))], $bob['iks'])), 'malformed', 400);
post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed(prekeys(10, 60)))], $bob['iks']));
$answer = post(signed('prekeys_put', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'keys' => b64(packed(prekeys(70, 60)))], $bob['iks']));
check('at most 100 prekeys are kept', ($answer['json']['prekeys'] ?? null) === 100, $answer['body']);
$answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])]);
check('the oldest prekeys made room', ($answer['json']['prekeys'][$bob_device_text]['id'] ?? null) === 30, $answer['body']);

// The limit per address: bundles take prekeys away
$count = 0;
do {
    $answer = post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])]);
    $count++;
} while (($answer['json']['ok'] ?? false) === true && $count < 60);
expect('bundle_get is limited per address', $answer, 'rate_limited');
check('the limit says when to come back', is_int($answer['json']['retry'] ?? null) && $answer['json']['retry'] <= 3600, $answer['body']);
expect('another address behind the proxy has a limit of its own',
    post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])], ['X-Forwarded-For: 203.0.113.1, 198.51.100.7']));
advance(3600);
expect('the limit is gone an hour later', post(['op' => 'bundle_get', 'fp' => b64($bob['fp'])]));

// ------------------------------------------------------------------ request boxes

$request_box = (string) hex2bin($vectors['names']['request_box']);
function request_put(string $box, string $note, array $headers = []): array
{
    return post(['op' => 'request_put', 'box' => b64($box), 'note' => b64($note)], $headers);
}

expect('request_put to a box nobody has', request_put($request_box, $envelope), 'unknown');
expect('requests_set', post(signed('requests_set', ['fp' => b64($bob['fp']), 'box' => b64($request_box)], $bob['iks'])));
expect('requests_set of a box somebody else has',
    post(signed('requests_set', ['fp' => b64($alice['fp']), 'box' => b64($request_box)], $alice['iks'])), 'box_taken');
expect('request_put', request_put($request_box, $envelope));
$answer = bob_inbox_get(['box' => b64($request_box)]);
expect('inbox_get of the request box', $answer);
$notes = $answer['json']['notes'] ?? [];
check('the request is there, byte for byte', count($notes) === 1 && unb64((string) $notes[0]['note']) === $envelope && is_string($notes[0]['id']), $answer['body']);
check('a request box answers no prekeys', !array_key_exists('prekeys', $answer['json'] ?? []), $answer['body']);
expect('inbox_get of somebody else\'s request box',
    post(signed('inbox_get', ['fp' => b64($alice['fp']), 'box' => b64($request_box)], $alice['iks'])), 'unknown');
$answer = post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'box' => b64($request_box), 'ids' => $notes[0]['id'] ?? ''], $bob['iks']));
check('inbox_ack takes the request away', ($answer['json']['removed'] ?? null) === 1, $answer['body']);
check('the request box is empty', (bob_inbox_get(['box' => b64($request_box)])['json']['notes'] ?? null) === []);
for ($i = 0; $i < 5; $i++) {
    request_put($request_box, $envelope . chr($i));
}
expect('a request box takes five here (PSRPT_FRIENDS_MAX_REQUESTS) and no more', request_put($request_box, $envelope), 'box_full');
$notes = bob_inbox_get(['box' => b64($request_box)])['json']['notes'] ?? [];
check('the requests come in the order they came', array_map(function ($n) {
    return substr(unb64($n['note']), -1);
}, $notes) === ["\x00", "\x01", "\x02", "\x03", "\x04"]);
post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'box' => b64($request_box), 'ids' => implode(',', array_column($notes, 'id'))], $bob['iks']));
expect('request 7 of the hour', request_put($request_box, $envelope));
expect('request 8 of the hour', request_put($request_box, $envelope));
expect('request 9 of the hour from one address is refused', request_put($request_box, $envelope), 'rate_limited');
expect('another address may still', request_put($request_box, $envelope, ['X-Forwarded-For: 198.51.100.9']));
$new_box = random_bytes(16);
expect('requests_set of a new box (a new friend link)', post(signed('requests_set', ['fp' => b64($bob['fp']), 'box' => b64($new_box)], $bob['iks'])));
advance(3600);
expect('the old request box is gone', request_put($request_box, $envelope), 'unknown');
expect('the new one takes requests', request_put($new_box, $envelope));
check('its requests start anew', count(bob_inbox_get(['box' => b64($new_box)])['json']['notes'] ?? []) === 1);
expect('a request too large is too big (an honest client can send one)', request_put($new_box, random_bytes(65537)), 'too_big');

// ------------------------------------------------------------------ tokens and inboxes

$token = random_bytes(16);
function inbox_put(string $token, string $note, ?string $device = null, ?array $to = null): array
{
    global $bob, $bob_device;
    return post(['op' => 'inbox_put', 'fp' => b64(($to ?? $bob)['fp']), 'device' => b64($device ?? $bob_device), 'token' => b64($token), 'note' => b64($note)]);
}

expect('inbox_put without a token the identity gave', inbox_put($token, $envelope), 'bad_token');
$answer = post(signed('token_add', ['fp' => b64($bob['fp']), 'hash' => b64(hash('sha256', $token, true))], $bob['iks']));
expect('token_add', $answer);
check('token_add answers how many there are', ($answer['json']['tokens'] ?? null) === 1, $answer['body']);
expect('inbox_put with the token', inbox_put($token, $envelope));
expect('inbox_put with another token', inbox_put(random_bytes(16), $envelope), 'bad_token');
expect('inbox_put to a device not on the card', inbox_put($token, $envelope, random_bytes(8)), 'unknown_device');
expect('inbox_put to an identity the server does not know', inbox_put($token, $envelope, null, ['fp' => random_bytes(16)]), 'unknown');
$largest = random_bytes(8192);
expect('inbox_put of the largest message (8 KB)', inbox_put($token, $largest));
expect('inbox_put of a message larger than 8 KB is too big', inbox_put($token, random_bytes(8193)), 'too_big');
$answer = bob_inbox_get();
$notes = $answer['json']['notes'] ?? [];
check('inbox_get answers both messages in order', count($notes) === 2 && unb64($notes[0]['note']) === $envelope && unb64($notes[1]['note']) === $largest, $answer['body']);
check('an inbox_get answer says whether there are more', ($answer['json']['more'] ?? null) === false);
$answer = post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'ids' => $notes[0]['id']], $bob['iks']));
check('inbox_ack takes one away', ($answer['json']['removed'] ?? null) === 1, $answer['body']);
check('the other one stays', count(bob_inbox_get()['json']['notes'] ?? []) === 1);
expect('inbox_ack with an id that is not one is malformed',
    post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'ids' => '../../etc'], $bob['iks'])), 'malformed', 400);
expect('inbox_get of somebody else\'s device',
    post(signed('inbox_get', ['fp' => b64($alice['fp']), 'device' => $bob_device_text], $alice['iks'])), 'unknown_device');
for ($i = 0; $i < 5; $i++) {
    inbox_put($token, $envelope);
}
expect('an inbox takes six here (PSRPT_FRIENDS_MAX_INBOX) and no more', inbox_put($token, $envelope), 'box_full');
$notes = bob_inbox_get()['json']['notes'] ?? [];
post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'ids' => implode(',', array_column($notes, 'id'))], $bob['iks']));
check('inbox_ack takes several at once', (bob_inbox_get()['json']['notes'] ?? null) === []);
expect('token_remove', post(signed('token_remove', ['fp' => b64($bob['fp']), 'hash' => b64(hash('sha256', $token, true))], $bob['iks'])));
expect('a removed token opens nothing', inbox_put($token, $envelope), 'bad_token');
$hashes = [random_bytes(32), random_bytes(32), random_bytes(32)];
foreach ($hashes as $hash) {
    post(signed('token_add', ['fp' => b64($bob['fp']), 'hash' => b64($hash)], $bob['iks']));
}
expect('at most three tokens here (PSRPT_FRIENDS_MAX_TOKENS)',
    post(signed('token_add', ['fp' => b64($bob['fp']), 'hash' => b64(random_bytes(32))], $bob['iks'])), 'too_many');
$answer = post(signed('token_add', ['fp' => b64($bob['fp']), 'hash' => b64($hashes[1])], $bob['iks']));
check('adding a token that is there changes nothing', ($answer['json']['tokens'] ?? null) === 3, $answer['body']);
$answer = post(signed('token_remove', ['fp' => b64($bob['fp']), 'hash' => b64(random_bytes(32))], $bob['iks']));
check('removing a token that is not there changes nothing', ($answer['json']['tokens'] ?? null) === 3, $answer['body']);
post(signed('token_remove', ['fp' => b64($bob['fp']), 'hash' => b64($hashes[0])], $bob['iks']));
expect('a token again', post(signed('token_add', ['fp' => b64($bob['fp']), 'hash' => b64(hash('sha256', $token, true))], $bob['iks'])));

// ------------------------------------------------------------------ status

function status_get(array $identities): array
{
    return post(['op' => 'status_get', 'boxes' => implode(',', array_map(function ($i) {
        return b64(status_box($i));
    }, $identities))]);
}

$bob_note = random_bytes(120);
expect('status_put', post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64($bob_note)], $bob['iks'])));
$answer = status_get([$bob, $alice, $carol]);
check('status_get answers the fresh notes only', ($answer['json']['notes'] ?? null) === [b64(status_box($bob)) => b64($bob_note)], $answer['body']);
expect('status_put without a box is malformed', post(signed('status_put', ['fp' => b64($bob['fp']), 'note' => b64('no box')], $bob['iks'])), 'malformed', 400);
// Under a new name (the next day, a new friends key) the one before goes
post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box(['fp' => 'next'])), 'note' => b64($bob_note)], $bob['iks']));
check('a new box takes the place of the one before', (status_get([$bob])['json']['notes'] ?? null) === [], status_get([$bob])['body']);
post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64($bob_note)], $bob['iks']));
post(signed('status_put', ['fp' => b64($alice['fp']), 'box' => b64(status_box($alice)), 'note' => b64('alice')], $alice['iks']));
check('status_get answers several', count(status_get([$bob, $alice])['json']['notes'] ?? []) === 2);
advance(170);
check('a status is fresh for three minutes', count(status_get([$bob, $alice])['json']['notes'] ?? []) === 2);
advance(11);
$answer = status_get([$bob, $alice]);
check('and then it is gone', strpos($answer['body'], '"notes":{}') !== false, $answer['body']);
post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64($bob_note)], $bob['iks']));
expect('status_put of nothing (invisible)', post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => ''], $bob['iks'])));
check('removes the status at once', (status_get([$bob])['json']['notes'] ?? null) === []);
expect('a status note larger than 2 KB is too big',
    post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64(random_bytes(2049))], $bob['iks'])), 'too_big');
$many = [];
for ($i = 0; $i < 401; $i++) {
    $many[] = ['fp' => random_bytes(16)];
}
expect('status_get of 400 boxes', status_get(array_slice($many, 0, 400)));
expect('status_get of 401 boxes is malformed', status_get($many), 'malformed', 400);

// ------------------------------------------------------------------ group boxes

$group_box = (string) hex2bin($vectors['names']['group_box']);
$owner = new_key();
$token1 = random_bytes(16);
$token2 = random_bytes(16);
function group(string $op, string $box, string $token, array $more = []): array
{
    return post(['op' => $op, 'box' => b64($box), 'token' => b64($token)] + $more);
}

expect('group_new', post(['op' => 'group_new', 'box' => b64($group_box), 'owner' => b64($owner['pub']), 'hash' => b64(hash('sha256', $token1, true))]));
expect('group_new of a box that is there',
    post(['op' => 'group_new', 'box' => b64($group_box), 'owner' => b64(new_key()['pub']), 'hash' => b64(hash('sha256', $token2, true))]), 'box_taken');
expect('group_new with an owner key that is no point', post(['op' => 'group_new', 'box' => b64(random_bytes(16)), 'owner' => b64($off), 'hash' => b64(random_bytes(32))]), 'bad_key');
$answer = group('group_put', $group_box, $token1, ['note' => b64($group_message)]);
expect('group_put with the token', $answer);
check('group_put answers the message\'s id', ($answer['json']['id'] ?? null) === 1, $answer['body']);
expect('group_put with another token', group('group_put', $group_box, $token2, ['note' => b64($group_message)]), 'bad_token');
expect('group_put to a box that is not there', group('group_put', random_bytes(16), $token1, ['note' => b64($group_message)]), 'unknown');
$answer = group('group_get', $group_box, $token1, ['after' => '0']);
check('group_get answers the message', ($answer['json']['notes'] ?? null) === [['id' => 1, 'note' => b64($group_message)]], $answer['body']);
expect('group_get with another token', group('group_get', $group_box, $token2, ['after' => '0']), 'bad_token');
expect('group_token signed by somebody else',
    post(signed('group_token', ['box' => b64($group_box), 'hash' => b64(hash('sha256', $token2, true))], $bob['iks'])), 'bad_signature');
$change = signed('group_token', ['box' => b64($group_box), 'hash' => b64(hash('sha256', $token2, true))], $owner);
expect('group_token signed by the owner key', post($change));
expect('group_token sent again is a replay', post($change), 'replay');
expect('the old token writes no more', group('group_put', $group_box, $token1, ['note' => b64($group_message)]), 'bad_token');
// A member who had not looked yet still reads, and finds the new token sealed for them
expect('the old token still reads', group('group_get', $group_box, $token1, ['after' => '0']));
expect('a token that never was reads nothing', group('group_get', $group_box, random_bytes(16), ['after' => '0']), 'bad_token');
expect('the new token writes', group('group_put', $group_box, $token2, ['note' => b64($group_message . 'b')]));
// A new owner key, signed with the one before: the old one changes nothing any more
$owner2 = new_key();
expect('group_owner signed by somebody else',
    post(signed('group_owner', ['box' => b64($group_box), 'owner' => b64($owner2['pub'])], $bob['iks'])), 'bad_signature');
expect('group_owner with no point', post(signed('group_owner', ['box' => b64($group_box), 'owner' => b64(str_repeat("\x04", 65))], $owner)), 'bad_key');
expect('group_owner signed by the owner key', post(signed('group_owner', ['box' => b64($group_box), 'owner' => b64($owner2['pub'])], $owner)));
expect('the old owner key makes no token any more',
    post(signed('group_token', ['box' => b64($group_box), 'hash' => b64(hash('sha256', $token1, true))], $owner)), 'bad_signature');
expect('the new owner key makes tokens',
    post(signed('group_token', ['box' => b64($group_box), 'hash' => b64(hash('sha256', $token2, true))], $owner2)));
$owner = $owner2;
$answer = group('group_get', $group_box, $token2, ['after' => '1']);
check('group_get answers what came after an id', array_column($answer['json']['notes'] ?? [], 'id') === [2], $answer['body']);
for ($i = 0; $i < 5; $i++) {
    group('group_put', $group_box, $token2, ['note' => b64($group_message . $i)]);
}
$answer = group('group_get', $group_box, $token2, ['after' => '0']);
check('a group box keeps the newest five here (PSRPT_FRIENDS_MAX_GROUP)', array_column($answer['json']['notes'] ?? [], 'id') === [3, 4, 5, 6, 7], $answer['body']);
expect('a group message larger than 8 KB is too big', group('group_put', $group_box, $token2, ['note' => b64(random_bytes(8193))]), 'too_big');
for ($i = 0; $i < 3; $i++) {
    post(['op' => 'group_new', 'box' => b64(random_bytes(16)), 'owner' => b64($owner['pub']), 'hash' => b64(random_bytes(32))]);
}
expect('new group boxes are limited per address',
    post(['op' => 'group_new', 'box' => b64(random_bytes(16)), 'owner' => b64($owner['pub']), 'hash' => b64(random_bytes(32))]), 'rate_limited');

// ------------------------------------------------------------------ link boxes

function link_op(string $op, string $box, string $side, array $more = []): array
{
    return post(['op' => $op, 'box' => b64($box), 'side' => $side] + $more);
}

advance(3600);
$link_box = (string) hex2bin($vectors['names']['link_box']);
$old_key = new_key()['pub'];
$new_key = new_key()['pub'];
expect('link_put of the old device\'s key', link_op('link_put', $link_box, '1', ['part' => '0', 'data' => b64($old_key)]));
$answer = link_op('link_get', $link_box, '2', ['part' => '0']);
check('the other side has nothing yet', ($answer['json']['parts'] ?? null) === [] && array_key_exists('data', $answer['json'] ?? []) && $answer['json']['data'] === null, $answer['body']);
expect('link_put of the new device\'s key', link_op('link_put', $link_box, '2', ['part' => '0', 'data' => b64($new_key)]));
$answer = link_op('link_get', $link_box, '1', ['part' => '0']);
check('link_get answers the other side\'s part', ($answer['json']['parts'] ?? null) === [0] && unb64((string) ($answer['json']['data'] ?? '')) === $old_key, $answer['body']);
$chunk = random_bytes(200000);
expect('link_put of a large part', link_op('link_put', $link_box, '1', ['part' => '1', 'data' => b64($chunk)]));
$answer = link_op('link_get', $link_box, '1');
check('link_get without a part lists the parts', ($answer['json']['parts'] ?? null) === [0, 1] && !array_key_exists('data', $answer['json'] ?? []), $answer['body']);
check('a large part comes back whole', unb64((string) (link_op('link_get', $link_box, '1', ['part' => '1'])['json']['data'] ?? '')) === $chunk);
expect('a link box takes at most its size (PSRPT_FRIENDS_MAX_LINK)', link_op('link_put', $link_box, '1', ['part' => '2', 'data' => b64(random_bytes(200000))]), 'box_full');
expect('a part larger than 256 KB is malformed', link_op('link_put', $link_box, '1', ['part' => '3', 'data' => b64(random_bytes(263169))]), 'malformed', 400);
expect('a side other than 1 or 2 is malformed', link_op('link_put', $link_box, '3', ['part' => '0', 'data' => b64('x')]), 'malformed', 400);
expect('link_put of nothing removes the box', link_op('link_put', $link_box, '1', ['part' => '0', 'data' => '']));
check('the box is gone', (link_op('link_get', $link_box, '1')['json']['parts'] ?? null) === []);
$short = random_bytes(16);
link_op('link_put', $short, '1', ['part' => '0', 'data' => b64('hello')]);
advance(100);
check('a link box is there for two minutes', (link_op('link_get', $short, '1')['json']['parts'] ?? null) === [0]);
advance(21);
check('and then it is gone', (link_op('link_get', $short, '1')['json']['parts'] ?? null) === []);
expect('the third new link box of the hour', link_op('link_put', random_bytes(16), '1', ['part' => '0', 'data' => b64('x')]));
expect('new link boxes are limited per address', link_op('link_put', random_bytes(16), '1', ['part' => '0', 'data' => b64('x')]), 'rate_limited');

// ------------------------------------------------------------------ the ban list

advance(3600);
$carol_device = random_bytes(8);
expect('card_put of a new identity', card_put($carol, make_card($carol, 1, [[$carol_device, new_key()['pub']]])));
expect('its status', post(signed('status_put', ['fp' => b64($carol['fp']), 'box' => b64(status_box($carol)), 'note' => b64('carol')], $carol['iks'])));
$carol_token = random_bytes(16);
post(signed('token_add', ['fp' => b64($carol['fp']), 'hash' => b64(hash('sha256', $carol_token, true))], $carol['iks']));
file_put_contents($data . '/banned.txt', "# banned on 2026-11-23\n\n" . b64($carol['fp']) . "   # sent spam\n" . bin2hex(random_bytes(16)) . "\n");
expect('a banned identity cannot put its card', card_put($carol, make_card($carol, 2, [[$carol_device, new_key()['pub']]])), 'banned');
expect('nor its status', post(signed('status_put', ['fp' => b64($carol['fp']), 'box' => b64(status_box($carol)), 'note' => b64('carol')], $carol['iks'])), 'banned');
expect('nor read its inbox', post(signed('inbox_get', ['fp' => b64($carol['fp']), 'device' => b64($carol_device)], $carol['iks'])), 'banned');
expect('nobody gets its card', post(['op' => 'card_get', 'fp' => b64($carol['fp'])]), 'unknown');
expect('nor a bundle', post(['op' => 'bundle_get', 'fp' => b64($carol['fp'])]), 'unknown');
check('nor its status', (status_get([$carol])['json']['notes'] ?? null) === []);
$from_carol = substr($envelope, 0, 2) . $carol['fp'] . substr($envelope, 18);
expect('its messages are refused', inbox_put($token, $from_carol), 'banned');
expect('the same message from somebody else is not', inbox_put($token, $envelope));
expect('its requests are refused', request_put($new_box, $from_carol), 'banned');
expect('its group messages are refused',
    group('group_put', $group_box, $token2, ['note' => b64(substr($group_message, 0, 17) . $carol['fp'] . substr($group_message, 33))]), 'banned');
expect('nothing goes into its inbox', inbox_put($carol_token, $envelope, $carol_device, $carol), 'banned');
$notes = bob_inbox_get()['json']['notes'] ?? [];
post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'ids' => implode(',', array_column($notes, 'id'))], $bob['iks']));

// ------------------------------------------------------------------ new identities per address

advance(3600);
for ($i = 0; $i < 6; $i++) {
    $someone = new_identity();
    $answer = card_put($someone, make_card($someone, 1, [[random_bytes(8), new_key()['pub']]]));
}
expect('the sixth new identity of the hour', $answer);
$someone = new_identity();
expect('the seventh new identity from one address is refused', card_put($someone, make_card($someone, 1, [[random_bytes(8), new_key()['pub']]])), 'rate_limited');
expect('a known identity still puts its card', card_put($bob, make_card($bob, 5, [[$bob_device, $bob_spk]])));

// ------------------------------------------------------------------ push messages

advance(3600);
$answer = post(['op' => 'push_key']);
$push_key = unb64((string) ($answer['json']['key'] ?? ''));
check('push_key answers the server\'s public key', strlen($push_key) === 65 && $push_key[0] === "\x04", $answer['body']);
check('push_key answers the same key each time', unb64((string) (post(['op' => 'push_key'])['json']['key'] ?? '')) === $push_key);
check('the server\'s keys are readable for it only', (fileperms($data . '/server.json') & 0777) === 0600);

function push_set(string $endpoint, array $more = []): array
{
    global $bob, $bob_device;
    return post(signed('push_set', ['fp' => b64($bob['fp']), 'device' => b64($bob_device), 'endpoint' => $endpoint,
        'p256dh' => b64(new_key()['pub']), 'auth' => b64(random_bytes(16))] + $more, $bob['iks']));
}

$subscription = $data . '/ids/' . bin2hex($bob['fp']) . '/push-' . bin2hex($bob_device) . '.json';
expect('push_set to an address that is not a push service', push_set('https://example.org/push'), 'not_allowed');
expect('push_set to a private address', push_set('http://10.0.0.1/push'), 'not_allowed');
// Each group box with a token that reads it: a box without one, or with a wrong one, is left out
expect('push_set with a group box but no token', push_set($push_base . '/push/bob', ['groups' => b64($group_box)]), 'malformed', 400);
$answer = push_set($push_base . '/push/bob', ['groups' => b64($group_box) . '.' . b64(random_bytes(16))]);
check('push_set leaves out a group box whose token is wrong', ($answer['json']['groups'] ?? null) === 0, $answer['body']);
$answer = push_set($push_base . '/push/bob', ['groups' => b64($group_box) . '.' . b64($token2) . ',' . b64(random_bytes(16)) . '.' . b64($token2)]);
expect('push_set', $answer);
check('push_set for the group boxes there are', ($answer['json']['groups'] ?? null) === 1, $answer['body']);

/** Checks a push message as a push service would: empty, TTL, VAPID with the server's key, a valid ES256 JWT for its origin. */
function check_push(string $what, ?array $push, string $path): void
{
    global $push_base, $push_key;
    check("$what: a push message arrives", $push !== null);
    if ($push === null) {
        return;
    }
    $headers = $push['headers'];
    check("$what: at the subscription's address", $push['method'] === 'POST' && $push['uri'] === $path, $push);
    check("$what: empty", $push['body'] === '' && ($headers['content-length'] ?? '0') === '0', $push);
    check("$what: TTL a day", ($headers['ttl'] ?? '') === '86400', $headers);
    $ok = preg_match('/^vapid t=([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+), k=([A-Za-z0-9_-]+)$/', (string) ($headers['authorization'] ?? ''), $m);
    check("$what: Authorization is vapid t=..., k=...", $ok === 1, $headers['authorization'] ?? '');
    if ($ok !== 1) {
        return;
    }
    check("$what: k is the key push_key answers", unb64($m[4]) === $push_key);
    $head = json_decode(unb64($m[1]), true);
    $claims = json_decode(unb64($m[2]), true);
    check("$what: the JWT is ES256", ($head['alg'] ?? '') === 'ES256' && ($head['typ'] ?? '') === 'JWT', $head);
    check("$what: for the push service's origin", ($claims['aud'] ?? '') === $push_base, $claims);
    check("$what: valid for 12 hours", abs(($claims['exp'] ?? 0) - (now() + 12 * 3600)) <= 120, $claims);
    check("$what: says whom to contact", preg_match('/^(https:|mailto:)/', (string) ($claims['sub'] ?? '')) === 1, $claims);
    check("$what: signed with the server's key", verify($push_key, $m[1] . '.' . $m[2], unb64($m[3])));
}

$before = count(received());
expect('a message for a device with a subscription', inbox_put($token, $envelope));
$pushes = wait_pushes($before + 1);
check_push('a message', $pushes[$before] ?? null, '/push/bob');
expect('a request for an identity with a subscription', request_put($new_box, $envelope));
$pushes = wait_pushes($before + 2);
check_push('a request', $pushes[$before + 1] ?? null, '/push/bob');
expect('a group message for a subscribed group box', group('group_put', $group_box, $token2, ['note' => b64($group_message)]));
$pushes = wait_pushes($before + 3);
check_push('a group message', $pushes[$before + 2] ?? null, '/push/bob');
$from_bob = substr($group_message, 0, 17) . $bob['fp'] . $bob_device . substr($group_message, 41);
expect('a group message of the subscribed device itself', group('group_put', $group_box, $token2, ['note' => b64($from_bob)]));
expect('a status (no push for that)', post(signed('status_put', ['fp' => b64($bob['fp']), 'box' => b64(status_box($bob)), 'note' => b64('s')], $bob['iks'])));
usleep(500000);
check('the writer\'s own device gets no push, nor does a status', count(received()) === $before + 3, count(received()) - $before);

// Without curl: the same message through PHP's own streams
$port2 = free_port();
serve($port2, ['-t', dirname($script)], ['PSRPT_FRIENDS_TEST_NO_CURL' => '1'] + $server_env, $work . '/server2.log');
$answer = post(['op' => 'inbox_put', 'fp' => b64($bob['fp']), 'device' => $bob_device_text, 'token' => b64($token), 'note' => b64($envelope)], [],
    'http://127.0.0.1:' . $port2 . '/psrpt_friends.php');
expect('a message through a server without curl', $answer);
$pushes = wait_pushes($before + 4);
check_push('without curl', $pushes[$before + 3] ?? null, '/push/bob');

expect('push_set to an address that answers "gone"', push_set($push_base . '/gone/bob'));
inbox_put($token, $envelope);
wait_pushes($before + 5);
for ($i = 0; $i < 40 && is_file($subscription); $i++) {
    usleep(50000);
}
check('a subscription the push service calls gone is removed', !is_file($subscription));
inbox_put($token, $envelope);
usleep(300000);
check('and gets no more push messages', count(received()) === $before + 5, count(received()) - $before);
push_set($push_base . '/push/bob', ['groups' => b64($group_box) . '.' . b64($token2)]);
expect('push_set with no address', push_set(''));
check('removes the subscription', !is_file($subscription));
check('and its place in the group box', !is_file($data . '/groups/' . bin2hex($group_box) . '/push/' . bin2hex($bob['fp']) . '-' . bin2hex($bob_device)));
$notes = bob_inbox_get()['json']['notes'] ?? [];
post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'device' => $bob_device_text, 'ids' => implode(',', array_column($notes, 'id'))], $bob['iks']));

// ------------------------------------------------------------------ reports

$report = "Messages from 4c8ed009 on 2026-11-23:\n„Hallo“ – something rude 🙂";
expect('report', post(['op' => 'report', 'text' => $report]));
$files = glob($data . '/reports/*.txt') ?: [];
check('the report is kept for the operator', count($files) === 1 && strpos((string) file_get_contents($files[0]), $report) !== false);
expect('a report that is not UTF-8 is malformed', post(['op' => 'report', 'text' => "\xff\xfe"]), 'malformed', 400);
expect('a second report', post(['op' => 'report', 'text' => 'two']));
expect('reports are limited per address', post(['op' => 'report', 'text' => 'three']), 'rate_limited');

// ------------------------------------------------------------------ what is stored

$names_ok = true;
$addresses = [];
$iterator = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($data, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::SELF_FIRST);
foreach ($iterator as $file) {
    if (!preg_match('/^[0-9a-z.\-]+$/', $file->getFilename())) {
        $names_ok = false;
    }
    if ($file->isFile() && strpos($file->getFilename(), 'push-') !== 0) {
        $content = (string) file_get_contents($file->getPathname());
        foreach (['127.0.0.1', '198.51.100.7', '198.51.100.9', '203.0.113.1'] as $address) {
            if (strpos($content, $address) !== false) {
                $addresses[] = $file->getPathname();
            }
        }
    }
}
check('every file is named after a hash or a word of the script\'s', $names_ok);
check('no client address is stored anywhere', $addresses === [], $addresses);
foreach (['server.log', 'server2.log'] as $log) {
    $text = (string) file_get_contents($work . '/' . $log);
    check("the server raised no warning, notice or error ($log)", preg_match('/(Warning|Notice|Deprecated|Fatal error|Parse error)/', $text) === 0, $text);
}

// ------------------------------------------------------------------ expiry and the sweep

$notes = bob_inbox_get(['box' => b64($new_box)])['json']['notes'] ?? [];
post(signed('inbox_ack', ['fp' => b64($bob['fp']), 'box' => b64($new_box), 'ids' => implode(',', array_column($notes, 'id'))], $bob['iks']));
inbox_put($token, $envelope);
request_put($new_box, $envelope);
advance(13 * 86400);
check('a request waits 14 days', count(bob_inbox_get(['box' => b64($new_box)])['json']['notes'] ?? []) === 1);
advance(2 * 86400);
check('and then it is gone', (bob_inbox_get(['box' => b64($new_box)])['json']['notes'] ?? null) === []);
check('a message waits longer', count(bob_inbox_get()['json']['notes'] ?? []) === 1);
advance(16 * 86400);
check('30 days, and then it is gone', (bob_inbox_get()['json']['notes'] ?? null) === []);
expect('a group box nobody used for 30 days is gone', group('group_get', $group_box, $token2, ['after' => '0']), 'unknown');
expect('a group box of that name can be made again',
    post(['op' => 'group_new', 'box' => b64($group_box), 'owner' => b64($owner['pub']), 'hash' => b64(hash('sha256', $token1, true))]));
check('and starts empty', (group('group_get', $group_box, $token1, ['after' => '0'])['json']['notes'] ?? null) === []);
expect('a card in use stays', post(['op' => 'card_get', 'fp' => b64($bob['fp'])]));
advance(60 * 86400);
sweep();
check('the sweep removes reports after 90 days', (glob($data . '/reports/*.txt') ?: []) === []);
check('the sweep forgets signed requests whose time is past', count(glob($data . '/seen/*') ?: []) === 0, glob($data . '/seen/*'));
check('the sweep removes the counters of hours gone by', count(glob($data . '/rate/*') ?: []) === 0, glob($data . '/rate/*'));
check('the sweep removes link boxes', count(glob($data . '/links/*') ?: []) === 0);
check('the sweep removes group boxes nobody uses', count(glob($data . '/groups/*') ?: []) === 0, glob($data . '/groups/*'));
check('a card nobody used for less than a year stays', is_file($data . '/ids/' . bin2hex($bob['fp']) . '/card'));
advance(275 * 86400);
expect('a card used 335 days ago is still there', post(['op' => 'card_get', 'fp' => b64($bob['fp'])]));
advance(31 * 86400);
expect('a card nobody used for a year is gone', post(['op' => 'card_get', 'fp' => b64($bob['fp'])]), 'unknown');
sweep();
check('the sweep removes it with everything that belonged to it', !is_dir($data . '/ids/' . bin2hex($bob['fp'])));
check('and its request box', !is_dir($data . '/requests/' . bin2hex($new_box)));
expect('an identity that expired can come back', card_put($bob, make_card($bob, 1, [[$bob_device, $bob_spk]])));

// ------------------------------------------------------------------ the settings file, the folder's lock

check('the data folder locks itself for Apache', trim((string) @file_get_contents($data . '/.htaccess')) === 'Require all denied');
// A copy of the script with psrpt_friends.config.php beside it, and no environment
$configured = $work . '/configured';
@mkdir($configured . '/site', 0700, true);
copy($script, $configured . '/site/psrpt_friends.php');
file_put_contents($configured . '/site/psrpt_friends.config.php',
    "<?php return ['PSRPT_FRIENDS_DIR' => " . var_export($configured . '/data', true) . ", 'PSRPT_FRIENDS_POLL_MS' => 45000];\n");
$configured_port = free_port();
serve($configured_port, ['-t', $configured . '/site'], ['PSRPT_FRIENDS_DIR' => '', 'PSRPT_FRIENDS_POLL_MS' => ''], $work . '/configured.log');
$configured_answer = json_decode((string) @file_get_contents('http://127.0.0.1:' . $configured_port . '/psrpt_friends.php'), true);
check('settings come from psrpt_friends.config.php', ($configured_answer['poll'] ?? null) === 45000, $configured_answer);
// A request that uses the data folder: it is made there, with its lock
@file_get_contents('http://127.0.0.1:' . $configured_port . '/psrpt_friends.php', false, stream_context_create(['http' => [
    'method' => 'POST', 'header' => 'Content-Type: application/x-www-form-urlencoded', 'ignore_errors' => true,
    'content' => http_build_query(['op' => 'card_get', 'fp' => b64(random_bytes(16))])]]));
check('the data folder named there is used', is_file($configured . '/data/.htaccess'), glob($configured . '/data/*'));
// The environment wins over the file
$env_port = free_port();
serve($env_port, ['-t', $configured . '/site'], ['PSRPT_FRIENDS_DIR' => '', 'PSRPT_FRIENDS_POLL_MS' => '50000'], $work . '/configured-env.log');
$env_answer = json_decode((string) @file_get_contents('http://127.0.0.1:' . $env_port . '/psrpt_friends.php'), true);
check('an environment variable wins over the file', ($env_answer['poll'] ?? null) === 50000, $env_answer);

echo "$checks checks, $failed failed\n";
exit($failed === 0 ? 0 : 1);
