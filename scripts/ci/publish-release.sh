#!/bin/bash
# Attaches files to the Forgejo release of a tag, creating the release if needed.
#
#   scripts/ci/publish-release.sh FILE...
#
# Environment:
#   FORGEJO_TOKEN       access token (in CI: the job's automatic token)
#   GITHUB_SERVER_URL   e.g. https://forgejo.example.org     (set by Forgejo Actions)
#   GITHUB_REPOSITORY   owner/name                           (set by Forgejo Actions)
#   GITHUB_REF_NAME     the tag, e.g. v1.2.3                 (set by Forgejo Actions)
#   RELEASE_TAG         overrides GITHUB_REF_NAME
#
# Several jobs call this for the same tag at the same time; whoever is first
# creates the release, the others find it. A file that is already attached is
# replaced, so a job can be re-run.
set -euo pipefail

TAG="${RELEASE_TAG:-${GITHUB_REF_NAME:-}}"
SERVER="${GITHUB_SERVER_URL:-}"
REPO="${GITHUB_REPOSITORY:-}"
TOKEN="${FORGEJO_TOKEN:-${GITHUB_TOKEN:-}}"

for name in TAG SERVER REPO TOKEN; do
	if [ -z "${!name}" ]; then
		echo "error: $name is not set (see the top of $0)" >&2
		exit 1
	fi
done
if [ "$#" -eq 0 ]; then
	echo "usage: $0 FILE..." >&2
	exit 1
fi
for f in "$@"; do
	[ -f "$f" ] || { echo "error: $f does not exist" >&2; exit 1; }
done

API="${SERVER%/}/api/v1/repos/$REPO"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# api METHOD PATH [curl args...]: body goes to $TMP/body, prints the HTTP status
api() {
	local method="$1" path="$2"
	shift 2
	curl --silent --show-error --retry 3 --retry-delay 2 \
		-X "$method" -H "Authorization: token $TOKEN" -H "Accept: application/json" \
		-o "$TMP/body" -w '%{http_code}' "$@" "$API$path"
}

json() { # json EXPRESSION: evaluates a Python expression on the last response (as d)
	python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$TMP/body" "$1"
}

release_id=""
for attempt in 1 2 3 4 5; do
	status="$(api GET "/releases/tags/$TAG")"
	if [ "$status" = 200 ]; then
		release_id="$(json 'd["id"]')"
		break
	fi
	if [ "$status" != 404 ]; then
		echo "error: looking up the release of $TAG failed with HTTP $status" >&2
		cat "$TMP/body" >&2 || true
		exit 1
	fi
	# A version like 1.2.3-rc1 is a pre-release
	prerelease=false
	case "$TAG" in *-*) prerelease=true ;; esac
	python3 -c 'import json,sys; print(json.dumps({"tag_name": sys.argv[1], "name": "PSRemotePlayTogether " + sys.argv[1].lstrip("v"), "body": sys.argv[2], "prerelease": sys.argv[3] == "true"}))' \
		"$TAG" "Built from commit ${GITHUB_SHA:-unknown}." "$prerelease" > "$TMP/request"
	status="$(api POST "/releases" -H "Content-Type: application/json" --data-binary "@$TMP/request")"
	if [ "$status" = 201 ]; then
		release_id="$(json 'd["id"]')"
		echo "Created the release for $TAG"
		break
	fi
	# Most likely another job created it in the meantime: look again
	echo "note: creating the release returned HTTP $status, retrying (attempt $attempt)" >&2
	sleep $((attempt * 2))
done
if [ -z "$release_id" ]; then
	echo "error: could not find or create the release for $TAG" >&2
	cat "$TMP/body" >&2 || true
	exit 1
fi

for f in "$@"; do
	name="$(basename "$f")"
	status="$(api GET "/releases/$release_id/assets")"
	if [ "$status" = 200 ]; then
		for old in $(python3 -c 'import json,sys; print(" ".join(str(a["id"]) for a in json.load(open(sys.argv[1])) if a["name"] == sys.argv[2]))' "$TMP/body" "$name"); do
			api DELETE "/releases/$release_id/assets/$old" >/dev/null
			echo "Replacing the existing $name"
		done
	fi
	encoded="$(python3 -c 'import sys,urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$name")"
	status="$(api POST "/releases/$release_id/assets?name=$encoded" --form "attachment=@$f")"
	if [ "$status" != 201 ]; then
		echo "error: uploading $name failed with HTTP $status" >&2
		cat "$TMP/body" >&2 || true
		exit 1
	fi
	echo "Attached $name"
done

echo "Release: ${SERVER%/}/$REPO/releases/tag/$TAG"
