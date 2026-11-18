# E.I.S EXTRACTOR v0.1.0 — Stable Release Announcement

E.I.S CYBERCORE is proud to announce the first stable release of **E.I.S EXTRACTOR**, a native Linux archive manager built for Fedora KDE Plasma 6 using C++20 and Qt 6 Widgets.

## What it does

- Opens, inspects, tests, and extracts common archive formats: ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, RAR (via 7-Zip), ISO, CAB, AR, CPIO, XAR, LHA, ZSTD, LZ4, and LZMA.
- Integrates directly into Dolphin with six context-menu actions: Extract Here, Extract to New Folder, Extract to..., Extract Selected Archives, Test Archive, and Open Archive Inspector.
- Shows a compact native progress window for Dolphin-launched extractions, including current file, percentage, file counts, speed, elapsed time, ETA, and destination.
- Protects against unsafe archives with path traversal rejection, symlink/hardlink validation, collision handling, and archive-bomb guards.
- Provides a bounded job queue with pause, resume, cancel, and retry controls.
- Includes an archive inspector, integrity lab, activity log, settings, and dark/light themes.

## Installation

From source:

```bash
sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtbase-gui libarchive-devel p7zip
git clone https://github.com/ezshelbyx/eis-extractor.git
cd eis-extractor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
./packaging/install-user.sh
```

Or install the attached Fedora RPM:

```bash
sudo rpm -Uvh eis-extractor-0.1.0-1.fc45.x86_64.rpm
```

Uninstall with `./packaging/uninstall-user.sh` or `sudo rpm -e eis-extractor`.

## Feedback

Bug reports and feature requests are welcome at:
https://github.com/ezshelbyx/eis-extractor/issues

Release page:
https://github.com/ezshelbyx/eis-extractor/releases/tag/v0.1.0
```

## Demo script (30-45 seconds)

1. Open Dolphin and right-click a ZIP archive.
2. Choose **Extract Here** and show the native progress window updating.
3. Close the progress window while extraction continues, then reopen/show completion.
4. Right-click an archive and choose **Test Archive** to demonstrate integrity checking.
5. Show the main application window: overview, inspector, and settings.
6. End on the GitHub release page and call for feedback.