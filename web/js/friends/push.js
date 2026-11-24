// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Notifications from friends while the page is closed (docs/friends.md, "Web only"): the
// browser's push messages, which only come to a page with a service worker. The one this page
// registers (../../push-sw.js) does only that and nothing for loading the page, and only once
// the player switches notifications on in the friends settings: the browser asks for the
// permission then, not before. Switching them off takes the subscription and the worker away.
//
// The friends server sends an empty push, signed with its own key (VAPID): the browser's push
// service learns that something is new and nothing else.

import { pushSupport } from './view.js';

const SCOPE = './';

/** What this browser can do: "ok", "home" (an iPhone or iPad: from the home screen only) or "none". */
export function support() {
  const nav = globalThis.navigator || {};
  return pushSupport({
    ua: nav.userAgent || '',
    platform: nav.platform || '',
    touchPoints: nav.maxTouchPoints || 0,
    standalone: nav.standalone === true
      || (typeof matchMedia === 'function' && ['standalone', 'fullscreen'].some((m) => matchMedia(`(display-mode: ${m})`).matches)),
    serviceWorker: 'serviceWorker' in nav,
    push: typeof globalThis.PushManager === 'function',
    notification: typeof globalThis.Notification === 'function',
  });
}

/** "granted", "denied" or "default" ("" where there are no notifications). */
export function permission() {
  return typeof globalThis.Notification === 'function' ? Notification.permission : '';
}

async function registration() {
  if (support() !== 'ok') return null;
  try {
    return (await navigator.serviceWorker.getRegistration(SCOPE)) || null;
  } catch (e) {
    return null;
  }
}

/** The subscription there is, or null. */
async function subscription() {
  const reg = await registration();
  if (!reg || !reg.pushManager) return null;
  try {
    return await reg.pushManager.getSubscription();
  } catch (e) {
    return null;
  }
}

/** Whether notifications are on in this browser: permitted, and subscribed. */
export async function isOn() {
  return permission() === 'granted' && !!(await subscription());
}

/** A worker that is running: subscribing needs one. */
function active(reg) {
  if (reg.active) return Promise.resolve(reg.active);
  return new Promise((resolve, reject) => {
    const worker = reg.installing || reg.waiting;
    if (!worker) {
      reject(new Error('no service worker'));
      return;
    }
    worker.addEventListener('statechange', () => {
      if (worker.state === 'activated') resolve(worker);
      else if (worker.state === 'redundant') reject(new Error('the service worker did not start'));
    });
  });
}

function sameKey(a, b) {
  if (!a || !b) return false;
  const x = new Uint8Array(a);
  const y = new Uint8Array(b);
  return x.length === y.length && x.every((v, i) => v === y[i]);
}

function keysOf(sub) {
  const json = sub.toJSON();
  return { endpoint: json.endpoint, p256dh: (json.keys && json.keys.p256dh) || '', auth: (json.keys && json.keys.auth) || '' };
}

/**
 * Switches notifications on: the permission (the browser asks now), the worker, the
 * subscription with the friends server's key, and the server told where to push. Resolves to
 * an empty string, or why it did not work.
 */
export async function switchOn(client) {
  const can = support();
  if (can === 'home') return 'On an iPhone or iPad, add this page to your home screen first: Share, then Add to Home Screen (iOS 16.4 or later).';
  if (can !== 'ok') return 'This browser cannot show notifications while the page is closed.';
  let asked = permission();
  if (asked !== 'granted') {
    try {
      asked = await Notification.requestPermission();
    } catch (e) {
      asked = 'denied';
    }
  }
  if (asked !== 'granted') return 'Notifications are blocked for this page. They can be allowed in the browser\'s settings for the site.';
  const key = await client.pushKey();
  if (!key) return 'The friends server cannot be reached right now.';
  let reg;
  let sub = null;
  try {
    reg = await navigator.serviceWorker.register('push-sw.js', { scope: SCOPE });
    await active(reg);
    sub = await reg.pushManager.getSubscription();
    // Made for another friends server: its key is another
    if (sub && !sameKey(sub.options && sub.options.applicationServerKey, key)) {
      await sub.unsubscribe();
      sub = null;
    }
    if (!sub) sub = await reg.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: key });
  } catch (e) {
    if (reg) await reg.unregister().catch(() => {});
    return 'This browser did not give the page a way to receive notifications.';
  }
  if (!(await client.setPush(keysOf(sub)))) {
    await sub.unsubscribe().catch(() => {});
    await reg.unregister().catch(() => {});
    return 'The friends server did not take it. Try again later.';
  }
  return '';
}

/** Switches notifications off: the server forgets where to push, the subscription and the worker go. */
export async function switchOff(client) {
  await client.setPush(null);
  const reg = await registration();
  if (!reg) return;
  try {
    const sub = await reg.pushManager.getSubscription();
    if (sub) await sub.unsubscribe();
  } catch (e) {
    // gone with the worker
  }
  await reg.unregister().catch(() => {});
}

/**
 * At the start, while notifications are on: the server hears again where to push (it may
 * have dropped the subscription, or the friends server changed and wants another one).
 */
export async function refresh(client) {
  if (!client.enabled || permission() !== 'granted') return;
  const sub = await subscription();
  if (!sub) return;
  const key = await client.pushKey();
  if (key && !sameKey(sub.options && sub.options.applicationServerKey, key)) {
    await switchOn(client);
    return;
  }
  await client.setPush(keysOf(sub));
}

/** A click on a notification while the page is open: `onOpen` shows the friends. */
export function listen(onOpen) {
  const container = globalThis.navigator && navigator.serviceWorker;
  if (!container) return;
  container.addEventListener('message', (e) => {
    if (e.data && e.data.type === 'psrpt-friends') onOpen();
  });
  container.startMessages();
}
