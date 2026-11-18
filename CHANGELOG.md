# Changelog

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
