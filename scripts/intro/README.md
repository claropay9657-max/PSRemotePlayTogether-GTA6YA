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

    # one moment, 1920 x 1080
    build/filmshot/filmshot scripts/intro/stage.qml 960 540 shot frame.png 9.6
    # sixteen moments on one sheet
    build/filmshot/filmshot scripts/intro/stage.qml 480 270 sheet sheet.png 4 1.2 3.9 6.6 9.6 13.2 17.4 21.5 24.6 28 31.9 36.9 39.6 43.9 47.4 48.9 50.6
    # the whole film with its sound, 1280 x 720
    build/filmshot/filmshot scripts/intro/stage.qml 640 360 raw 30 0 54.3 \
      | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - -i gui/res/sounds/intro.flac \
               -c:v libx264 -crf 19 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest intro.mp4

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
