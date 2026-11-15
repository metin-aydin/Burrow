# GitHub repository settings

What the repository should have on GitHub itself (not in files). Applied through the GitHub MCP
(CLAUDE.md); kept here so the next maintainer can check them. Release flow: ADR 0002.

## About

- **Description:** Remote desktop through a private tunnel: RDP to Windows and Ubuntu, Screen Sharing to
  Macs, with GlobalProtect, Tailscale and Cloudflare built in. Open source, macOS.
- **Website:** https://github.com/metin-aydin/Burrow/releases/latest
- **Topics:** `rdp`, `remote-desktop`, `vnc`, `screen-sharing`, `freerdp`, `globalprotect`, `openconnect`,
  `tailscale`, `cloudflare-access`, `macos`, `qt6`, `qml`, `golang`, `vpn`, `totp`
- Releases on, Packages off, Wiki off, Discussions optional, Issues on.

## Branches

- **Default branch:** `development`.
- **`development`** (ruleset): block force pushes and deletion; direct pushes allowed.
- **`master`** (ruleset): changes only through a pull request from `development`; required status checks
  `Go netcore tests`, `macOS build`, `Release scripts` (CI); block force pushes and deletion.
- **Tags `v*`** (ruleset): block deletion and updates (published versions never move).
- Allow merge commits for `development` → `master` (the merged PR's description becomes the release notes).

## Actions

- Workflow permissions: read-only `GITHUB_TOKEN` by default (jobs ask for `contents: write` themselves);
  Actions may not approve pull requests.
- **Environment `release`:** deployment branches limited to `master`. Secrets (added by the maintainer only,
  never written anywhere else): `CSC_LINK` (Developer ID Application .p12, base64), `CSC_KEY_PASSWORD`,
  `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`. Without them the release stops before
  publishing an unsigned build. The maintainer stores them with `.github/scripts/set-release-secrets.sh`.

## Security

- Private vulnerability reporting: on (SECURITY.md points to it).
- Dependabot alerts and security updates: on (`.github/dependabot.yml` adds weekly version updates).
- Secret scanning and push protection: on.
- Code scanning: the `CodeQL` workflow (Go and Actions), also on pull requests; add its results as a
  required check on `master`.
