#!/bin/zsh
# Builds, signs, notarizes and staples ShinerCore Flasher into dist/ShinerCoreFlasher.zip, then
# prints the command that publishes it. Publishing stays a manual step.
# Usage: ./release.sh VERSION
set -euo pipefail
cd "${0:A:h}"

identity=${SIGN_IDENTITY:-"Developer ID Application: Nevyn Bengtsson (M4Q2TE45WT)"}
team=${identity##*\(}; team=${team%\)}
profile=${NOTARY_PROFILE:-dibs-notary}
keychain=${NOTARY_KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}
app=dist/ShinerCoreFlasher.app
zip=dist/ShinerCoreFlasher.zip

die() { print -u2 "release: $*"; exit 1 }
zip_app() {
  rm -f $zip
  xattr -cr "$app"
  ditto -c -k --keepParent --norsrc "$app" $zip
  # AppleDouble entries make Gatekeeper reject the app when something other than Archive Utility unzips it.
  [[ "$(unzip -l $zip)" != *"/._"* ]] || die "$zip contains AppleDouble (._) entries"
}

[[ $# -eq 1 && $1 =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]] || die "usage: ./release.sh VERSION (major.minor.patch)"
version=$1
[[ -z "$(git status --porcelain)" ]] || die "working tree is not clean"

SIGN_IDENTITY=$identity scripts/build-app.sh $version
built=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")
[[ $built == $version ]] || die "built app reports version $built, expected $version"
[[ "$(codesign -dv "$app" 2>&1)" == *$'\nTeamIdentifier='$team$'\n'* ]] || die "signature lacks TeamIdentifier=$team"

zip_app
submission=$(xcrun notarytool submit $zip --keychain-profile $profile --keychain $keychain --wait)
print -r -- $submission
[[ $submission == *"status: Accepted"* ]] || die "notarization was not accepted; see xcrun notarytool log <id> --keychain-profile $profile --keychain $keychain"
xcrun stapler staple "$app"
xcrun stapler validate "$app"
spctl --assess --type execute -vv "$app"
zip_app  # again, now with the stapled ticket
(cd dist && shasum -a 256 ${zip:t} > ${zip:t}.sha256)

branch=$(git branch --show-current)
[[ $branch == main ]] || print -u2 "release: note: $branch is not main; release from main so that the tag points at a main commit"
print "\nNotarized $PWD/$zip. To publish it:\n"
print -r -- "gh release create flasher-v$version $PWD/$zip $PWD/$zip.sha256 --repo nevyn/shinercore --target $(git rev-parse HEAD) --title \"ShinerCore Flasher $version\" --latest --generate-notes"
