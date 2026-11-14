---
name: project-release-flow
description: Release ve otomatik güncelleme, kullanıcının JumpDesk/UPaste akışıyla aynı; gizli değer adları
type: project
---

Kullanıcı 2026-11-14'te GitHub Releases'ten otomatik güncelleme ve development→master merge'ünde otomatik
release istedi; 2026-11-15'te bunların kendi JumpDesk/UPaste repolarındaki gibi yapılmasını istedi.

Uygulanan (ADR 0002): taslak sürüm → platform derlemeleri → `latest.json` + yayımla, hata olursa taslağı sil;
PR açıklaması sürüm notu; uygulama içi güncelleyici 15 sn / 6 saatte bir denetler, sormadan indirmez, SHA-256 +
aynı Developer ID ekip imzası doğrular.

Farklar (bilinçli): dal adı `master` (JumpDesk `main`), sürüm Conventional Commits'ten (JumpDesk patch artırır).

Gizli değerler `release` ortamında, JumpDesk ile aynı adlar: `CSC_LINK`, `CSC_KEY_PASSWORD`, `APPLE_ID`,
`APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`. UPaste hafızasına göre sertifika kullanıcının
`~/Documents/apple-signing` klasöründe; değerleri kullanıcı ekler (Claude parola görmez).

Henüz yapılmayan: GitHub'da `release` ortamı, dal korumaları, gizli değerler (GitHub MCP bekleniyor);
Intel/Ubuntu/Windows paketleri; Touch ID için provisioning profile (release'te Touch ID şimdilik çalışmaz).

**Nasıl uygulanır:** Yeni CI işi eklenince dal korumasındaki zorunlu kontrol listesini de güncelle.
Bkz. [[feedback-github-via-mcp]], [[project-github-meta]].
