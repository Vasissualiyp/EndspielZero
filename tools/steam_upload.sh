#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Upload the packaged build to Steam via SteamPipe (M16.2).
#
# Idempotent and scriptable: it fills packaging/steam/*.vdf.in from the environment and
# runs steamcmd, so CI can ship without a hand-copied build. The AppID and depot id only
# exist once the store page does; until then this generates the scripts and prints the
# command, which is still useful for review.
#
#   STEAM_APPID=... STEAM_DEPOTID=... STEAM_USER=... tools/steam_upload.sh [PREFIX]
#
# Credentials come from the environment, never from the repository. With STEAM_PREVIEW=1
# SteamPipe validates the build without publishing it.
set -euo pipefail
cd "$(dirname "$0")/.."

prefix="${1:-dist/endspielzero}"
: "${STEAM_APPID:?set STEAM_APPID (from Steamworks)}"
: "${STEAM_DEPOTID:?set STEAM_DEPOTID (from Steamworks)}"
: "${STEAM_USER:?set STEAM_USER (a Steamworks account with build rights)}"
buildout="${STEAM_BUILDOUT:-build/steam}"
setlive="${STEAM_SETLIVE:-}"   # "" the default branch, "beta", ...
preview="${STEAM_PREVIEW:-0}"  # 1 to validate without uploading
desc="${STEAM_DESC:-}"

test -f "$prefix/bin/endspielzero_gui" || {
  echo "steam_upload: no package at '$prefix'; run tools/package.sh first" >&2
  exit 1
}

mkdir -p "$buildout/generated"
out="$buildout/generated"
# steamcmd resolves relative paths against its own working directory, not ours, so the vdf
# carries absolute ones.
abs() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
prefix_abs="$(abs "$prefix")"
buildout_abs="$(abs "$buildout")"
out_abs="$(abs "$out")"
subst() {
  sed -e "s|@APPID@|$STEAM_APPID|g" \
      -e "s|@DEPOTID@|$STEAM_DEPOTID|g" \
      -e "s|@CONTENT@|$prefix_abs|g" \
      -e "s|@BUILDOUT@|$buildout_abs|g" \
      -e "s|@SETLIVE@|$setlive|g" \
      -e "s|@PREVIEW@|$preview|g" \
      -e "s|@DEPOTVDF@|$out_abs/depot_build.vdf|g" \
      -e "s|@DESC@|$desc|g"
}
subst <packaging/steam/depot_build.vdf.in >"$out/depot_build.vdf"
subst <packaging/steam/app_build.vdf.in >"$out/app_build.vdf"

if ! command -v steamcmd >/dev/null 2>&1; then
  echo "steam_upload: steamcmd not on PATH; wrote $out/app_build.vdf" >&2
  echo "  steamcmd +login $STEAM_USER +run_app_build $out_abs/app_build.vdf +quit" >&2
  exit 0
fi

login=(+login "$STEAM_USER")
if [[ -n "${STEAM_PASSWORD:-}" ]]; then login=(+login "$STEAM_USER" "$STEAM_PASSWORD"); fi
exec steamcmd "${login[@]}" +run_app_build "$out_abs/app_build.vdf" +quit
