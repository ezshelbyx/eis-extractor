# Build

## Dependencies (Fedora)
```
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtbase-gui libarchive-devel p7zip
```

## Configure and build
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

## Tests
```
ctest --test-dir build --output-on-failure
```

## Smoke check
```
QT_QPA_PLATFORM=offscreen ./build/eis-extractor --smoke
```
