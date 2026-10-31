#!/bin/zsh
# Builds dist/ShinerCoreFlasher.app (universal) with esptool bundled, signed with SIGN_IDENTITY.
# Usage: scripts/build-app.sh [VERSION]    (VERSION defaults to 0.0.0)
set -euo pipefail
cd "${0:A:h}/.."
version=${1:-0.0.0}
identity=${SIGN_IDENTITY:-"Developer ID Application: Nevyn Bengtsson (M4Q2TE45WT)"}
team=${identity##*\(}; team=${team%\)}
app=dist/ShinerCoreFlasher.app
cache=esptool-cache
esptool_version=v5.6.0
typeset -A esptool_sha256=(
  arm64 08a58829a3f14bb08f2e39f4cad48dcb14b7000384268e7f06689836332d6e25
  amd64 23921776a7df75f2e6f3ca10a604d74db4661e1b8a0ebb89bdddcd2d26f37055
)

die() { print -u2 "build-app: $*"; exit 1 }

[[ $version =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]] || die "VERSION must be major.minor.patch, got $version"
[[ $team =~ '^[A-Z0-9]{10}$' ]] || die "cannot read a team ID from SIGN_IDENTITY ($identity)"

swift build -c release --arch arm64 --arch x86_64
bin=$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)/ShinerCoreFlasher
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/esptool"
cp "$bin" "$app/Contents/MacOS/ShinerCoreFlasher"
sed "s/VERSION/$version/g" app/Info.plist > "$app/Contents/Info.plist"
plutil -lint -s "$app/Contents/Info.plist" app/esptool.entitlements

# Each tarball is verified, then extracted into its own empty directory; only esptool and LICENSE leave it.
mkdir -p $cache
extract=$(mktemp -d -t shinercore-esptool)
trap 'rm -rf "$extract"' EXIT
for arch in arm64 amd64; do
  tarball=$cache/esptool-$esptool_version-macos-$arch.tar.gz
  if [[ ! -f $tarball ]]; then
    curl -fL --retry 3 -o $tarball.part "https://github.com/espressif/esptool/releases/download/$esptool_version/${tarball:t}"
    mv $tarball.part $tarball
  fi
  actual=$(shasum -a 256 $tarball | cut -d' ' -f1)
  [[ $actual == $esptool_sha256[$arch] ]] || die "$tarball has SHA-256 $actual, expected $esptool_sha256[$arch]; move it away to download it again"
  mkdir "$extract/$arch"
  tar -xzf $tarball -C "$extract/$arch"
  mkdir "$app/Contents/Resources/esptool/$arch"
  cp "$extract/$arch/esptool-macos-$arch/esptool" "$app/Contents/Resources/esptool/$arch/esptool"
done
cp "$extract/arm64/esptool-macos-arm64/LICENSE" "$app/Contents/Resources/esptool/LICENSE"

sign=(--force --options runtime --timestamp -s "$identity")
for arch in arm64 amd64; do  # inside out, since signing a bundle does not sign the code nested in it
  codesign "${sign[@]}" --entitlements app/esptool.entitlements "$app/Contents/Resources/esptool/$arch/esptool"
done
codesign "${sign[@]}" "$app"
codesign --verify --deep --strict "$app"

# --deep --strict accepts nested code signed by someone else, which notarization rejects.
find "$app" -type f -print0 | while IFS= read -r -d '' file; do
  [[ "$(file -b "$file")" == Mach-O* ]] || continue
  [[ "$(codesign -dv "$file" 2>&1)" == *$'\nTeamIdentifier='$team$'\n'* ]] || die "$file is not signed by team $team"
done
print "Built $app $version"
