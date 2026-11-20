# Security Policy

## Supported versions

Security fixes go into the latest release of PSRemotePlayTogether. Please update to it before reporting an issue; the app tells you when a newer version is out.

## Reporting a vulnerability

Please don't report security issues in public issues or discussions.

Report them privately through GitHub instead: open the [Security tab](https://github.com/Crypto90/PSRemotePlayTogether/security) of this repository and choose **Report a vulnerability**. If that button is not there, open an issue that only asks for a private contact, without any details.

Include the affected version or commit, the steps or input that trigger the issue, and what you saw. A suggested fix is welcome.

You will get a reply in the advisory, we work on a fix together, and agree on when to publish the details once a release with the fix is out. Reporters are credited unless they prefer not to be.

## Where an issue belongs

PSRemotePlayTogether is built on [chiaki-ng](https://github.com/streetpea/chiaki-ng). The Remote Play protocol, console registration and the video and audio pipeline in `lib/` come from there; sharing a session with friends (`coplay/`, the invite, voice chat, webcams, the overlays) is this project's own. If an issue is in the part that comes from chiaki-ng, say so in the report: it then concerns their users too and is passed on.
