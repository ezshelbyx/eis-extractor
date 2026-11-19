# Build

## Dependencies (Fedora)
Required:
```
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtbase-gui libarchive-devel openssl-devel
```
Runtime / optional capabilities:
```
sudo dnf install 7zip xorriso udisks2
```
7-Zip enables 7Z, RAR and encrypted ZIP; xorriso enables ISO creation; udisks2
enables read-only ISO mounting. The app probes these at runtime.

## Configure and build
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

## Tests
```
ctest --test-dir build --output-on-failure
```

## Smoke checks
```
QT_QPA_PLATFORM=offscreen ./build/eis-extractor --smoke
./build/eis-extractor --capabilities
./build/eis-extractor --checksum README.md
```

## Package
```
rpmbuild -ba packaging/eis-extractor.spec
```
The spec expects a source tarball named `eis-extractor-0.2.0.tar.gz`.
