# Security

## Vault cryptography
- Key derivation: Argon2id through OpenSSL's `EVP_KDF` interface with 64 MiB
  memory, 3 iterations and 1 lane, using a random 16-byte salt per vault.
- Encryption: AES-256-GCM with a random 12-byte nonce per write. The 16-byte
  authentication tag is stored with the ciphertext.
- Container: versioned `EISVLT01` header (see ARCHITECTURE.md), written to a
  temporary file in the same directory and atomically renamed with mode 0600.
- The master password is never stored. A wrong password fails GCM
  authentication; no plaintext is returned.
- Encrypted export/import uses the same container. Restore replaces the current
  vault contents only after successful decryption.
- No custom cryptography is implemented; all primitives come from OpenSSL 3/4.

## Password handling
- Archive passwords created through 7-Zip are passed as `-p<password>` command
  arguments. On Linux, another process running as the same user may be able to
  read the process command line. This is an accepted limitation of the 7-Zip
  CLI and is documented in LIMITATIONS.md.
- Vault passwords are copied to the clipboard only on explicit request and the
  clipboard is cleared after 30 seconds on a best-effort basis. Other
  clipboard managers may retain copies.
- The application does not log passwords or vault contents. Error messages from
  backends may include file paths but not credentials.

## Filesystem safety
- Entry paths are normalized and rejected if absolute or containing `..`.
- Symlink and hardlink targets are validated before creation.
- Extraction enforces entry-count, total-size and compression-ratio limits.
- Existing files are never overwritten unless the collision policy explicitly
  requests it; the default is rename.
- ISO mounting is read-only and delegated to udisks2/polkit. The application
  never runs as root and does not call `mount` directly.

## Reporting
Report security issues privately to the publisher rather than opening a public
issue with exploit details.
