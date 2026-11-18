# Contributing

1. Fork the repository.
2. Create a feature branch.
3. Build and run tests:
   ```
   cmake -S . -B build -G Ninja
   ninja -C build
   ctest --test-dir build
   ```
4. Submit a pull request.

Please follow existing code style and keep changes minimal.
