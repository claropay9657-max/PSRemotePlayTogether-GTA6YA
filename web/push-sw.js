// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Notifications from friends while the page is closed, and nothing else (docs/friends.md,
// "Web only"). The page has no service worker on purpose: it must be the newest every time it
// is opened, which the host's app counts on. This one only takes push messages and leaves
// loading the page alone: it has no fetch handler, keeps nothing in a cache and loads nothing.
// The page registers it only when the player switches notifications on in the friends settings
// (js/friends/push.js), and removes it again when they switch them off.
//
// The friends server's push is empty: it says only that something is new. So the notification
// says just that, with nothing of who or what; opening the page shows the rest.

const TITLE = 'PSRemotePlayTogether';
const TEXT = 'Something new from your friends';
const TAG = 'psrpt-friends';

/**
 * WebKit (Safari, a page on an iPhone's home screen) takes push messages away from a page that
 * does not show a notification for each one; the others need none while the page is in front.
 */
function mustShow(ua) {
  return /AppleWebKit/.test(ua) && !/Chrome|Chromium|Android/.test(ua);
}

async function onPush() {
  if (!mustShow(self.navigator ? self.navigator.userAgent : '')) {
    const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    // The page is open and in front: it fetches what is new itself
    if (windows.some((w) => w.focused && w.visibilityState === 'visible')) return;
  }
  await self.registration.showNotification(TITLE, {
    body: TEXT,
    tag: TAG,
    icon: 'img/icon-192.png',
    badge: 'img/icon-192.png',
  });
}

async function onClick() {
  const scope = self.registration.scope;
  const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
  const open = windows.find((w) => w.url.startsWith(scope));
  if (open) {
    // The page that is there already: in front, with the friends panel open
    await open.focus();
    open.postMessage({ type: 'psrpt-friends' });
    return;
  }
  await self.clients.openWindow(scope + '#friends');
}

self.addEventListener('push', (event) => {
  event.waitUntil(onPush());
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  event.waitUntil(onClick());
});
