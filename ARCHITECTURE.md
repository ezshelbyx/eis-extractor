# Architecture

## Modules
- `src/core`: shared types, utilities, runtime capability registry.
- `src/archive`: archive listing types and the `ArchiveService` facade.
- `src/backends`: libarchive and 7-Zip backends plus `BackendRegistry`.
- `src/extraction`: extraction options and tasks.
- `src/compression`: archive creation through libarchive or 7-Zip.
- `src/discimage`: image inspection, ISO creation, read-only udisks2 mounts.
- `src/vault`: Argon2id/AES-GCM container, password records, generator.
- `src/verification`: streaming hashes and checksum files.
- `src/queue`: bounded job queue.
- `src/integrity`: archive CRC verification.
- `src/security`: `PathGuard`.
- `src/ui`: pages, main window, progress window.
- `src/widgets`: custom painted HUD widgets.
- `src/settings`, `src/logging`: application settings and logging.

## Capability model
`CapabilityRegistry` probes the installed backends at startup:
- libarchive writer formats (`archive_write_set_format_by_name`) and filters;
- 7-Zip `i` format table, including the extra flags column some rows carry;
- xorriso and udisksctl presence.
Only capabilities that probe successfully are offered for creation or
encryption. The registry report is shown in Settings and by `--capabilities`.

## Engines
- `CompressionEngine` scans sources, builds a format-specific request, writes to
  a hidden temporary file in the destination directory, then renames atomically.
  7-Zip is used for 7Z, encrypted ZIP and multi-volume output; libarchive writes
  other formats.
- `DiscImageEngine` detects ISO/UDF by magic (`CD001`, `BEA01`, `NSR02/03`),
  delegates listing/extraction to the archive service, creates ISO with xorriso
  when available, and mounts only ISO/UDF through
  `udisksctl --no-user-interaction`. Active mounts are recorded in
  `~/.local/share/EIS/eis-archive/mounts.json` and unmounted on quit.
- `HashEngine` streams files through OpenSSL EVP digests and reads/writes
  checksum files with paths resolved relative to the checksum file.
- `PasswordVault` serializes JSON, encrypts it with AES-256-GCM under an
  Argon2id-derived key, and writes a versioned binary container atomically with
  mode 0600. The shared `PasswordVault::instance()` serves the vault page and
  saved-password lookup on the extraction page.

## Vault container
```
offset  size  field
0       8     magic "EISVLT01"
8       4     Argon2 memory KiB        (big-endian)
12      4     Argon2 iterations
16      4     Argon2 lanes
20      4     Argon2 version (0x13 = v1.3)
24      4     salt length (16)
28      16    salt
44      4     nonce length (12)
48      12    nonce
60      ...   ciphertext
end-16  16    AES-GCM tag
```
The entire header (including lengths, salt and nonce) is authenticated as
AES-GCM additional data.
The master password and derived key are never written to disk. GCM
authentication rejects wrong passwords and modified files.

## Safety
`PathGuard` validates every entry path, rejects traversal and absolute paths,
validates symlink/hardlink targets, handles collisions, enforces entry/size
limits and the compression-ratio bomb guard. Archive extraction and creation
are cancellable; creation leaves no partial output after cancellation or error.

## Concurrency
`JobQueue` owns jobs and a `QThreadPool` with configurable concurrency.
Extraction and compression tasks run off the UI thread and emit progress
through queued signals. The UI never blocks on a backend process.
