# Looking at the intro film without the app

The intro film (`gui/src/qml/intro`) is drawn from its time alone: `IntroFilm.qml` has a
property `t` in seconds, and nothing in it runs by itself. `filmshot` here sets `t` and
renders what the film shows then, without a window: one frame, several moments on one sheet,
or every frame for a video. It draws with Qt Quick's own scene graph, the way the app does.

Build it once (Qt 6.6 or newer with its private headers, as Homebrew's or aqtinstall's Qt
has them), and put the app's resources next to it:

    cmake -S scripts/intro -B build/filmshot -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
    cmake --build build/filmshot
    RCC="$(qmake -query QT_INSTALL_LIBEXECS)/rcc"
    (cd gui/res && "$RCC" -binary resources.qrc -o ../../build/filmshot/resources.rcc \
                && "$RCC" -binary overlay/overlay.qrc -o ../../build/filmshot/overlay.rcc)

Use the `rcc` of the Qt that `filmshot` was built with: an older Qt cannot read a newer one's
resource files. Building against the Qt version of the Windows and Linux builds (see
`scripts/ci`) shows the film as it looks there.

    # the colours of a video, said in its stream (FFmpeg takes them from the pictures, not from options)
    TAGS=setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=tv
    # one moment, 1920 x 1080
    build/filmshot/filmshot scripts/intro/stage.qml 960 540 shot frame.png 9.6
    # sixteen moments on one sheet
    build/filmshot/filmshot scripts/intro/stage.qml 480 270 sheet sheet.png 4 1.2 3.9 6.6 9.6 13.2 17.4 21.5 24.6 28 31.9 36.9 39.6 43.9 47.4 48.9 50.6
    # the whole film with its sound, 1280 x 720
    build/filmshot/filmshot scripts/intro/stage.qml 640 360 raw 30 0 54.3 \
      | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - -i gui/res/sounds/intro.flac \
               -vf "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p,$TAGS" \
               -c:v libx264 -crf 19 -c:a aac -b:a 192k -shortest intro.mp4

The numbers after the file are the picture's size in points; every point is two pixels
(`FILMSHOT_DPR` says otherwise). `name=value` anywhere after them sets a property of
`stage.qml`: `still=1` (the sky stands still), `tall=1` (the upright film) and `promo=1`
(a video to post, see below). The `scale` options and `$TAGS` matter: without them FFmpeg
writes the colours the way small pictures have them, players read a large picture the other
way, and the film's purple and pink come out a little wrong.

## The upright film

`tall=1` shows the film on its upright stage, 1080 x 1920: the same film at the same times,
laid out for a picture higher than wide. It is for videos for phones only: the app always
shows the wide film, whatever the shape of its window. The frame for it is 9 to 16:

    build/filmshot/filmshot scripts/intro/stage.qml 540 960 shot frame.png 9.6 tall=1

What matters in it keeps away from where the apps for upright videos (TikTok, Instagram
Reels, YouTube Shorts) lay their own buttons over a video: nothing above 250 or below 1500,
and in the lower half nothing right of 910 but the game picture. `IntroFilm.qml` says so
where `tall` is declared; keep to it when something moves.

## A video to post

`promo=1` makes the film a video about the app: the end does not offer to play again and
says where the app is to be had instead, and the upright one opens with a line of its own
over the icon coming together (the lines are in `stage.qml`). For posting, the sound is
raised to the loudness those apps play at (-14 LUFS, peaks held under -1 dB) without
changing the film's own file:

    ffmpeg -i gui/res/sounds/intro.flac -c:a pcm_s24le reel-sound.wav \
           -af aresample=192000,volume=4dB,alimiter=limit=0.87:attack=3:release=60:level=false,aresample=48000

    # upright, rendered once at 60 pictures a second and written at 30 (every app) and at 60
    build/filmshot/filmshot scripts/intro/stage.qml 540 960 raw 60 0 54.34 tall=1 promo=1 \
      | ffmpeg -f rawvideo -pix_fmt rgba -s 1080x1920 -r 60 -i - -i reel-sound.wav -filter_complex \
          "[0:v]scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int,format=yuv420p,$TAGS,split=2[v60][vh];[vh]fps=30[v30];[1:a]asplit=2[a60][a30]" \
          -map "[v30]" -map "[a30]" -c:v libx264 -preset slow -crf 16 -x264-params aq-mode=3 \
          -c:a aac -b:a 256k -movflags +faststart intro-reel.mp4 \
          -map "[v60]" -map "[a60]" -c:v libx264 -preset slow -crf 16 -x264-params aq-mode=3 \
          -c:a aac -b:a 256k -movflags +faststart intro-reel-60fps.mp4

The wide one is the same without `tall=1`, at `960 540` and `1920x1080`.

## The film at the top of the README

`docs/images/intro.webp` is the film as a looping picture, 800 x 450 at 15 frames a second,
with the sky standing still (`still=1`) so that only what happens changes between frames. Its
first frame is the finished title, for wherever the picture does not move.

    mkdir frames
    build/filmshot/filmshot scripts/intro/stage.qml 400 225 raw 15 0 50.7 still=1 \
      | ffmpeg -f rawvideo -pix_fmt rgba -s 800x450 -r 15 -i - -start_number 1 frames/f%04d.png
    build/filmshot/filmshot scripts/intro/stage.qml 400 225 shot frames/f0000.png 3.9 still=1
    cd frames && last=$(ls f*.png | tail -1)
    img2webp -loop 0 -kmin 15 -kmax 24 -lossy -q 84 -m 5 -d 40 f0000.png \
      -d 67 $(ls f*.png | sed -n '2,$p' | grep -v "$last") -d 2600 "$last" -o ../docs/images/intro.webp

Whole frames every second or two (`-kmin`, `-kmax`) keep the dark gradients clean: without
them what one frame leaves behind stays until something moves there again.

## The sound

`scripts/sounds/make_intro_sound.py` makes the film's music and effects and reads the
film's timing from `IntroFilm.qml`. After changing when something happens in the film, run it
again and convert the result as its header says.
