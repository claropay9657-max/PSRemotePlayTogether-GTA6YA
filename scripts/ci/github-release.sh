#!/bin/bash
# Publishes the GitHub release of a tag with the given files, or adds the files
# to it if it already exists (so that the job can be run again).
#
#   scripts/ci/github-release.sh FILE...
#
# Environment (all set by GitHub Actions):
#   GH_TOKEN            token that may write to the repository's contents
#   GITHUB_REPOSITORY   owner/name
#   GITHUB_REF_NAME     the tag, e.g. v1.2.3 (RELEASE_TAG overrides it)
#
# A tag with a hyphen, such as v1.2.3-rc1, becomes a pre-release.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

TAG="${RELEASE_TAG:-${GITHUB_REF_NAME:-}}"
if [ -z "$TAG" ] || [ "$#" -eq 0 ]; then
	echo "usage: RELEASE_TAG=v1.2.3 $0 FILE..." >&2
	exit 1
fi
VERSION="${TAG#v}"

NOTES="$(mktemp)"
scripts/ci/release-notes.sh "$VERSION" "$@" > "$NOTES"
echo "== Release notes"
cat "$NOTES"

if gh release view "$TAG" >/dev/null 2>&1; then
	echo "== Adding the files to the existing release $TAG"
	gh release upload "$TAG" "$@" --clobber
	gh release edit "$TAG" --notes-file "$NOTES"
else
	echo "== Creating the release $TAG"
	FLAGS=()
	case "$VERSION" in *-*) FLAGS+=(--prerelease) ;; esac
	gh release create "$TAG" "$@" --verify-tag --title "PSRemotePlayTogether $VERSION" --notes-file "$NOTES" "${FLAGS[@]}"
fi
