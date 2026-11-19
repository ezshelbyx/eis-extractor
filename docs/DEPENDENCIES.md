# Dependencies

## Build (Fedora)
- gcc-c++
- cmake
- ninja-build
- qt6-qtbase-devel
- qt6-qtbase-gui
- libarchive-devel
- openssl-devel
- pkg-config
- python3 and zip (tests)

## Runtime
- qt6-qtbase, qt6-qtbase-gui
- libarchive
- openssl-libs (Argon2id provider required for the vault)

## Optional capability providers
- 7zip: 7Z creation, RAR extraction, encrypted ZIP, multi-volume output
- xorriso: ISO creation
- udisks2: read-only ISO/UDF mounting through polkit

The application probes optional tools at startup and disables unavailable
actions rather than assuming support.
