#!/bin/bash
# Prints the version used in release file names.
#   tag v1.2.3            -> 1.2.3
#   anything else         -> dev-<short commit>
# Works inside Forgejo Actions (GITHUB_* variables) and in a plain checkout.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

if [ -n "${PSRPT_VERSION:-}" ]; then
	echo "$PSRPT_VERSION"
	exit 0
fi

ref_type="${GITHUB_REF_TYPE:-}"
ref_name="${GITHUB_REF_NAME:-}"
if [ -z "$ref_type" ] && [[ "${GITHUB_REF:-}" == refs/tags/* ]]; then
	ref_type=tag
	ref_name="${GITHUB_REF#refs/tags/}"
fi
# What the app itself shows: PSRPT_VERSION_* in the top-level CMakeLists.txt
app_version() {
	local part out=""
	for part in MAJOR MINOR PATCH; do
		out="$out.$(sed -n "s/^set(PSRPT_VERSION_$part \\([0-9][0-9]*\\)).*/\\1/p" CMakeLists.txt)"
	done
	echo "${out#.}"
}

# A release must not say one version in its file names and another in the app
check_tag() {
	local version="$1" base="${1%%-*}"
	if [ "$base" != "$(app_version)" ]; then
		echo "error: the tag says version $version, but the app is version $(app_version)." >&2
		echo "Set PSRPT_VERSION_* in CMakeLists.txt to match, commit, and tag that commit." >&2
		exit 1
	fi
}

if [ "$ref_type" = tag ] && [[ "$ref_name" == v[0-9]* ]]; then
	check_tag "${ref_name#v}"
	echo "${ref_name#v}"
	exit 0
fi

sha="${GITHUB_SHA:-}"
if [ -z "$sha" ] && command -v git >/dev/null; then
	# A plain checkout (the macOS release is built by hand): a v* tag on this commit counts
	tag="$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null || true)"
	if [ -n "$tag" ]; then
		check_tag "${tag#v}"
		echo "${tag#v}"
		exit 0
	fi
	sha="$(git rev-parse HEAD 2>/dev/null || true)"
fi
if [ -n "$sha" ]; then
	echo "dev-${sha:0:7}"
else
	echo "dev"
fi
