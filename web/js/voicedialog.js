// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Voice & webcam: what the app's settings page of that name (gui/src/qml/settings/
// VoicePage.qml) has for trying one's devices before a session, with the same rows and
// words: the microphone test with its mark, the speaker test, the webcam's picture size and
// the picture itself as the others would get it. Nothing of it is sent anywhere.
//
// What the app sets up on that page and the page already has elsewhere stays where it is:
// taking part in the voice chat is on the card for joining, the rest is in the stream menu.

import { DeviceCheck, MIC_GATE } from './devicecheck.js';
import { el } from './dom.js';
import { icon } from './menu.js';
import { group, pill, row, segmented } from './setparts.js';

const CAMERA_LINES = ['320 × 180, about 0.4 Mbit/s', '480 × 270, about 1 Mbit/s', '640 × 360, about 2.5 Mbit/s'];
const SIDES = ['', 'Playing on the left…', 'Playing on the right…', 'Playing on both…'];

export class VoiceDialog {
  /**
   * @param {HTMLDialogElement} dialog
   * @param {HTMLElement} body where everything but the title and "Close" goes
   * @param {object} h
   * @param {() => number} h.cameraSize the webcam's picture size, an index into CAMERA_SIZES
   * @param {(index: number) => void} h.onCameraSize
   * @param {() => boolean} h.playing a session is running: it has the devices
   */
  constructor(dialog, body, h) {
    this.dialog = dialog;
    this.body = body;
    this.h = h;
    this.check = new DeviceCheck(() => this.refresh());
    dialog.addEventListener('close', () => this.check.stopAll());
    this.build();
  }

  show() {
    this.refresh();
    if (this.dialog.showModal) this.dialog.showModal();
    else this.dialog.setAttribute('open', '');
  }

  build() {
    const check = this.check;

    // ---- voice chat
    this.micName = el('span', '', '');
    this.speakerName = el('span', '', 'The speakers or headphones your system plays on');
    const devices = el('div', 'set-devices');
    const line = (name, text) => {
      const l = el('div', 'set-device');
      l.append(icon(name, 16), text);
      return l;
    };
    devices.append(line('mic', this.micName), line('speaker', this.speakerName));

    // How loud the microphone hears you, with the mark from which the others hear you
    this.meter = el('span', 'set-meter');
    this.meter.setAttribute('role', 'progressbar');
    this.meter.setAttribute('aria-label', 'Microphone level');
    this.meter.setAttribute('aria-valuemin', '0');
    this.meter.setAttribute('aria-valuemax', '100');
    this.meterFill = el('span', 'set-meter-fill');
    const mark = el('span', 'set-meter-mark');
    mark.style.left = `calc(${MIC_GATE}% - 1px)`;
    this.meter.append(this.meterFill, mark);
    this.micButton = pill('ghost', 'Test', 'mic', () => (check.micTesting ? check.stopMic() : check.startMic()));
    this.micRow = row({ title: 'Microphone test', subtitle: ' ', controls: [this.meter, this.micButton] });

    // A chime on the left, then on the right, then on both
    this.speakerButton = pill('ghost', 'Play', 'speaker', () => check.playTestSound());
    this.speakerButton.setAttribute('aria-label', 'Play a test chime on the speakers');
    this.speakerRow = row({ title: 'Speaker test', subtitle: ' ', controls: [this.speakerButton] });

    const tip = el('div', 'set-tip');
    tip.append(icon('info', 15), el('span', '', 'Use headphones. If a friend hears themselves, their voice is coming out of your speakers into your microphone.'));

    const voice = group('Voice chat', [
      row({ title: 'Microphone and speakers', subtitle: 'The ones your browser uses: choose them in the browser, or in your system\'s sound settings.', controls: [] }),
      devices, this.micRow, this.speakerRow, tip,
    ]);

    // ---- webcam
    this.sizeSeg = segmented('Picture size', ['Small', 'Medium', 'Large'], this.h.cameraSize(), (index) => {
      this.h.onCameraSize(index);
      // The picture shown is the one that would be sent
      if (check.cameraTesting) check.startCamera(index);
      this.refresh();
    });
    this.sizeRow = row({ title: 'Picture size', subtitle: ' ', textMin: 120, controls: [this.sizeSeg] });
    // The picture as the others would get it, before a session
    this.cameraButton = pill('ghost', 'Show', 'camera', () => (check.cameraTesting ? check.stopCamera() : check.startCamera(this.h.cameraSize())));
    this.cameraRow = row({ title: 'Your picture', subtitle: ' ', controls: [this.cameraButton] });
    // 16:9 like the picture that is sent, rounded like the webcam tiles in a session
    this.preview = el('div', 'set-preview');
    this.previewImage = el('img');
    this.previewImage.alt = 'Your webcam';
    this.previewWait = el('span', '', 'Starting the camera…');
    const frame = el('div', 'set-preview-frame');
    frame.append(this.previewImage, this.previewWait);
    this.preview.append(frame);
    const webcam = group('Webcam', [this.sizeRow, this.cameraRow, this.preview]);

    this.body.replaceChildren(voice, webcam);
    this.refresh();
  }

  refresh() {
    const check = this.check;
    const playing = this.h.playing();
    if (playing && (check.micTesting || check.speakerTesting || check.cameraTesting)) {
      check.stopAll();
      return;
    }

    this.micName.textContent = check.micName || 'The microphone your browser is set to use';
    this.micRow.setSubtitle(playing ? 'While you play, the stream menu shows how loud you are.'
      : check.micError ? `The microphone could not be opened: ${check.micError}`
        : check.micTesting ? (check.micLevel >= MIC_GATE ? 'Your friends hear this. Talk as you would while playing.'
          : 'Talk: from the mark on, your friends hear you.')
          : 'See how loud your microphone hears you. Nothing is sent.');
    this.meter.hidden = !check.micTesting;
    this.meter.setAttribute('aria-valuenow', String(check.micLevel));
    this.meterFill.style.width = check.micLevel > 0 ? `max(10px, ${check.micLevel}%)` : '0';
    this.meterFill.classList.toggle('heard', check.micLevel >= MIC_GATE);
    this.micButton.set('ghost', check.micTesting ? 'Stop' : 'Test', check.micTesting ? '' : 'mic');
    this.micButton.setAttribute('aria-label', check.micTesting ? 'Stop the microphone test' : 'Test the microphone');
    this.micButton.disabled = playing;

    this.speakerRow.setSubtitle(playing ? 'While you play, the game and your friends are the test.'
      : check.speakerError ? `The speakers could not be opened: ${check.speakerError}`
        : check.speakerTesting ? SIDES[check.speakerSide] || ''
          : 'A chime on the left, then on the right, then on both.');
    this.speakerButton.disabled = playing || check.speakerTesting;

    const size = this.h.cameraSize();
    this.sizeSeg.set(size);
    this.sizeRow.setSubtitle(CAMERA_LINES[size] || '');
    this.cameraRow.setSubtitle(playing ? 'While you play, the stream menu shows it.'
      : check.cameraError ? check.cameraError
        : check.cameraTesting ? 'As your friends see it, at the size chosen above.'
          : 'See your webcam before you play. Nothing is sent.');
    this.cameraButton.set('ghost', check.cameraTesting ? 'Hide' : 'Show', check.cameraTesting ? '' : 'camera');
    this.cameraButton.setAttribute('aria-label', check.cameraTesting ? 'Hide my webcam picture' : 'Show my webcam picture');
    this.cameraButton.disabled = playing;
    this.preview.hidden = !check.cameraTesting;
    if (check.cameraPicture && this.previewImage.src !== check.cameraPicture) this.previewImage.src = check.cameraPicture;
    this.previewImage.hidden = !check.cameraPicture;
    this.previewWait.hidden = !!check.cameraPicture;
  }
}
