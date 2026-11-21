# Sound generators

`make_alert_sounds.py` and `make_scene_music.py` make every sound of the app's Twitch alerts,
its chat sound and the music of the stream scenes (see `gui/res/sounds/NOTICE.md`). They need
only numpy. Keep the two together: the music script uses the instruments of the other one.

    python3 make_alert_sounds.py OUT_DIR [name ...]
    python3 make_scene_music.py OUT_DIR [name ...]
