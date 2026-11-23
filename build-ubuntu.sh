#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

package=openoledshifter@openoledshifter
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$stage/schemas" release/ubuntu
cp Ubuntu/extension/{extension,translation}.js Ubuntu/extension/metadata.json LICENSE "$stage"

schema=org.gnome.shell.extensions.openoledshifter.gschema.xml
dotnet run --project Ubuntu/Build -c Release -- \
    "Ubuntu/extension/schemas/$schema.in" "$stage/schemas/$schema" "$stage/cycles.js"
glib-compile-schemas --strict "$stage/schemas"
gjs -m "$stage/cycles.js"
gjs -m "$stage/translation.js"
gjs -m Ubuntu/translation-test.js
gnome-extensions pack --extra-source=cycles.js --extra-source=translation.js --extra-source=LICENSE --force --out-dir="$PWD/release/ubuntu" "$stage"
printf 'Ready: release/ubuntu/%s.shell-extension.zip\n' "$package"

dotnet publish Ubuntu/Desktop -c Release -p:UseCurrentRuntimeIdentifier=true --self-contained true -o release/ubuntu/desktop --nologo
printf 'Ready: release/ubuntu/desktop/OpenOLEDShifter\n'
