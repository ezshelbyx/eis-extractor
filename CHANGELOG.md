# Changelog

## [0.2.0] - 2026-11-20
### Added
- Runtime capability registry for archive formats and optional system tools.
- Compression engine: ZIP, 7Z, TAR variants, CPIO and single-stream compressors,
  compression levels, progress, atomic output, encryption and multi-volume
  support where 7-Zip provides it.
- Encrypted password vault (`EISVLT01`): Argon2id key derivation, AES-256-GCM,
  atomic 0600 writes, entries, archive passwords, search, categories, favorites,
  history, password generator, auto-lock, encrypted export/import and restore.
- Streaming SHA-256/SHA-512/SHA-1/MD5 verification plus checksum-file
  generation and verification.
- Disc image engine: ISO/UDF inspection, xorriso ISO creation, read-only
  udisks2 mounting and unmount-on-quit.
- New UI pages: compression, disc images, password vault, file verification,
  and vault-backed saved passwords on the extraction page.
- CLI operations: `--capabilities`, `--compress-zip`, `--compress-7z`,
  `--checksum`, `--checksum-write`, `--verify-checksums`, `--create-iso`,
  `--mount-image`, `--unmount-image`.
- Dolphin actions for compression, SHA-256 checksum files, ISO mount and unmount.
- Tests for ZIP/7Z roundtrips, encrypted 7Z, vault confidentiality and wrong
  passwords, checksum verification and ISO creation/inspection.

### Fixed
- libarchive TAR writer capability was never detected (`tar` vs `pax`).
- 7-Zip format-table parsing missed rows with an extra flags column, disabling
  7Z creation and encryption.
- 7-Zip extraction stripped all permissions because `QFile::Permissions` was
  masked as a POSIX mode. Real `stat` modes are now preserved.
- File > Open archive now actually opens the selected archive.

## [0.1.0] - 2026-11-18
### Added
- Initial stable release of E.I.S EXTRACTOR.
- Native Linux archive manager (C++20 + Qt 6 Widgets).
- Support for ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, RAR (via 7-Zip), ISO, CAB, AR, CPIO, XAR, LHA, ZSTD, LZ4, LZMA.
- Six-section UI: overview, extraction operations, archive inspector, integrity lab, activity log, settings.
- Safe extraction with path traversal rejection, symlink/hardlink validation, collision handling, and bomb guard.
- Bounded job queue with pause/resume/cancel/retry.
- Dolphin service-menu integration for all six context-menu actions.
- Native Qt progress window for Dolphin-launched extractions.
- Fedora RPM packaging and user-level installer/uninstaller.

### Known limitations
- Tested on Fedora 45 KDE Plasma 6 (x86_64).
- RAR extraction requires the 7-Zip backend.
- Password-protected archive support depends on backend capabilities.
- Dolphin may need a restart before service-menu actions appear.
