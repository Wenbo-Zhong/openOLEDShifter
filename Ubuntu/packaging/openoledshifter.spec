# SPDX-License-Identifier: GPL-3.0-only
# Keep the self-contained .NET payload intact.
%global __os_install_post %{nil}
%global _build_id_links none
%global debug_package %{nil}

Name:           openoledshifter
Version:        %{package_version}
Release:        1
Summary:        OLED wallpaper pixel shifting for GNOME Wayland
License:        GPL-3.0-only
URL:            https://github.com/Wenbo-Zhong/openOLEDShifter
AutoReqProv:    no
Requires:       gnome-shell >= 50
Requires:       gnome-shell < 51
Requires:       gjs
Requires:       xorg-x11-server-Xwayland
Requires:       libX11, libICE, libSM, fontconfig, libglvnd-glx
Requires:       glibc, libgcc, libstdc++, zlib, libicu, openssl-libs

%description
OpenOLEDShifter shifts a static wallpaper on a single display using a GNOME
Shell extension. An Avalonia settings window is available in the app grid.

%install
mkdir -p "%{buildroot}"
cp -a "%{payload}/usr" "%{buildroot}/"

%files
/usr/lib/openoledshifter
/usr/share/applications/org.openoledshifter.Desktop.desktop
/usr/share/icons/hicolor/512x512/apps/org.openoledshifter.Desktop.png
/usr/share/gnome-shell/extensions/openoledshifter@openoledshifter
%dir /usr/share/doc/openoledshifter
%license /usr/share/doc/openoledshifter/copyright
