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
`APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`. İmza malzemesi 2026-11-15'te kullanıcının
`~/Downloads/apple-signing-*.zip` arşivinden secret'lara yüklendi, arşiv sonra silindi (aslı kullanıcıda): Developer ID Application, ekip `JTRUY8Y2ZF`, sertifika 2027-11-15'te
bitiyor (yenilemeden önce release durur). Değerleri kullanıcı ekler (Claude parola/anahtar girmez).

Yapıldı (2026-11-15): `release` ortamı (yalnız `master`) ve 5 secret'ın hepsi. Parolalı olanları kullanıcı
`.github/scripts/set-release-secrets.sh` ile ekledi; sertifika yenilenince aynı betik kullanılır.

`master` dalı ve kural setleri 2026-11-15'te kuruldu. İlk release v0.1.0 aynı gün çıktı; imza + notarization
CI'da doğrulandı. Linux AppImage release'te var ama `latest.json`'da yok (Linux'ta uygulama içi güncelleme yok).

**Dikkat:** Başka bir oturum kaynak dosyalarda satır kaydırırsa CI'ın Translations adımı düşer; PR'dan önce
`cmake --build build --target Burrow_lupdate` çalıştırıp `app/i18n`'i commit et.

Henüz yapılmayan:
Intel/Ubuntu/Windows paketleri.

**Nasıl uygulanır:** Yeni CI işi eklenince dal korumasındaki zorunlu kontrol listesini de güncelle.
Bkz. [[feedback-github-via-mcp]], [[project-github-meta]].
