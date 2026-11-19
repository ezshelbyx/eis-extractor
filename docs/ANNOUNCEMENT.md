# E.I.S ARCHIVE v0.2.0 — Release Announcement

E.I.S CYBERCORE announces **E.I.S ARCHIVE 0.2.0**, the evolution of E.I.S
EXTRACTOR into a native Linux archive, disc image and encrypted password
manager for Fedora KDE Plasma 6, built with C++20 and Qt 6 Widgets.

## What it does

- Creates ZIP, 7Z, TAR and compressed TAR variants, CPIO and single-stream
  GZIP/BZIP2/XZ/ZSTD/LZ4/LZMA archives when the installed backends support them.
- Encrypts ZIP (AES-256) and 7Z (AES-256, optional encrypted headers) through
  7-Zip, with multi-volume support where 7-Zip provides it.
- Extracts and inspects ZIP, TAR variants, 7Z, RAR (via 7-Zip), ISO/UDF, CAB,
  AR, CPIO, XAR, LHA and more.
- Inspects and creates ISO images, and mounts ISO/UDF read-only through
  udisks2/polkit. The application never runs as root.
- Stores credentials in a password vault protected by Argon2id (64 MiB, 3
  iterations) and AES-256-GCM, with archive-password records, search,
  categories, history, a generator, auto-lock and encrypted backup/restore.
- Verifies SHA-256, SHA-512, SHA-1 and MD5 streaming hashes and generates or
  verifies GNU/BSD-style checksum files.
- Adds COMPRESSION ENGINE, DISC IMAGES, PASSWORD VAULT and FILE VERIFICATION
  pages, plus saved-password lookup during extraction.
- Adds Dolphin actions for compression, SHA-256 checksum creation and ISO
  mount/unmount, alongside the existing extraction actions.
- Keeps the safe-extraction guarantees: traversal and symlink validation,
  collision handling, bomb guards, and a bounded pause/resume/cancel/retry
  queue.

## Installation

From source:

```bash
sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtbase-gui libarchive-devel openssl-devel 7zip xorriso udisks2
git clone https://github.com/ezshelbyx/eis-extractor.git
cd eis-extractor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
./packaging/install-user.sh
```

Packaging metadata is provided for Fedora RPM (`packaging/eis-extractor.spec`)
and Debian/Ubuntu (`packaging/debian`). Create the source tarball with
`./packaging/make-source-tarball.sh`.

Uninstall with `./packaging/uninstall-user.sh` or `sudo rpm -e eis-extractor`.

## Verification

- `ctest --test-dir build --output-on-failure` covers safe extraction, ZIP and
  encrypted 7Z roundtrips, vault confidentiality and wrong-password rejection,
  encrypted backup/restore, checksum verification and ISO creation/inspection.
- `QT_QPA_PLATFORM=offscreen ./build/eis-extractor --smoke`
- `./build/eis-extractor --capabilities` reports probed backend support.

## Feedback

Bug reports and feature requests are welcome at:
https://github.com/ezshelbyx/eis-extractor/issues

Release page:
https://github.com/ezshelbyx/eis-extractor/releases/tag/v0.2.0
