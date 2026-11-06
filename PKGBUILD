# Builds the checkout this PKGBUILD sits in: run makepkg from the repo root.
pkgname=kde-transparent-wobbly
pkgver=0.7.0
pkgrel=1
pkgdesc="KWin effects on the GPU: wobbly windows with a frosted background, and background blur"
arch=('x86_64')
url="https://github.com/nicman23/kde-transparent-wobbly"
license=('GPL-2.0-or-later')
# Built against KWin's internal effect API: rebuild whenever kwin updates.
depends=('kwin=6.8.2' 'kcmutils' 'kconfig' 'kdecoration' 'ki18n' 'libxcb')
makedepends=('cmake' 'ninja' 'extra-cmake-modules')
provides=('kwin-effect-gpuwobbly' 'kwin-effect-gpublur')
conflicts=('kwin-effect-gpuwobbly' 'kwin-effect-gpublur')
replaces=('kwin-effect-gpuwobbly' 'kwin-effect-gpublur')

build() {
    cmake -S "$startdir" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}
