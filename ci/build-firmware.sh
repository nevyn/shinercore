#!/bin/bash
# Compiles every board and writes a firmware build to OUTDIR: flash pieces per board plus
# manifest.json. The format is documented in docs/firmware-builds.md.
# Usage: ci/build-firmware.sh OUTDIR [extra arduino-cli compile flags, e.g. --libraries DIR]
set -euo pipefail

[[ $# -ge 1 ]] || { echo "usage: $0 OUTDIR [arduino-cli flags...]" >&2; exit 64; }
out=$1; shift
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)

# id|name|fqbn|extra compiler flags
boards=(
    "atom-lite|M5Atom Lite|m5stack:esp32:m5stack_atom|"
    "atom-echo|M5Atom Echo|m5stack:esp32:m5stack_atom|-DARDUINO_M5STACK_ATOM_ECHO"
    "atoms3-lite|M5AtomS3 Lite|m5stack:esp32:m5stack_atoms3|"
    "stickc-plus|M5StickC Plus|m5stack:esp32:m5stack_stickc_plus|"
)

# arduino-cli insists that the sketch directory is named after the .ino.
work=$(mktemp -d "${TMPDIR:-/tmp}/shinerbuild.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/shinercore"
for f in "$repo"/*.ino "$repo"/*.h "$repo"/*.cpp; do ln -s "$f" "$work/shinercore/"; done

table="$work/boards.tsv"
: > "$table"
for entry in "${boards[@]}"; do
    IFS='|' read -r id name fqbn flags <<< "$entry"
    echo "== $id ($fqbn $flags)" >&2
    build="$work/build-$id"
    props=(--build-property "compiler.cpp.extra_flags=$flags")
    arduino-cli compile -b "$fqbn" --build-path "$build" "${props[@]}" "$@" "$work/shinercore" >&2
    arduino-cli compile -b "$fqbn" --show-properties "${props[@]}" "$@" "$work/shinercore" > "$build/props.txt"
    prop() { grep -m1 "^$1=" "$build/props.txt" | cut -d= -f2-; }

    cp "$build/shinercore.ino.bootloader.bin" "$out/$id-bootloader.bin"
    cp "$build/shinercore.ino.partitions.bin" "$out/$id-partitions.bin"
    cp "$(prop runtime.platform.path)/tools/partitions/boot_app0.bin" "$out/$id-boot_app0.bin"
    cp "$build/shinercore.ino.bin" "$out/$id-app.bin"
    cp "$build/shinercore.ino.elf" "$work/$id.elf"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$name" "$(prop build.mcu)" \
        "$(prop build.bootloader_addr)" "$(prop build.flash_mode)" "$(prop build.flash_freq)" \
        "$(prop build.flash_size)" "$(prop upload.speed)" >> "$table"
done

subject=$(git -C "$repo" log -1 --format=%s)
# A GitHub merge commit's subject names the branch; its body's first line is the PR title.
[[ $subject == "Merge pull request "* ]] && subject=$(git -C "$repo" log -1 --format=%b | head -1)

python3 -I - "$out" "$work" "$table" \
    "$(git -C "$repo" rev-parse HEAD)" "$(git -C "$repo" log -1 --format=%cI)" "$subject" <<'EOF'
import hashlib, json, os, sys
out, work, table, commit, date, subject = sys.argv[1:]
ELF_SHA_OFFSET = 0xb0  # platform.txt: elf2image --elf-sha256-offset 0xb0

def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()

boards = []
for line in open(table):
    id, name, chip, bootloader, mode, freq, size, speed = line.rstrip("\n").split("\t")
    elf_sha = sha(os.path.join(work, id + ".elf"))
    app = open(os.path.join(out, id + "-app.bin"), "rb").read()
    embedded = app[ELF_SHA_OFFSET:ELF_SHA_OFFSET + 32].hex()
    if embedded != elf_sha:
        sys.exit(f"{id}: app image carries ELF SHA {embedded} at {ELF_SHA_OFFSET:#x}, expected {elf_sha}")
    parts = []
    for offset, piece in [(int(bootloader, 0), "bootloader"), (0x8000, "partitions"),
                          (0xe000, "boot_app0"), (0x10000, "app")]:
        file = f"{id}-{piece}.bin"
        path = os.path.join(out, file)
        parts.append({"offset": offset, "file": file, "size": os.path.getsize(path), "sha256": sha(path)})
    boards.append({"id": id, "name": name, "chip": chip, "elfSha256": elf_sha,
                   "flashMode": mode, "flashFreq": freq, "flashSize": size,
                   "uploadSpeed": int(speed), "parts": parts})

manifest = {"schema": 1, "commit": commit, "date": date, "subject": subject, "boards": boards}
with open(os.path.join(out, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
EOF

echo "wrote $out/manifest.json" >&2
