---
name: project-release-flow
description: Otomatik güncelleme ve development→master otomatik release gereksinimi
type: project
---

Kullanıcı 2026-11-14'te istedi:
- Uygulama GitHub Releases'ten güncelleme kontrol edip kendini güncellemeli.
- `development` → `master` merge'ünde GitHub Actions otomatik release oluşturmalı.

Karar: ADR `docs/decisions/0002-release-and-auto-update.md` — Sparkle 2 (macOS), Conventional
Commits → otomatik SemVer, imza + notarization + EdDSA imzalı appcast GitHub Release'e eklenir.

Kullanıcının Apple Developer Program hesabı **var** (2026-11-14) → imzalama + notarization yapılacak.
Sertifika/API anahtarı repo kurulunca GitHub secret'larına eklenecek (kullanıcı ekler).

**Nasıl uygulanır:** Repo kurulunca varsayılan branch `development` yapılır, `master` korunur,
`ci.yml` + `release.yml` eklenir. Bkz. [[project-stack-status]], [[feedback-github-via-mcp]].
