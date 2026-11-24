// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// What the page's friends panel (./panel.js) shows, worked out without a browser: the counts on
// the Friends button, a friend's line, the times, whether an invite in a chat is open, the places
// of a session, the words of the invite card. The same rules and the same English words as the
// app's gui/src/qml/FriendsPanel.qml, InviteCard.qml and MainView.qml (its Friends button).

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/** Sessions rarely last longer: an invite older than this is "Probably over" unless it is known to be open. */
export const INVITE_OLD_MS = 6 * 3600 * 1000;

/** Incoming friend requests among the requests. */
export function incomingCount(requests) {
  return (requests || []).filter((r) => r && r.incoming).length;
}

/** What is new for the Friends button: unread messages and requests to answer; 0 while friends are off. */
export function newsCount(fr) {
  if (!fr || !fr.enabled) return 0;
  return (fr.unread || 0) + incomingCount(fr.requests);
}

/** Friends who are not offline; 0 while friends are off. */
export function onlineCount(fr) {
  if (!fr || !fr.enabled) return 0;
  return (fr.friends || []).filter((f) => f && f.state !== 'offline').length;
}

/** The number on the button: up to 99. */
export function badgeText(n) {
  return n > 99 ? '99+' : String(n);
}

/** What the button says on hover and to screen readers. */
export function buttonTip(news, online) {
  if (news > 0) return `Friends: ${news} new`;
  if (online > 0) return `Friends: ${online} online`;
  return 'Friends';
}

/** The two tabs of the lists, with what is new on each. */
export function tabLabels(incoming, unread) {
  return [incoming > 0 ? `Friends (${incoming} new)` : 'Friends', unread > 0 ? `Chats (${unread})` : 'Chats'];
}

/** One line for what a friend does right now. */
export function stateText(f) {
  if (!f || f.state === 'offline') return 'Offline';
  // What their app says ("Hosting Friday racing (2 / 4 players)") says the most
  if (f.activity) return f.activity;
  if (f.room && f.room.title) return `Hosting ${f.room.title}`;
  if (f.state === 'away') return 'Away';
  if (f.state === 'busy') return 'Busy';
  if (f.state === 'web') return 'Online in a web browser';
  return 'Online';
}

/** The colour of a status dot, as a class: good (online, in a browser), wait (away), bad (busy), off. */
export function stateTone(state) {
  if (state === 'online' || state === 'web') return 'good';
  if (state === 'away') return 'wait';
  if (state === 'busy') return 'bad';
  return 'off';
}

/** The own dot: invisible looks offline. */
export function ownPresence(myState) {
  return myState === 'invisible' ? 'offline' : myState;
}

/** The first letter of a name, for a picture there is none of. */
export function initialOf(name) {
  return (String(name || '?').trim().charAt(0) || '?').toUpperCase();
}

const two = (n) => String(n).padStart(2, '0');

/** "21:07" today, "3 Nov 21:07" on another day. */
export function timeText(ms, now = Date.now()) {
  const d = new Date(ms);
  const today = new Date(now);
  const hm = `${two(d.getHours())}:${two(d.getMinutes())}`;
  if (d.toDateString() === today.toDateString()) return hm;
  return `${d.getDate()} ${MONTHS[d.getMonth()]} ${hm}`;
}

/** What stands under a message instead of its time while it is on its way, or when it did not go. */
export function messageTime(m, now = Date.now()) {
  if (m.state === 'failed') return 'Not sent';
  if (m.state === 'sending') return 'Sending…';
  return timeText(m.at, now);
}

/** The line under a request. */
export function requestLine(r) {
  if (r.incoming) return r.text ? `“${r.text}”` : 'wants to be your friend';
  return r.unsent ? 'Not sent: the friends server did not take it' : 'Waiting for an answer';
}

/**
 * Whether an invite in the chat leads to a session that is open now: "open", "old" (sent long
 * ago, likely over) or "" (not known). An own invite is open while this device hosts it (a
 * browser never does); a friend's while their status carries the same invite.
 */
export function liveFor(message, friend, hostedInvite, nowMs) {
  const card = message && message.join;
  if (!card) return '';
  const open = message.mine ? !!hostedInvite && hostedInvite === card.invite
    : !!friend && !!friend.room && friend.room.invite === card.invite;
  if (open) return 'open';
  // Sessions rarely last longer; their status may just not show it (hidden, offline for us)
  return nowMs - (message.at || 0) > INVITE_OLD_MS ? 'old' : '';
}

/** The places of a session: how many in all (at most 16) and how many are taken. */
export function places(card) {
  const all = Math.max(0, Math.min(16, (card && card.max) || 0));
  const taken = Math.max(0, Math.min(all, (card && card.players) || 0));
  return { places: all, taken };
}

/** "2 / 4 · 2 free", or "Full: 4 / 4". */
export function placesText(taken, all) {
  return taken >= all ? `Full: ${taken} / ${all}` : `${taken} / ${all} · ${all - taken} free`;
}

export function consoleName(kind) {
  return { ps5: 'PS5', ps4: 'PS4', pc: 'PC', xbox: 'Xbox' }[kind] || '';
}

/** How the session is played (the cyan badge); empty for a session without a game. */
export function controlText(card) {
  if (!card || card.talk || !card.control) return '';
  return { turns: 'One plays at a time', host: 'Only the host plays', each: 'Each their own controller' }[card.control]
    || 'Everybody plays together';
}

/** How one gets in (the purple badge). */
export function doorText(card) {
  if (!card || !card.policy) return '';
  if (card.policy === 'ask') return 'Ask to join';
  if (card.talk) return 'Join and talk';
  return card.policy === 'watch' ? 'Join as viewer' : 'Join and play';
}

/** The line under the invite's title. */
export function inviteLine(card) {
  if (!card) return '';
  if (card.talk) return 'Just talking, no game';
  if (card.game) return card.game;
  return card.host ? `Hosted by ${card.host}` : '';
}

/** Who sent it, or to whom it went. */
export function inviteWho(mine, person) {
  return mine ? `You invited ${person}` : `${person} invites you`;
}

/** The letter on the cover when there is no picture. */
export function inviteInitial(card) {
  return initialOf((card && (card.game || card.title)) || '?');
}

/**
 * What a browser that is in a session tells its friends, as the app does for a guest
 * (gui/src/qmlbackend_display.cpp): the game if it is known.
 */
export function guestActivity(game) {
  const g = String(game || '').trim();
  return g ? `Playing ${g} with friends` : 'In a session with friends';
}

/**
 * A friend's session as the page's join flow and its list of sessions joined before take it:
 * the fields of a room in the public list that a join card has.
 */
export function roomOfCard(card, friendName = '') {
  return {
    invite: card.invite,
    title: card.title || 'A session',
    host: card.host || friendName || 'A friend',
    game: card.game || '',
    cover: card.cover || '',
    console: card.console || '',
    control: card.talk ? '' : card.control || '',
    policy: card.policy || '',
    web: !!card.web,
    talk: !!card.talk,
    players: card.players || 0,
    max: card.max || 0,
  };
}

/** The name of a backup file, as the app suggests it. */
export function backupFileName(date = new Date()) {
  return `PSRemotePlayTogether friends ${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}.psrpt-backup`;
}

/** What is wrong with a passphrase for saving ("save") or opening ("restore") a backup; empty if nothing. */
export function passphraseProblem(mode, one, two_) {
  if (String(one || '').length < 8) return 'The passphrase needs at least 8 characters.';
  if (mode === 'save' && one !== two_) return 'The two passphrases are not the same.';
  return '';
}

/**
 * What notifications while the page is closed need in this browser: "ok", "home" (an iPhone or
 * iPad: only for the page added to the home screen, iOS 16.4 and later) or "none".
 * @param {{ua?: string, platform?: string, touchPoints?: number, standalone?: boolean, serviceWorker?: boolean, push?: boolean, notification?: boolean}} env
 */
export function pushSupport(env) {
  const ua = env.ua || '';
  // iPadOS says it is a Mac; a Mac has no touch screen
  const apple = /iPad|iPhone|iPod/.test(ua) || (env.platform === 'MacIntel' && (env.touchPoints || 0) > 1);
  if (apple && !env.standalone) return 'home';
  return env.serviceWorker && env.push && env.notification ? 'ok' : 'none';
}

// ------------------------------------------------------------------ groups, plans, read receipts, devices

const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];

/** "3 members" under a group's name. */
export function membersText(n) {
  return n === 1 ? '1 member' : `${n} members`;
}

/** Under an own message: "Read" (a friend), "Read by 2" or "Read by everybody" (a group), or "" (not yet, or read receipts off). */
export function readText(m, members = 0) {
  if (m.read) return 'Read';
  if (m.readBy > 0) return members && m.readBy >= members - 1 ? 'Read by everybody' : `Read by ${m.readBy}`;
  return '';
}

/** Whether a message in a group shows its writer's name: the first of a run by somebody else. */
export function namedRun(messages, index, isGroup) {
  const m = messages[index];
  if (!isGroup || !m || m.mine || m.system) return false;
  const before = index > 0 ? messages[index - 1] : null;
  return !(before && before.from === m.from && !before.system);
}

/** "When: Racing?" or "When shall we play?" */
export function planTitle(plan) {
  return plan && plan.title ? `When: ${plan.title}?` : 'When shall we play?';
}

/** Per time of a plan: [yes, maybe] from everybody's answers ({fp: [0|1|2, ...]}). */
export function planTally(votes, count) {
  const out = [];
  for (let i = 0; i < count; i++) out.push([0, 0]);
  for (const answers of Object.values(votes || {})) {
    for (let i = 0; i < count; i++) {
      const a = Array.isArray(answers) ? answers[i] : 0;
      if (a === 2) out[i][0]++;
      else if (a === 1) out[i][1]++;
    }
  }
  return out;
}

/** The time most can make: most yes, then most maybe; -1 while nobody said yes or maybe. */
export function planBest(tally) {
  let best = -1;
  for (let i = 0; i < tally.length; i++) {
    if (tally[i][0] + tally[i][1] === 0) continue;
    if (best < 0 || tally[i][0] > tally[best][0] || (tally[i][0] === tally[best][0] && tally[i][1] > tally[best][1])) best = i;
  }
  return best;
}

/** "3 yes" or "1 yes · 1 maybe" */
export function planCount(yes, maybe) {
  return maybe > 0 ? `${yes} yes · ${maybe} maybe` : `${yes} yes`;
}

/** The day of a plan's time: "Today", "Tomorrow" or "Wed 25 Nov". */
export function dayText(ms, now = Date.now()) {
  const d = new Date(ms);
  const today = new Date(now);
  const tomorrow = new Date(today.getFullYear(), today.getMonth(), today.getDate() + 1);
  if (d.toDateString() === today.toDateString()) return 'Today';
  if (d.toDateString() === tomorrow.toDateString()) return 'Tomorrow';
  return `${DAYS[d.getDay()].slice(0, 3)} ${d.getDate()} ${MONTHS[d.getMonth()]}`;
}

/** "20:00" */
export function hourText(ms) {
  const d = new Date(ms);
  return `${two(d.getHours())}:${two(d.getMinutes())}`;
}

/** A time on the sheet that makes a plan: "Wednesday 25 Nov, 20:00". */
export function planTimeText(ms) {
  const d = new Date(ms);
  return `${DAYS[d.getDay()]} ${d.getDate()} ${MONTHS[d.getMonth()]}, ${hourText(ms)}`;
}

/** The hours to choose from for a plan, as the app offers them (20:00 first chosen). */
export const PLAN_HOURS = ['14:00', '15:00', '16:00', '17:00', '18:00', '19:00', '19:30', '20:00', '20:30', '21:00', '22:00', '23:00'];

/** The days to choose from for a plan: today and the next six, each {date (its midnight, ms), label}. */
export function planDays(now = Date.now()) {
  const today = new Date(now);
  const out = [];
  for (let i = 0; i < 7; i++) {
    const d = new Date(today.getFullYear(), today.getMonth(), today.getDate() + i);
    out.push({ date: d.getTime(), label: i === 0 ? 'Today' : i === 1 ? 'Tomorrow' : `${DAYS[d.getDay()]} ${d.getDate()} ${MONTHS[d.getMonth()]}` });
  }
  return out;
}

/** A day's midnight and "HH:MM" as one time (ms), on this device's clock. */
export function planTimeAt(dayMs, hour) {
  const d = new Date(dayMs);
  const [h, m] = hour.split(':').map(Number);
  return new Date(d.getFullYear(), d.getMonth(), d.getDate(), h, m).getTime();
}

/** One of the own devices, as the settings list it: {title, line}. */
export function deviceText(d) {
  if (d.me) return { title: 'This browser', line: 'A web browser, here' };
  return { title: d.kind === 2 ? 'A web browser' : 'The app on another computer', line: `Device ${String(d.id).slice(0, 6)}` };
}

/**
 * What the window for linking a device says: on the device that has the friends ("giving") or
 * on the new one.
 */
export function linkWords(state, giving, problem = '') {
  switch (state) {
    case 'waiting':
      return giving
        ? 'On the other device: Settings → Friends & chats → Link to my other device, and paste this link. In a browser, the friends panel offers it too. The link works for two minutes.'
        : 'Waiting for the other device…';
    case 'code':
      return giving
        ? 'Does the other device show the same six digits? Only then send: whoever has these digits gets your friends and keys.'
        : 'The other device shows six digits too. If they are the same, confirm there.';
    case 'sending':
      return 'Sending, sealed for the other device only…';
    case 'receiving':
      return 'Your friends are on their way…';
    case 'done':
      return giving ? 'The other device has your friends now. It gets your chats from here on.' : 'This browser has your friends now.';
    case 'failed':
      return problem;
    default:
      return '';
  }
}

/** The title of that window. */
export function linkTitle(state) {
  return state === 'done' ? 'Linked' : state === 'failed' ? 'Not linked' : 'Link a device';
}
