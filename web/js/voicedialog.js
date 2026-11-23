// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Voice & webcam: the app's settings page of that name (gui/src/qml/settings/VoicePage.qml),
// with the same rows and words: taking part in the voice chat, starting muted, the others'
// volume, the microphone test with its mark, the speaker test, the webcam's picture size, the
// picture itself as the others would get it, and whether the others' webcams show. Nothing
// of the tests is sent anywhere. "Leave me out of their streams" is on the app's Live
// streaming page, which a browser does not have: it is here, under its own heading.
//
// Before joining and during a session alike (the stream menu opens it).

import { DeviceCheck, MIC_GATE } from './devicecheck.js';
import { el } from './dom.js';
import { icon } from './menu.js';
import { group, pill, row, segmented, slider, toggle } from './setparts.js';

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
   * @param {() => {voice: boolean, startMuted: boolean, voiceVolume: number, showCams: boolean, noStreams: boolean, canVoice: boolean}} h.prefs
   * @param {(name: string, value: boolean|number) => void} h.onPref one of those changed
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

    // Whether to take part, how to start, and how loud the others are, as the app's first rows
    const prefs = this.h.prefs();
    this.voiceSwitch = toggle('Take part in the voice chat', prefs.voice, (on) => {
      this.h.onPref('voice', on);
      this.refresh();
    });
    this.voiceRow = row({ title: 'Take part in the voice chat', subtitle: 'The browser asks for your microphone when you join.', textMin: 180, switch: this.voiceSwitch });
    this.mutedSwitch = toggle('Start with my microphone muted', prefs.startMuted, (on) => this.h.onPref('startMuted', on));
    this.mutedRow = row({ title: 'Start with my microphone muted', subtitle: 'While you play, the stream menu mutes and unmutes you.', textMin: 180, switch: this.mutedSwitch });
    this.volumeSlider = slider('Volume of the others', { min: 0, max: 200, step: 5, value: prefs.voiceVolume, width: 190, valueWidth: 48, format: (v) => `${Math.round(v)} %` },
      (v) => this.h.onPref('voiceVolume', v));
    this.volumeRow = row({ title: 'Volume of the others', textMin: 140, controls: [this.volumeSlider] });

    const voice = group('Voice chat', [
      this.voiceRow, this.mutedRow, this.volumeRow,
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
    this.camsSwitch = toggle('Show the other players\' webcams', prefs.showCams, (on) => this.h.onPref('showCams', on));
    const camsRow = row({ title: 'Show the other players\' webcams', textMin: 180, switch: this.camsSwitch });
    const webcam = group('Webcam', [this.sizeRow, this.cameraRow, this.preview, camsRow]);

    this.streamsSwitch = toggle('Leave me out of their streams', prefs.noStreams, (on) => this.h.onPref('noStreams', on));
    const streams = group('When others go live', [row({
      title: 'Leave me out of their streams',
      subtitle: 'Your voice, webcam and name stay out of what the other players stream. Players with an older version of the app do not know this yet.',
      textMin: 180,
      switch: this.streamsSwitch,
    })]);

    this.body.replaceChildren(voice, webcam, streams);
    this.refresh();
  }

  refresh() {
    const check = this.check;
    const playing = this.h.playing();
    const prefs = this.h.prefs();
    this.voiceSwitch.checked = prefs.voice;
    this.voiceRow.setEnabled(prefs.canVoice);
    this.mutedRow.setEnabled(prefs.canVoice && prefs.voice);
    this.volumeRow.setEnabled(prefs.canVoice && prefs.voice);
    this.mutedSwitch.checked = prefs.startMuted;
    this.volumeSlider.set(prefs.voiceVolume);
    this.camsSwitch.checked = prefs.showCams;
    this.streamsSwitch.checked = prefs.noStreams;
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
