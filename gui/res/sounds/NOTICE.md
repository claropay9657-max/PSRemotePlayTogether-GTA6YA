# Sounds

The alert sounds, the chat sound, the music of the stream scenes and the sound of the intro
film are the project's own. They are synthesized from scratch by
`scripts/sounds/make_alert_sounds.py`, `scripts/sounds/make_scene_music.py` and
`scripts/sounds/make_intro_sound.py`: no samples, presets or other third-party material,
so they ship under the project's licence and are safe to use on a stream.

- `follow`, `subscribe`, `resub`, `giftsubs`, `bits`, `raid`, `channelpoints`, `hypetrain`,
  `poll`, `prediction`, `goal`, `charity`, `shoutout`, `adbreak`: one per kind of Twitch
  alert, each mastered to the same loudness (-16 LUFS, channel points -18), peaks below -1.5 dBFS.
- `chat`: a new chat message, quieter (-22 LUFS).
- `music-starting`, `music-brb`, `music-ending`: 16-bar loops for Starting soon, Be right back
  and Thanks for watching, -20 LUFS. They loop without a gap: the FLAC keeps the exact number
  of samples.
- `intro`: the music and the effects of the intro film (`gui/src/qml/intro`) in one piece that
  starts with the film, -18 LUFS. D major, 100 beats a minute; the film is timed to it, and
  the script reads the film's timing. The chat sound and the follow alert play in it where
  the film shows what they are for.

The scripts write 48 kHz 24-bit WAV masters; the files here are 16-bit FLAC made from them
(dithered), for example with `flac --best -o follow.flac follow.wav` after a dithered
conversion to 16 bit.
