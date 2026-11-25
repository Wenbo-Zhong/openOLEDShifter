# SPDX-License-Identifier: GPL-3.0-only
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
Requires:       wxGTK >= 3.2
Requires:       gtk3, glib2, json-glib, libstdc++

%description
OpenOLEDShifter shifts a static wallpaper on a single display using a GNOME
Shell extension. A wxWidgets settings window is available in the app grid.

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
