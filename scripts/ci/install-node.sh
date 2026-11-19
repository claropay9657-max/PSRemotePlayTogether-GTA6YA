#!/bin/bash
# Installs Node.js into /usr/local when the build container has none.
#
# Forgejo's actions (here: the artifact upload) are JavaScript programs that
# the runner starts with the container's own `node`.
set -euo pipefail

NODE_VERSION=v22.23.5

if command -v node >/dev/null; then
	echo "Node.js $(node --version) is already installed"
	exit 0
fi

case "$(uname -m)" in
	x86_64) NODE_ARCH=x64; NODE_SHA256=cdeb8670a0c81f51b4dc4ac61919f9b1ad917b304bf8221eb939570663d240f8 ;;
	aarch64) NODE_ARCH=arm64; NODE_SHA256=2babb9eb5264860d540849f4b064a6fbdebcdc323657d9d382ce23d313fd5e09 ;;
	*) echo "error: no Node.js download for $(uname -m)" >&2; exit 1 ;;
esac

SUDO=""
if [ "$(id -u)" != 0 ]; then
	SUDO=sudo
fi

FILE="node-${NODE_VERSION}-linux-${NODE_ARCH}.tar.xz"
TMP="$(mktemp -d)"
# The x86_64 builder image has a curl from before --retry-all-errors existed (7.71)
RETRY_ALL=""
if curl --help all 2>/dev/null | grep -q -- '--retry-all-errors'; then
	RETRY_ALL=--retry-all-errors
fi
curl --fail --location --silent --show-error --retry 5 --retry-delay 3 $RETRY_ALL \
	-o "$TMP/$FILE" "https://nodejs.org/dist/${NODE_VERSION}/${FILE}"
echo "${NODE_SHA256}  $TMP/$FILE" | sha256sum -c -
$SUDO tar -xJf "$TMP/$FILE" -C /usr/local --strip-components=1 --no-same-owner \
	"node-${NODE_VERSION}-linux-${NODE_ARCH}/bin/node"
rm -rf "$TMP"
echo "Installed Node.js $(node --version)"
