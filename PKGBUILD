pkgname=plume
pkgver=0.1.0beta1
_tag=v0.1.0-beta1
pkgrel=1
pkgdesc='Plume — лёгкое рабочее окружение Wayland (Wayfire + C/GTK4)'
arch=('x86_64')
url='https://github.com/etern4llyfunny/plume'
license=('GPL-3.0-or-later')
depends=(
  'wayfire'
  'waybar'
  'mako'
  'swaybg'
  'gtk4'
  'gtk4-layer-shell'
  'python'
  'thunar'
)
makedepends=('gcc' 'pkgconf')
source=("${pkgname}-${_tag}.tar.gz::${url}/archive/refs/tags/${_tag}.tar.gz")
sha256sums=('SKIP')

build() {
  cd "${srcdir}/${pkgname}-${_tag#v}"
  make
}

package() {
  cd "${srcdir}/${pkgname}-${_tag#v}"
  make DESTDIR="${pkgdir}" PREFIX=/usr install
}
