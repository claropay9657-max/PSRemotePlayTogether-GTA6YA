// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Where the page keeps its friends (web/js/friends/client.js): the whole state as one object,
// written again after every change. The app keeps the same in one sealed file
// (gui/src/friends.cpp); a browser keeps it in IndexedDB, which stores the object as it is,
// CryptoKeys included (structured clone), so private keys never leave WebCrypto.
//
// A store has load() (the state, or null), save(state) and clear(). save() takes its copy of
// the state at once, before it returns to the event loop: what changes afterwards is not in it.
// WriteQueue below lets the client save as often as it likes: one write at a time, never two
// interleaved, and a save() resolves only once a write that started after it is done (so a
// ratchet is on disk before its message goes out).

/** Keeps the state in memory only, as a copy: for tests, and where IndexedDB is missing. */
export class MemoryStore {
  #data = null;

  async load() {
    return this.#data === null ? null : structuredClone(this.#data);
  }

  async save(state) {
    this.#data = structuredClone(state);
  }

  async clear() {
    this.#data = null;
  }
}

function done(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

function finished(tx) {
  return new Promise((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
    tx.onabort = () => reject(tx.error || new Error('the write was aborted'));
  });
}

/**
 * One IndexedDB database with one object store and one record: the state. Two tabs of the page
 * must not both be this browser's device (their ratchets would part ways and a message key
 * could be used twice), so claim() takes the browser's lock for it first where the browser has
 * Web Locks.
 */
export class IndexedDbStore {
  #db = null;
  #release = null;

  constructor(name = 'psrpt-friends') {
    this.name = name;
  }

  #open() {
    if (this.#db) return this.#db;
    this.#db = new Promise((resolve, reject) => {
      const request = globalThis.indexedDB.open(this.name, 1);
      request.onupgradeneeded = () => request.result.createObjectStore('state');
      request.onsuccess = () => {
        const db = request.result;
        // A newer page that changes the database: this one lets go
        db.onversionchange = () => {
          db.close();
          this.#db = null;
        };
        resolve(db);
      };
      request.onerror = () => reject(request.error);
      request.onblocked = () => reject(new Error('the friends database is blocked by another tab'));
    });
    this.#db.catch(() => {
      this.#db = null;
    });
    return this.#db;
  }

  /** Whether this tab may use the state: false while another tab of the page has it. */
  claim() {
    if (this.#release) return Promise.resolve(true);
    const locks = globalThis.navigator && globalThis.navigator.locks;
    if (!locks) return Promise.resolve(true);
    return new Promise((resolve) => {
      locks.request(`${this.name} device`, { ifAvailable: true }, (lock) => {
        if (!lock) {
          resolve(false);
          return undefined;
        }
        resolve(true);
        // Held until close(), or until the tab goes
        return new Promise((release) => {
          this.#release = release;
        });
      }).catch(() => resolve(true));
    });
  }

  async load() {
    const db = await this.#open();
    const value = await done(db.transaction('state', 'readonly').objectStore('state').get('state'));
    return value === undefined ? null : value;
  }

  async save(state) {
    const db = await this.#open();
    // Strict: the write is on disk when it is done, not only handed to the system
    const tx = db.transaction('state', 'readwrite', { durability: 'strict' });
    tx.objectStore('state').put(state, 'state');
    await finished(tx);
  }

  async clear() {
    const db = await this.#open();
    const tx = db.transaction('state', 'readwrite', { durability: 'strict' });
    tx.objectStore('state').delete('state');
    await finished(tx);
  }

  close() {
    if (this.#release) {
      this.#release();
      this.#release = null;
    }
    if (this.#db) {
      this.#db.then((db) => db.close(), () => {});
      this.#db = null;
    }
  }
}

/**
 * Saves the state through a store one write at a time. save() asks for a write of the state as
 * it is when that write starts; saves asked for while one is waiting share it.
 */
export class WriteQueue {
  #store;
  #state;
  #tail = Promise.resolve();
  #waiting = null;

  /** @param {() => object} state gives the current state when a write starts */
  constructor(store, state) {
    this.#store = store;
    this.#state = state;
  }

  /** After everything queued before, whether that worked or not. */
  #enqueue(op) {
    const p = this.#tail.catch(() => {}).then(op);
    this.#tail = p;
    return p;
  }

  /** @returns {Promise<void>} once a write that started after this call is done */
  save() {
    if (this.#waiting) return this.#waiting;
    const p = this.#enqueue(() => {
      // Started: what changes from now on needs a write of its own
      if (this.#waiting === p) this.#waiting = null;
      return this.#store.save(this.#state());
    });
    this.#waiting = p;
    return p;
  }

  /** Removes the state, after the writes before it. */
  clear() {
    this.#waiting = null;
    return this.#enqueue(() => this.#store.clear());
  }
}
