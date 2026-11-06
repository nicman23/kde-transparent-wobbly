# kde-transparent-wobbly

Two KWin effects that do their work on the GPU, built out of tree against the installed KWin.

- **GPU Wobbly Windows** (`gpuwobbly`): the wobbly windows physics, mesh and drawing run in shaders;
  a moved window is drawn translucent over a blurred copy of what is behind it. Settings: those of
  Wobbly Windows, plus the opacity while moving.
- **GPU Blur** (`gpublur`): blurs behind translucent windows, following KWin's Blur effect in what
  to blur, and optionally every translucent window. Windows being moved are left to GPU Wobbly
  Windows, which frosts with these settings too. Settings: those of Blur, plus "blur all
  translucent windows". It replaces Blur: do not enable both.

Requires KWin 6.8 with OpenGL compositing. The plugins use KWin's internal effect API, so rebuild
them whenever KWin is updated.

## Build and install

    makepkg -si                      # Arch: builds the checkout, PKGBUILD pins the KWin version

    cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr && cmake --build build
    sudo cmake --install build      # elsewhere

Then enable them in System Settings → Desktop Effects (and disable Wobbly Windows and Blur).

## Tests

`tests/` runs KWin headless (`kwin_wayland --virtual`, on the GPU) with real input through EIS,
and screenshots through KWin's screenshot interface. Output goes to `tmp/`.

    tests/run-wobbly.sh gpuwobbly tmp/shots && tests/check.py tmp/shots tmp/wobbly/kwin.log
    tests/run-blur.sh tmp/blur-shots gpublur        # DRAG=1, RIGHT_CLICK=1, CLIENT=dolphin
    tests/kcm-shot.sh kwin_gpublur_config tmp/kcm.png

Needs dbus-run-session, qdbus6, qml (qt6-declarative), konsole/dolphin/kvantum for the blur
tests, and python with dbus-python and Pillow.
