#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
policy=$1
destination=$2
mkdir -p "$destination"
cp -a Ubuntu/plasma-wallpaper/. "$destination/"
cp LICENSE "$destination/"
# QML imports plain JavaScript, while GJS imports an ES module.
sed -e '/^export const translations/,$d' -e 's/^export const /var /' \
    "$policy" >"$destination/contents/ui/policy.js"
sed 's/^export function /function /' Ubuntu/shared/shift-runtime.js >"$destination/contents/ui/shift-runtime.js"
