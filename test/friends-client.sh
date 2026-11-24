#!/bin/sh
# Friends end to end: the app's friends client (build/gui/friends-client-test) against the friends
# server, started here with PHP's own web server on a free port with an empty data folder.
#
#   test/friends-client.sh [build folder]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$root/build}
test_bin="$build/gui/friends-client-test"
[ -x "$test_bin" ] || { echo "no $test_bin: build the friends-client-test target first"; exit 2; }
command -v php >/dev/null || { echo "php is needed"; exit 2; }

work=$(mktemp -d)
trap 'kill "$php_pid" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM
mkdir -p "$work/www" "$work/data"
cp "$root/server/friends/psrpt_friends.php" "$work/www/"
port=$(php -r '$s = stream_socket_server("tcp://127.0.0.1:0"); echo explode(":", stream_socket_get_name($s, false))[1];')
# The server takes its settings from the environment too: an empty data folder of its own
PSRPT_FRIENDS_DIR="$work/data" php -S "127.0.0.1:$port" -t "$work/www" >"$work/php.log" 2>&1 &
php_pid=$!
sleep 0.5
# The same web server has the pages whose links get a preview (the test writes them there)
"$test_bin" "http://127.0.0.1:$port/psrpt_friends.php" "$work/www" || { echo "--- server log"; tail -50 "$work/php.log"; exit 1; }
