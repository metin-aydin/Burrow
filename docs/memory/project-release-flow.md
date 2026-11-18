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

**Çeviri kataloğu (2026-11-19):** `lupdate -locations none`; katalogda satır numarası yok, bu yüzden metne
dokunmayan düzenlemeler CI'ın Translations adımını artık düşürmez. Yeni/değişen metinde yine
`cmake --build build --target Burrow_lupdate` çalıştırıp Türkçesini ekle ve `app/i18n`'i commit et.

Windows (2026-11-15, ADR 0010): kullanıcı "JumpDesk'te nasılsa her şey aynı olsun" dedi → imzasız NSIS kurulum
(`-setup.exe`, kullanıcı başına) + taşınabilir zip; `latest.json`'da `windows-x64` = setup.exe; güncelleme SHA-256 +
(uygulama imzalıysa) aynı Authenticode yayıncısı, sessiz kurulum `/S /RELAUNCH /WAITPID=`. Windows sertifikası yok.

Kaynak dal kuralı (2026-11-16, kullanıcı: "master'a sadece development'tan merge edilebilsin"): kural seti PR'ın kaynağını
sınırlayamıyor → `.github/workflows/release-source.yml` (`pull_request_target`, master'daki hali çalışır, PR'dan kod
çalıştırmaz) kaynak `development` değilse düşer. 0.2.2 ile master'a girdi; aynı gün zorunlu kontrollere eklendi.
Değiştirilirse: yeni hali ancak master'a girdikten sonra çalışır (pull_request_target).

Henüz yapılmayan:
Intel paketi; Linux uygulama içi güncellemesi (deb ve AppImage) (JumpDesk'te AppImage de güncelleniyor); Windows kod imzalama.

**Nasıl uygulanır:** Yeni CI işi eklenince dal korumasındaki zorunlu kontrol listesini de güncelle.
Bkz. [[feedback-github-via-mcp]], [[project-github-meta]].
