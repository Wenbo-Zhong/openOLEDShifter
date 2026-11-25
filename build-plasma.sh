#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
cmake -S NativeApp -B "$stage/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$stage/build" --parallel
ctest --test-dir "$stage/build" --output-on-failure
"$stage/build/generate_policy" Ubuntu/extension/schemas/org.gnome.shell.extensions.openoledshifter.gschema.xml.in \
    "$stage/schema.xml" "$stage/cycles.js"
bash Ubuntu/packaging/stage-plasma.sh "$stage/cycles.js" "$stage/plugin"
mkdir -p release/ubuntu
(cd "$stage/plugin" && zip -qr "$stage/openoledshifter-plasma.zip" .)
mv "$stage/openoledshifter-plasma.zip" release/ubuntu/
printf 'Ready: release/ubuntu/openoledshifter-plasma.zip\n'
