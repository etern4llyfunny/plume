pkgname=plume
pkgver=0.1.0beta1
_tag=v0.1.0-beta1
pkgrel=3
pkgdesc='Plume — лёгкое рабочее окружение Wayland (Wayfire + C/GTK4)'
arch=('x86_64')
url='https://github.com/etern4llyfunny/plume'
license=('GPL-3.0-or-later')
depends=(
  'wayfire'
  'plume-gtkdecor'
  'waybar'
  'mako'
  'swaybg'
  'gtk4'
  'gtk4-layer-shell'
  'python'
  'thunar'
  'swaylock'
  'grim'
  'slurp'
  'wl-clipboard'
  'xdg-user-dirs'
  'kitty'
  'libpulse'
  'pavucontrol'
  'brightnessctl'
  'nm-connection-editor'
  'polkit-gnome'
  'procps-ng'
  'blueman'
  'ttf-nerd-fonts-symbols'
  'xorg-xwayland'
  'xdg-desktop-portal-gtk'
  'xdg-desktop-portal-wlr'
)
makedepends=('gcc' 'pkgconf')
source=("${pkgname}-${_tag}.tar.gz::${url}/archive/refs/tags/${_tag}.tar.gz")
sha256sums=('e9830a70033505804503a56e58fd2f8cda7480aaea2453b72e4540580e968ad7')

build() {
  cd "${srcdir}/${pkgname}-${_tag#v}"
  make
}

package() {
  cd "${srcdir}/${pkgname}-${_tag#v}"
  make DESTDIR="${pkgdir}" PREFIX=/usr install
  # The beta1 archive predates the desktop-entry fix in Makefile.
  install -Dm644 share/applications/plume-settings.desktop \
    "${pkgdir}/usr/share/applications/plume-settings.desktop"
}
