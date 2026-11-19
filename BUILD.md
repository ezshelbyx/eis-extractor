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

### Source tarball
```
./packaging/make-source-tarball.sh
```
Creates `dist/eis-extractor-<version>.tar.gz` from the current commit.

### RPM
```
rpmbuild -ba packaging/eis-extractor.spec
```
The spec expects `eis-extractor-<version>.tar.gz` in the RPM `SOURCES`
directory.

### Debian/Ubuntu
```
cp -r packaging/debian debian
dpkg-buildpackage -us -uc -b
```
`dpkg-buildpackage` is not installed on Fedora by default; the Debian metadata
is provided for Debian/Ubuntu build hosts.
