#!/bin/sh
# Pictures of the friends panel (build/gui/friends-shot) with real friends clients against the
# friends server, started here with PHP's own web server on a free port with an empty data folder.
#
#   test/friends-shot.sh <folder for the pictures> [build folder]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?folder for the pictures}
build=${2:-$root/build}
test_bin="$build/gui/friends-shot"
[ -x "$test_bin" ] || { echo "no $test_bin: build the friends-shot target first"; exit 2; }
command -v php >/dev/null || { echo "php is needed"; exit 2; }

work=$(mktemp -d)
trap 'kill "$php_pid" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM
mkdir -p "$work/www/example.org" "$work/data"
cp "$root/server/friends/psrpt_friends.php" "$work/www/"
# The pages whose links get a preview in the pictures: "example.org", asked for through this
# server as a proxy (the picture tool sends only that name here), never the real site
cat >"$work/router.php" <<'PHP'
<?php
$uri = $_SERVER['REQUEST_URI'] ?? '';
if (preg_match('#^http://example\.org/([a-z0-9.-]+)$#', $uri, $m)) {
    $file = __DIR__ . '/www/example.org/' . $m[1];
    if (!is_file($file)) {
        http_response_code(404);
        return true;
    }
    header('Content-Type: ' . (str_ends_with($file, '.jpg') ? 'image/jpeg' : 'text/html'));
    readfile($file);
    return true;
}
return false;
PHP
port=$(php -r '$s = stream_socket_server("tcp://127.0.0.1:0"); echo explode(":", stream_socket_get_name($s, false))[1];')
# The server takes its settings from the environment too: an empty data folder of its own
PSRPT_FRIENDS_DIR="$work/data" php -S "127.0.0.1:$port" -t "$work/www" "$work/router.php" >"$work/php.log" 2>&1 &
php_pid=$!
sleep 0.5
"$test_bin" "http://127.0.0.1:$port/psrpt_friends.php" "$out" "$work/www" || { echo "--- server log"; tail -50 "$work/php.log"; exit 1; }
