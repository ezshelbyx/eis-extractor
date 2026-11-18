# E.I.S EXTRACTOR

Publisher: E.I.S CYBERCORE
Version: 0.1.0

Native Linux archive manager for Fedora KDE Plasma 6. Built with C++20 and Qt 6 Widgets.

## Features
- Formats: ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, RAR (via 7-Zip), ISO, CAB, AR, CPIO, XAR, LHA, ZSTD, LZ4, LZMA.
- Six sections: OVERVIEW, EXTRACTION OPERATIONS, ARCHIVE INSPECTOR, INTEGRITY LAB, ACTIVITY LOG, SETTINGS.
- Safe extraction: path traversal rejection, symlink/hardlink validation, collision handling, no overwrite, bomb guard.
- Bounded job queue with pause/resume/cancel/retry.
- Dolphin service menus (user-level, no system file modification).
- Dark and light themes.

## Build
See BUILD.md.

## Install (one command, Fedora KDE Plasma)
Prerequisites: `gcc-c++`, `cmake`, `ninja-build`, `qt6-qtbase-devel`, `qt6-qtbase-gui`, `libarchive-devel`, `p7zip`.
```bash
git clone https://github.com/ezshelbyx/eis-extractor.git && cd eis-extractor && sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtbase-gui libarchive-devel p7zip && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && ninja -C build && ./packaging/install-user.sh
```

## Uninstall
```
./packaging/uninstall-user.sh
```
