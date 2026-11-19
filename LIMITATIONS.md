# Limitations

## Platform
- Developed and tested on Fedora 45 KDE Plasma 6, x86_64. Other distributions
  are not tested.
- Qt 6.4+ and OpenSSL 3/4 with the Argon2 KDF provider are required. Vault
  creation fails with a clear error if the provider is unavailable.
- Optional tools are probed at runtime: 7-Zip for 7Z/RAR/encrypted ZIP,
  xorriso for ISO creation, udisks2 for mounting. Missing tools disable the
  corresponding actions instead of faking support.
- Dolphin may need a restart before new service-menu actions appear.

## Formats
- RAR and creation of 7Z/encrypted ZIP depend on the installed 7-Zip.
- Encrypted ZIP uses 7-Zip AES-256; some third-party ZIP tools cannot read it.
- Multi-volume output is available only where 7-Zip supports it.
- ISO/UDF mounting is read-only and ISO-only. IMG/BIN/CUE files can be
  inspected when a backend recognizes their filesystem, but are never mounted.
- ISO creation uses xorriso defaults; bootable or hybrid images are not
  configured by the application.

## Security
- 7-Zip receives archive passwords as `-p<password>` on the command line. A
  process running as the same user can potentially read the argument list.
- Clipboard clearing after copying a vault password is best effort; clipboard
  managers may keep history.
- Vault export writes another encrypted container; protect it like the original
  vault. There is no key escrow or password recovery: a lost master password
  means the vault is unrecoverable.
- The vault protects data at rest. It does not defend against a compromised
  desktop session, keyloggers, or an attacker who can read process memory while
  the vault is unlocked.
- SHA-1 and MD5 are offered for compatibility with existing checksum files and
  are not collision-resistant; use SHA-256 or SHA-512 for new work.

## UI and operations
- Compression launched from Dolphin runs without a progress window; the
  in-app COMPRESSION ENGINE page shows progress and results.
- The compression page accepts files and directories but does not expose
  include/exclude filters.
- Archive creation does not preserve extended attributes or ACLs; permissions
  are preserved on extraction where the backend provides them.
- The activity log is in-memory for the session unless the logger writes to its
  standard location.
