# E.I.S ARCHIVE

Publisher: E.I.S CYBERCORE
Version: 0.2.0

Native Linux archive, disc image and encrypted password vault manager for
Fedora KDE Plasma 6. Built with C++20 and Qt 6 Widgets. The application never
runs as root and never fakes format support: capabilities are probed at runtime.

## Features

### Archive extraction
- ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, TAR.ZST, TAR.LZ4, TAR.LZMA, 7Z, RAR
  (via 7-Zip), ISO/UDF, CAB, AR, CPIO, XAR, LHA, ZSTD, LZ4, LZMA.
- Path traversal, absolute path, symlink/hardlink and bomb guards.
- Collision policies, include selection, bounded job queue with pause, resume,
  cancel and retry.
- Context-menu extraction with a small native progress window.

### Compression
- Create ZIP, 7Z, TAR and compressed TAR variants, CPIO and single-stream
  GZIP/BZIP2/XZ/ZSTD/LZ4/LZMA when the installed backends support them.
- Compression levels, progress, atomic output (temporary file then rename).
- Encrypted ZIP (AES-256) and 7Z (AES-256, optional header encryption) via
  7-Zip, plus multi-volume 7Z/ZIP when 7-Zip reports support.
- Saved archive passwords can be pulled from the unlocked password vault.

### Disc images
- Inspect ISO 9660/UDF and other images using the archive backend.
- Create ISO images with xorriso.
- Mount ISO/UDF images read-only through udisks2/polkit, and unmount on quit.
  Mounting is ISO-only; the application does not implement FUSE or loop mounts.

### Password vault
- Argon2id (64 MiB, 3 iterations, 1 lane) key derivation and AES-256-GCM
  authenticated encryption from OpenSSL.
- Versioned `EISVLT01` container, atomic 0600 writes, master password never
  stored. Search, categories, favorites, password history, password generator,
  auto-lock, encrypted export/import and restore.
- Archive password records matched by archive path or file name.

### Verification
- Streaming SHA-256, SHA-512, SHA-1 and MD5.
- Generate and verify GNU/BSD-style checksum files.
- Integrity lab still runs archive CRC tests through the backends.

### Interface and integration
- Sections: OVERVIEW, EXTRACTION OPERATIONS, COMPRESSION ENGINE, ARCHIVE
  INSPECTOR, DISC IMAGES, PASSWORD VAULT, FILE VERIFICATION, INTEGRITY LAB,
  ACTIVITY LOG, SETTINGS.
- Black/graphite/navy/cyan/electric-blue/violet HUD theme, dark and light modes.
- Dolphin service menus for extraction, testing, inspection, compression,
  checksum creation and ISO mount/unmount.

## Build
See BUILD.md.

## Install (user level)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
./packaging/install-user.sh
```

## Command line
```
eis-extractor --smoke
eis-extractor --capabilities
eis-extractor --inspect archive.zip
eis-extractor --extract-here archive.tar.gz
eis-extractor --extract-to archive.7z
eis-extractor --test archive.zip
eis-extractor --compress-zip file1 file2
eis-extractor --compress-7z directory/
eis-extractor --checksum file.iso
eis-extractor --checksum-write file.iso
eis-extractor --verify-checksums checksums.sha256
eis-extractor --create-iso directory --iso-output image.iso --iso-label DATA
eis-extractor --mount-image image.iso
eis-extractor --unmount-image image.iso
```

## Known limitations
See LIMITATIONS.md. Security properties and threat model are in SECURITY.md.

## Feedback and bug reports
https://github.com/ezshelbyx/eis-extractor/issues

## Uninstall
```
./packaging/uninstall-user.sh
```
