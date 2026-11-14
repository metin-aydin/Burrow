# ADR 0002 — Release akışı ve otomatik güncelleme

- **Durum:** KABUL EDİLDİ (2026-11-14), **güncellendi 2026-11-15**: kullanıcının isteğiyle akış, kendi
  JumpDesk/UPaste uygulamalarındakiyle aynı yapıya getirildi; Sparkle yerine uygulama içi güncelleyici.
- Kullanıcının Apple Developer Program hesabı var → imzalama + notarization zorunlu.

## Branch modeli
- `development`: varsayılan branch, günlük geliştirme (doğrudan push serbest, silme/zorla push kapalı).
- `master`: sadece release. Yalnızca `development` → `master` PR merge'ü ile güncellenir (PR zorunlu, CI
  kontrolleri zorunlu, zorla push/silme kapalı). `v*` etiketleri silinemez/güncellenemez.
  (JumpDesk/UPaste'te dal adı `main`; Burrow'da CLAUDE.md gereği `master`.)
- PR açıklaması sürüm notudur → `.github/pull_request_template.md` (New / Changed / Fixed).

## Sürüm numarası
- Conventional Commits'ten otomatik (`.github/scripts/next-version.sh`): `feat` → minor, diğerleri → patch,
  `!`/`BREAKING CHANGE` → major (0.x'te minor). (JumpDesk her sürümde patch artırır; Burrow CLAUDE.md gereği
  commit türünden hesaplar.)

## `.github/workflows/release.yml` (tetik: `master`'a push)
1. **prepare:** sürümü hesapla; birleşen PR'ın açıklamasını `release-notes.py` ile temizleyip sürüm notu yap
   (boşsa son etiketten bu yana commit başlıkları + "Full changelog" bağlantısı); `vX.Y.Z` taslak sürüm oluştur
   (yarım kalmış eski taslak silinir).
2. **build** (matris, `release` ortamında): macOS arm64 → Qt + FreeRDP + Go netcore derle, testler,
   `macdeployqt`, Developer ID imza (hardened runtime, `entitlements.mac.plist`), dmg, notarize + staple,
   güncelleme paketi `Burrow-X.Y.Z-mac-arm64.zip`, SHA-256'lı manifest parçası. Intel/Ubuntu/Windows
   portları geldikçe matrise eklenir.
3. **publish:** parçaları `latest.json`'da birleştir (`version`, `notes`, `assets{platform:{name,sha256,size}}`),
   sürüme yükle, taslağı yayımla (`--latest`).
4. **discard:** herhangi bir adım başarısızsa taslak sürüm ve etiketi silinir.
- `ci.yml`: her push/PR'da Go testleri, macOS derleme + Qt testleri + çeviri kontrolü, sürüm notu betiği testi.

### Gerekli gizli değerler (`release` ortamı; JumpDesk ile aynı adlar)
`CSC_LINK` (Developer ID Application .p12, base64), `CSC_KEY_PASSWORD`, `APPLE_ID`,
`APPLE_APP_SPECIFIC_PASSWORD` (notarization), `APPLE_TEAM_ID`. Değerler hiçbir dosyaya, loga veya sohbete
yazılmaz; kullanıcı kendisi ekler. Eksikse macOS derlemesi hata verip yayını durdurur.

## Uygulama içi güncelleme (`app/src/app/Updater.*`)
- Manifest: `https://github.com/metin-aydin/Burrow/releases/latest/download/latest.json` (sunucu yok).
  Test için `BURROW_UPDATE_URL`.
- Davranış (JumpDesk ile aynı): açılıştan 15 sn sonra ve 6 saatte bir denetim (yalnız paketli sürüm, ayarlardan
  kapatılabilir); yeni sürümde bildirim + galeri başlığında "Güncelleme X" çipi; **sormadan indirmez**;
  Ayarlar → Güncellemeler'den İndir → Yeniden Başlat Ve Güncelle.
- Doğrulama: indirilen paketin SHA-256'sı manifestle eşleşmeli; macOS'ta açılan uygulama `codesign --verify
  --deep --strict`'ten geçmeli ve **çalışan uygulamayla aynı Developer ID ekip kimliğini** taşımalı.
  İmzasız (geliştirme) kopya kendini güncellemez.
- Kurulum: ayrık bir yardımcı betik Burrow kapanınca eski paketi yenisiyle değiştirir (başarısızsa eskisini geri
  koyar) ve yeniden açar. Aktif RDP oturumu varken kurulum sorulmadan yapılmaz (RDP motoruyla birlikte).
- Ubuntu (AppImage/deb) ve Windows (kurulum) güncellemesi portlarla birlikte eklenecek.

## Reddedilen: Sparkle 2
macOS'ta standart ama yalnız macOS; ayrı EdDSA anahtarı ve appcast gerektiriyor. Kullanıcı üç platformda
JumpDesk/UPaste ile aynı davranışı istediği için tek, platformlar arası güncelleyici seçildi.
