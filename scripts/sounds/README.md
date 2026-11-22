# Sound generators

`make_alert_sounds.py`, `make_scene_music.py` and `make_intro_sound.py` make every sound of the
app's Twitch alerts, its chat sound, the music of the stream scenes and the sound of the intro
film (see `gui/res/sounds/NOTICE.md`). They need only numpy. Keep them together: the music and
the intro scripts use the instruments of the first one, and the intro script reads the film's
timing from `gui/src/qml/intro/IntroFilm.qml`.

    python3 make_alert_sounds.py OUT_DIR [name ...]
    python3 make_scene_music.py OUT_DIR [name ...]
    python3 make_intro_sound.py OUT_DIR
