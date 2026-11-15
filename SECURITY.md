# Security Policy

Burrow holds the keys to other computers: tunnel credentials, OTP secrets and remote desktop passwords.
Reports that help keep them safe are very welcome.

## Reporting a vulnerability

Please report privately, not in a public issue:

- **GitHub:** [Report a vulnerability](https://github.com/metin-aydin/Burrow/security/advisories/new)
  (Security → Advisories). This reaches the maintainer only.

Include what is affected (version, platform, the connection type: RDP, VNC, a tunnel), how to reproduce it,
and what an attacker gains. You should get a first answer within a week. Once a fix is released, the
advisory is published with credit, unless you prefer otherwise.

## Supported versions

Only the latest release receives fixes; the app updates itself, so staying current is automatic.

## What Burrow does to protect you

- Devices, passwords, OTP secrets and previews live in a local vault encrypted with the master password
  (Argon2id + XChaCha20-Poly1305). Secrets are never written to logs, error messages or test fixtures.
- RDP server certificates are pinned on first use; a changed certificate stops the connection.
- Data from remote computers (RDP, VNC, tunnels) is treated as untrusted and bounds-checked before use.
- Failed sign-ins are never retried automatically.
- Updates are fetched over HTTPS from this repository's releases and installed only when the archive matches
  its SHA-256 and the new app carries a valid Developer ID signature from the same team.
