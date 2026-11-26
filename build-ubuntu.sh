#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

format=${1:-deb}
case "$format" in
    deb|rpm|all) ;;
    *) printf 'Usage: %s [deb|rpm|all]\n' "$0" >&2; exit 2 ;;
esac

name=openoledshifter
version=${PACKAGE_VERSION:-0.1.0}
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'PACKAGE_VERSION must have the form X.Y.Z.\n' >&2
    exit 2
fi
arch=$(dpkg --print-architecture)
case "$arch" in
    amd64) rpm_arch=x86_64 ;;
    arm64) rpm_arch=aarch64 ;;
    *) printf 'Unsupported Ubuntu architecture: %s\n' "$arch" >&2; exit 1 ;;
esac

uuid=openoledshifter@openoledshifter
desktop_id=org.openoledshifter.Desktop
stage=$(mktemp -d)
chmod 0755 "$stage"
output="$PWD/release/ubuntu"
mkdir -p "$output"
temporary=
trap 'rm -rf -- "$stage"; if [[ -n "$temporary" ]]; then rm -f -- "$temporary"; fi' EXIT

app="$stage/usr/lib/$name"
extension="$stage/usr/share/gnome-shell/extensions/$uuid"
install -d "$extension/schemas"
install -m 0644 Linux/extension/{extension,translation}.js Linux/extension/metadata.json "$extension/"
install -Dm0644 "Linux/packaging/$desktop_id.desktop" "$stage/usr/share/applications/$desktop_id.desktop"
install -Dm0644 assets/app-icon.png "$stage/usr/share/icons/hicolor/512x512/apps/$desktop_id.png"
install -Dm0644 LICENSE "$stage/usr/share/doc/$name/copyright"

schema=org.gnome.shell.extensions.openoledshifter.gschema.xml
cmake -S NativeApp -B "$stage/native-build" -DCMAKE_BUILD_TYPE=Release -DOPENOLED_BUILD_UI=ON
cmake --build "$stage/native-build" --parallel
ctest --test-dir "$stage/native-build" --output-on-failure
"$stage/native-build/generate_policy" \
    "Linux/extension/schemas/$schema.in" "$extension/schemas/$schema" "$extension/cycles.js"
install -m 0644 Linux/shared/shift-runtime.js "$extension/shift-runtime.js"
glib-compile-schemas --strict "$extension/schemas"
gjs -m "$extension/cycles.js"
cp Linux/shared/shift-runtime-test.js "$extension/"
gjs -m "$extension/shift-runtime-test.js"
rm "$extension/shift-runtime-test.js"
gjs -m Linux/translation-test.js
desktop-file-validate "$stage/usr/share/applications/$desktop_id.desktop"

plasma="$stage/usr/share/plasma/wallpapers/org.openoledshifter.wallpaper"
bash Linux/packaging/stage-plasma.sh "$extension/cycles.js" "$plasma"
for source in "$extension" "$plasma"; do
    if [[ "$source" == "$extension" ]]; then
        bundle="$uuid.shell-extension.zip"
    else
        bundle=openoledshifter-plasma.zip
    fi
    if [[ "$source" == "$plasma" ]]; then
        (cd "$(dirname "$source")" && zip -qr "$stage/$bundle" "$(basename "$source")")
    else
        (cd "$source" && zip -qr "$stage/$bundle" .)
    fi
    mv "$stage/$bundle" "$output/"
done

install -Dm0755 "$stage/native-build/OpenOLEDShifter" "$app/OpenOLEDShifter"
rm -rf -- "$stage/native-build"

build_deb() {
    install -d "$stage/DEBIAN"
    cat > "$stage/DEBIAN/control" <<CONTROL
Package: $name
Version: $version
Architecture: $arch
Section: utils
Priority: optional
Maintainer: OpenOLEDShifter contributors <108850792+Wenbo-Zhong@users.noreply.github.com>
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Depends: libwxgtk3.2-1t64, libgtk-3-0t64, libglib2.0-0t64, libjson-glib-1.0-0, libayatana-appindicator3-1, libstdc++6
Suggests: gnome-shell, plasma-workspace
Homepage: https://github.com/Wenbo-Zhong/openOLEDShifter
Description: OLED wallpaper pixel shifting for GNOME and KDE Plasma
 Includes a GNOME Shell extension and a KDE Plasma wallpaper plugin.
 A wxWidgets settings window is available in the app grid.
CONTROL

    local artifact="$output/${name}_${version}_${arch}.deb"
    temporary=$(mktemp "$output/.openoledshifter.XXXXXX")
    dpkg-deb --root-owner-group -Zzstd --build "$stage" "$temporary"
    chmod 0644 "$temporary"
    mv -f -- "$temporary" "$artifact"
    printf 'Ready: %s\n' "$artifact"
}

build_rpm() {
    local topdir="$stage/rpmbuild"
    rpmbuild -bb --target "$rpm_arch" \
        --define "_topdir $topdir" \
        --define "package_version $version" \
        --define "payload $stage" \
        Linux/packaging/openoledshifter.spec
    local filename="$name-$version-1.$rpm_arch.rpm"
    mv -f -- "$topdir/RPMS/$rpm_arch/$filename" "$output/$filename"
    printf 'Ready: %s\n' "$output/$filename"
}

case "$format" in
    deb) build_deb ;;
    rpm) build_rpm ;;
    all) build_deb; build_rpm ;;
esac
