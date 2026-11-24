// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// What the page's friends panel shows (web/js/friends/view.js), worked out without a browser:
// the same rules and words as the app's FriendsPanel.qml, InviteCard.qml and its Friends button.
// Run: node --experimental-default-type=module --test test/web/*.test.mjs

import { test } from 'node:test';
import assert from 'node:assert/strict';

import * as V from '../../web/js/friends/view.js';

test('the Friends button: what is new, who is online, its words', () => {
  const fr = {
    enabled: true,
    unread: 3,
    requests: [{ incoming: true }, { incoming: false }, { incoming: true }],
    friends: [{ state: 'online' }, { state: 'offline' }, { state: 'web' }, { state: 'busy' }],
  };
  assert.equal(V.incomingCount(fr.requests), 2);
  assert.equal(V.newsCount(fr), 5);
  assert.equal(V.onlineCount(fr), 3);
  // Switched off: nothing to show
  assert.equal(V.newsCount({ ...fr, enabled: false }), 0);
  assert.equal(V.onlineCount({ ...fr, enabled: false }), 0);
  assert.equal(V.newsCount(null), 0);
  assert.equal(V.badgeText(7), '7');
  assert.equal(V.badgeText(99), '99');
  assert.equal(V.badgeText(100), '99+');
  assert.equal(V.buttonTip(2, 5), 'Friends: 2 new');
  assert.equal(V.buttonTip(0, 1), 'Friends: 1 online');
  assert.equal(V.buttonTip(0, 0), 'Friends');
  assert.deepEqual(V.tabLabels(1, 4), ['Friends (1 new)', 'Chats (4)']);
  assert.deepEqual(V.tabLabels(0, 0), ['Friends', 'Chats']);
});

test('a friend\'s line: what they do says the most', () => {
  assert.equal(V.stateText(null), 'Offline');
  assert.equal(V.stateText({ state: 'offline', activity: 'Playing' }), 'Offline');
  assert.equal(V.stateText({ state: 'online', activity: 'Playing Astro Bot on PS5' }), 'Playing Astro Bot on PS5');
  assert.equal(V.stateText({ state: 'online', room: { title: 'Friday racing' } }), 'Hosting Friday racing');
  assert.equal(V.stateText({ state: 'away' }), 'Away');
  assert.equal(V.stateText({ state: 'busy' }), 'Busy');
  assert.equal(V.stateText({ state: 'web' }), 'Online in a web browser');
  assert.equal(V.stateText({ state: 'online' }), 'Online');
  assert.deepEqual(['online', 'web', 'away', 'busy', 'offline', ''].map(V.stateTone), ['good', 'good', 'wait', 'bad', 'off', 'off']);
  assert.equal(V.ownPresence('invisible'), 'offline');
  assert.equal(V.ownPresence('busy'), 'busy');
  assert.equal(V.initialOf(' mara'), 'M');
  assert.equal(V.initialOf(''), '?');
  assert.equal(V.initialOf(null), '?');
});

test('requests and times, as the lists and the chat say them', () => {
  assert.equal(V.requestLine({ incoming: true, text: 'Hi, Kim here' }), '“Hi, Kim here”');
  assert.equal(V.requestLine({ incoming: true, text: '' }), 'wants to be your friend');
  assert.equal(V.requestLine({ incoming: false, unsent: true }), 'Not sent: the friends server did not take it');
  assert.equal(V.requestLine({ incoming: false }), 'Waiting for an answer');

  const now = new Date(2026, 10, 23, 21, 30).getTime();
  assert.equal(V.timeText(new Date(2026, 10, 23, 9, 5).getTime(), now), '09:05');
  assert.equal(V.timeText(new Date(2026, 10, 3, 21, 7).getTime(), now), '3 Nov 21:07');
  assert.equal(V.timeText(new Date(2025, 0, 1, 0, 0).getTime(), now), '1 Jan 00:00');
  assert.equal(V.messageTime({ state: 'failed', at: now }, now), 'Not sent');
  assert.equal(V.messageTime({ state: 'sending', at: now }, now), 'Sending…');
  assert.equal(V.messageTime({ state: 'sent', at: now }, now), '21:30');
  assert.equal(V.messageTime({ at: now }, now), '21:30');
});

test('an invite in a chat: open now, probably over, or not known', () => {
  const now = 1_000_000_000_000;
  const card = { invite: 'psrpt://join/abc' };
  const theirs = { join: card, mine: false, at: now - 60000 };
  const hosting = { fp: 'f', room: { invite: 'psrpt://join/abc' } };
  // Their status carries the same invite
  assert.equal(V.liveFor(theirs, hosting, '', now), 'open');
  // Another session, or none: not known while it is young, probably over when it is old
  assert.equal(V.liveFor(theirs, { fp: 'f', room: { invite: 'psrpt://join/other' } }, '', now), '');
  assert.equal(V.liveFor(theirs, { fp: 'f', room: null }, '', now), '');
  assert.equal(V.liveFor({ ...theirs, at: now - V.INVITE_OLD_MS - 1 }, null, '', now), 'old');
  assert.equal(V.liveFor({ ...theirs, at: now - V.INVITE_OLD_MS }, null, '', now), '');
  // An own invite is open while this device hosts it: a browser never does
  const mine = { join: card, mine: true, at: now - 60000 };
  assert.equal(V.liveFor(mine, hosting, '', now), '');
  assert.equal(V.liveFor(mine, null, 'psrpt://join/abc', now), 'open');
  // No invite: nothing to say
  assert.equal(V.liveFor({ text: 'hi', at: 0 }, hosting, '', now), '');
});

test('the places of a session and the invite card\'s words', () => {
  assert.deepEqual(V.places({ players: 2, max: 4 }), { places: 4, taken: 2 });
  assert.deepEqual(V.places({ players: 9, max: 4 }), { places: 4, taken: 4 });
  assert.deepEqual(V.places({ players: 3, max: 40 }), { places: 16, taken: 3 });
  assert.deepEqual(V.places({}), { places: 0, taken: 0 });
  assert.deepEqual(V.places(null), { places: 0, taken: 0 });
  assert.equal(V.placesText(2, 4), '2 / 4 · 2 free');
  assert.equal(V.placesText(1, 3), '1 / 3 · 2 free');
  assert.equal(V.placesText(4, 4), 'Full: 4 / 4');

  assert.deepEqual(['ps5', 'ps4', 'pc', 'xbox', 'wii', undefined].map(V.consoleName), ['PS5', 'PS4', 'PC', 'Xbox', '', '']);
  assert.equal(V.controlText({ control: 'all' }), 'Everybody plays together');
  assert.equal(V.controlText({ control: 'turns' }), 'One plays at a time');
  assert.equal(V.controlText({ control: 'host' }), 'Only the host plays');
  assert.equal(V.controlText({ control: 'each' }), 'Each their own controller');
  assert.equal(V.controlText({ control: 'all', talk: true }), '');
  assert.equal(V.controlText({}), '');
  assert.equal(V.doorText({ policy: 'ask' }), 'Ask to join');
  assert.equal(V.doorText({ policy: 'ask', talk: true }), 'Ask to join');
  assert.equal(V.doorText({ policy: 'open', talk: true }), 'Join and talk');
  assert.equal(V.doorText({ policy: 'watch' }), 'Join as viewer');
  assert.equal(V.doorText({ policy: 'open' }), 'Join and play');
  assert.equal(V.doorText({}), '');
  assert.equal(V.inviteLine({ talk: true, game: 'X' }), 'Just talking, no game');
  assert.equal(V.inviteLine({ game: 'Night Circuit 9', host: 'Jonas' }), 'Night Circuit 9');
  assert.equal(V.inviteLine({ host: 'Jonas' }), 'Hosted by Jonas');
  assert.equal(V.inviteLine({}), '');
  assert.equal(V.inviteWho(false, 'Jonas'), 'Jonas invites you');
  assert.equal(V.inviteWho(true, 'Lea'), 'You invited Lea');
  assert.equal(V.inviteInitial({ game: 'night circuit', title: 'Friday' }), 'N');
  assert.equal(V.inviteInitial({ title: 'Friday' }), 'F');
  assert.equal(V.inviteInitial({}), '?');
});

test('a browser in a friend\'s session: its status, and the session for the join flow', () => {
  assert.equal(V.guestActivity('Astro Bot'), 'Playing Astro Bot with friends');
  assert.equal(V.guestActivity(' '), 'In a session with friends');
  assert.equal(V.guestActivity(undefined), 'In a session with friends');
  const room = V.roomOfCard({ invite: 'psrpt://join/x', web: 'https://page/#x', title: 'Friday racing', game: 'Night Circuit 9',
    console: 'ps5', policy: 'ask', control: 'all', players: 2, max: 4 }, 'Jonas');
  assert.deepEqual(room, { invite: 'psrpt://join/x', title: 'Friday racing', host: 'Jonas', game: 'Night Circuit 9', cover: '',
    console: 'ps5', control: 'all', policy: 'ask', web: true, talk: false, players: 2, max: 4 });
  // What the card says wins over the name in the list; a session without a game has no way of playing
  const talk = V.roomOfCard({ invite: 'psrpt://join/y', host: 'Lea H.', talk: true, control: 'all' }, 'Lea');
  assert.equal(talk.host, 'Lea H.');
  assert.equal(talk.title, 'A session');
  assert.equal(talk.control, '');
  assert.equal(talk.talk, true);
  assert.equal(V.roomOfCard({ invite: 'psrpt://join/z' }, '').host, 'A friend');
});

test('the backup: its file name and its passphrase', () => {
  assert.equal(V.backupFileName(new Date(2026, 10, 3)), 'PSRemotePlayTogether friends 2026-11-03.psrpt-backup');
  assert.equal(V.passphraseProblem('save', 'short', 'short'), 'The passphrase needs at least 8 characters.');
  assert.equal(V.passphraseProblem('save', 'long enough', 'long enougH'), 'The two passphrases are not the same.');
  assert.equal(V.passphraseProblem('save', 'long enough', 'long enough'), '');
  // Opening asks once
  assert.equal(V.passphraseProblem('restore', 'long enough', ''), '');
  assert.equal(V.passphraseProblem('restore', '1234567', ''), 'The passphrase needs at least 8 characters.');
});

test('notifications while the page is closed: where a browser can have them', () => {
  const all = { serviceWorker: true, push: true, notification: true };
  const chrome = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36';
  const iphone = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Mobile/15E148 Safari/604.1';
  const mac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Safari/605.1.15';
  assert.equal(V.pushSupport({ ua: chrome, platform: 'Win32', ...all }), 'ok');
  assert.equal(V.pushSupport({ ua: chrome, platform: 'Win32', serviceWorker: true, push: false, notification: true }), 'none');
  // An iPhone or iPad: only for the page on the home screen
  assert.equal(V.pushSupport({ ua: iphone, platform: 'iPhone', ...all }), 'home');
  assert.equal(V.pushSupport({ ua: iphone, platform: 'iPhone', standalone: true, ...all }), 'ok');
  assert.equal(V.pushSupport({ ua: iphone, platform: 'iPhone', standalone: true }), 'none');
  // An iPad says it is a Mac, but has a touch screen
  assert.equal(V.pushSupport({ ua: mac, platform: 'MacIntel', touchPoints: 5, ...all }), 'home');
  assert.equal(V.pushSupport({ ua: mac, platform: 'MacIntel', touchPoints: 0, ...all }), 'ok');
});
