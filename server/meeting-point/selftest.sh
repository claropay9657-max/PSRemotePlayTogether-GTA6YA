#!/bin/bash
# Checks psrpt_meet.php against the rules the app relies on.
#
#   server/meeting-point/selftest.sh                  starts PHP's built-in server and tests the file next to this script
#   server/meeting-point/selftest.sh https://...      tests a meeting point that is already installed
#
# Needs curl, and php for the first form. Leaves nothing behind on an installed
# meeting point but notes in rooms with random names, which expire in half a minute.

set -u
here="$(cd "$(dirname "$0")" && pwd)"
url="${1:-}"
server_pid=""
store=""

cleanup() {
	[ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null
	[ -n "$store" ] && rm -rf "$store"
}
trap cleanup EXIT

if [ -z "$url" ]; then
	port=$((20000 + RANDOM % 20000))
	store="$(mktemp -d)"
	PSRPT_MEET_DIR="$store" php -S "127.0.0.1:$port" -t "$here" >/dev/null 2>&1 &
	server_pid=$!
	url="http://127.0.0.1:$port/psrpt_meet.php"
	for _ in $(seq 1 50); do
		curl -s -o /dev/null "$url" && break
		sleep 0.1
	done
fi

failed=0
checks=0
check() { # description, expected, actual
	checks=$((checks + 1))
	if [ "$2" != "$3" ]; then
		failed=$((failed + 1))
		printf 'FAILED: %s\n  expected: %s\n  got:      %s\n' "$1" "$2" "$3"
	fi
}

# What an app would send. Prints the status and the answer's lines, joined by "|".
post() { # room id role note [version]
	curl -s -m 10 -w '\n%{http_code}' --data "v=${5:-1}&room=$1&id=$2&role=$3&note=$4" "$url" \
		| awk 'NF { if (n++) printf "|"; printf "%s", $0 }'
}
room() { head -c 16 /dev/urandom | base64 | tr '+/' '-_' | tr -d '=\n'; }

a="$(room)"
b="$(room)"
host=00000000000000a1
guest=00000000000000b1
guest2=00000000000000b2

# Somebody looking with a browser gets a friendly 200, not an error a proxy would count
check "GET is answered with 200" "200" "$(curl -s -m 10 -o /dev/null -w '%{http_code}' "$url")"
check "GET says what this is" "psrpt-meet 1" "$(curl -s -m 10 "$url" | head -1)"

# The host leaves its note; nobody else is there yet
check "host alone" "psrpt-meet 1|poll 3000|200" "$(post "$a" $host h HOSTNOTE1)"
# The guest finds it, and the host finds the guest's
check "guest sees the host" "psrpt-meet 1|poll 3000|note HOSTNOTE1|200" "$(post "$a" $guest g GUESTNOTE1)"
check "host sees the guest" "psrpt-meet 1|poll 3000|note GUESTNOTE1|200" "$(post "$a" $host h HOSTNOTE1)"
# Guests do not get each other's notes
check "guests do not see each other" "psrpt-meet 1|poll 3000|note HOSTNOTE1|200" "$(post "$a" $guest2 g GUESTNOTE2)"
check "host sees both guests" "psrpt-meet 1|poll 3000|note GUESTNOTE1|note GUESTNOTE2|200" "$(post "$a" $host h HOSTNOTE1)"
# A new note replaces the sender's old one
check "a note is replaced" "psrpt-meet 1|poll 3000|note HOSTNOTE2|200" "$(post "$a" $host h HOSTNOTE2 >/dev/null; post "$a" $guest g GUESTNOTE1)"
# An empty note removes it
check "a note is removed" "psrpt-meet 1|poll 3000|note GUESTNOTE2|200" "$(post "$a" $guest g '' >/dev/null; post "$a" $host h HOSTNOTE2)"
# Another room knows nothing of this
check "rooms are apart" "psrpt-meet 1|poll 3000|200" "$(post "$b" $guest g GUESTNOTE1)"
check "an empty room stays empty" "psrpt-meet 1|poll 3000|200" "$(post "$(room)" $guest g '')"

# Never more than ten guests' notes in a room: the oldest go
c="$(room)"
for i in 01 02 03 04 05 06 07 08 09 10 11 12; do post "$c" "00000000000000$i" g "G$i" >/dev/null; done
check "at most ten guests per room" "10" "$(post "$c" $host h H | tr '|' '\n' | grep -c '^note ')"
check "the oldest guests made room" "0" "$(post "$c" $host h H | tr '|' '\n' | grep -c -e '^note G01$' -e '^note G02$')"

# What is not a request of the app
check "a room that is too short" "psrpt-meet 1|error malformed request|400" "$(post short $host h X)"
check "an id that is no hex number" "psrpt-meet 1|error malformed request|400" "$(post "$a" 'zzzzzzzzzzzzzzzz' h X)"
check "a role that does not exist" "psrpt-meet 1|error malformed request|400" "$(post "$a" $host x X)"
check "a note with other characters" "psrpt-meet 1|error malformed request|400" "$(post "$a" $host h 'a.b')"
check "a note that is too long" "psrpt-meet 1|error malformed request|400" "$(post "$a" $host h "$(head -c 1025 /dev/zero | tr '\0' 'A')")"
check "the longest note is fine" "200" "$(post "$b" $host h "$(head -c 1024 /dev/zero | tr '\0' 'A')" | tr '|' '\n' | tail -1)"
check "a room name cannot leave the folder" "psrpt-meet 1|error malformed request|400" "$(post '../../../../etc/passwd/x' $host h X)"
check "a later version is told so, without an error status" "200" "$(post "$a" $host h X 2 | tr '|' '\n' | tail -1)"
check "a later version gets an error line" "1" "$(post "$a" $host h X 2 | tr '|' '\n' | grep -c '^error ')"

if [ -n "$store" ]; then
	# Nothing of what was sent became a file name, and nothing lies outside the folder
	check "files are named after a hash" "0" "$(ls "$store" | grep -vc -E '^[0-9a-f]{64}\.room$')"
	# Notes expire: with a lifetime of 12 s (the shortest, four visits of a host) a
	# note that is 20 s old is gone. The file's content is aged by hand to avoid waiting.
	d="$(room)"
	post "$d" $host h OLDNOTE >/dev/null
	file="$(grep -l OLDNOTE "$store"/*.room)"
	php -r '$f = $argv[1]; $e = json_decode(file_get_contents($f), true); $e[0]["t"] -= 60; file_put_contents($f, json_encode($e));' "$file"
	check "an old note is forgotten" "psrpt-meet 1|poll 3000|200" "$(post "$d" $guest g GUESTNOTE1)"
fi

echo "$checks checks, $failed failed ($url)"
[ "$failed" -eq 0 ]
