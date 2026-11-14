# ADR 0002 — Release akışı ve otomatik güncelleme

- **Durum:** KABUL EDİLDİ (2026-11-14). Kullanıcının Apple Developer Program hesabı var →
  imzalama + notarization yapılacak.
- **Bağlam:** Kullanıcı, uygulamanın GitHub Releases üzerinden kendini güncellemesini ve
  `development` → `master` merge'ünde GitHub Actions'ın otomatik release oluşturmasını istedi.

## Branch modeli
- `development`: varsayılan branch, günlük geliştirme. Feature branch'ler buraya PR ile girer.
- `master`: sadece release. Yalnızca `development` → `master` PR merge'ü ile güncellenir
  (branch protection: doğrudan push yok).
- Commit mesajları **Conventional Commits** (`feat:`, `fix:`, `feat!:` / `BREAKING CHANGE:` …).

## Sürüm numarası (SemVer, otomatik)
`master`'a her merge'de son `vX.Y.Z` tag'inden bu yana gelen commit'lere bakılır:
- `BREAKING CHANGE` / `!` → major, `feat` → minor, diğerleri (`fix`, `perf`…) → patch.
- Release notları aynı commit'lerden otomatik üretilir.

## GitHub Actions — `.github/workflows/release.yml` (tetik: `push` → `master`)
1. Sonraki sürümü hesapla, `vX.Y.Z` tag'ini oluştur.
2. macOS build (universal: arm64 + x86_64): Qt 6 + FreeRDP 3 + Go netcore.
3. **Kod imzalama** (Developer ID Application) → **notarization** (`notarytool`) → `stapler staple`.
4. Paketler: `Burrow-X.Y.Z-mac.dmg` (ilk kurulum) + `Burrow-X.Y.Z-mac.zip` (güncelleme için).
5. Güncelleme paketini **Sparkle EdDSA** anahtarıyla imzala, `appcast.xml` üret.
6. GitHub Release oluştur: notlar + dmg + zip + `appcast.xml` + SHA256 toplamları.
- Ayrıca `development`'a her push/PR'da: build + test (`ci.yml`), release yok.

### Gerekli GitHub secret'ları
`APPLE_CERT_P12`, `APPLE_CERT_PASSWORD`, `APPLE_TEAM_ID`, `APPLE_NOTARY_KEY` (App Store Connect API
anahtarı), `APPLE_NOTARY_KEY_ID`, `APPLE_NOTARY_ISSUER`, `SPARKLE_ED_PRIVATE_KEY`.

## Uygulama içi otomatik güncelleme
- **macOS: Sparkle 2** (macOS'ta standart, MIT). Feed adresi:
  `https://github.com/metin-aydin/Burrow/releases/latest/download/appcast.xml`
  → sunucu gerekmez, her şey GitHub Releases'ten gelir.
- Doğrulama iki katmanlı: Sparkle EdDSA imzası + Apple kod imzası. İkisi tutmazsa güncelleme kurulmaz.
- Davranış: açılışta ve 24 saatte bir kontrol; yeni sürüm bildirimi, "Şimdi kur / Sonra / Bu sürümü
  atla"; ayarlarda otomatik kontrolü kapatma ve "arka planda indir, kapanışta kur" seçeneği.
- **Aktif RDP oturumu varken** güncelleme zorla yeniden başlatma yapmaz; kullanıcıya sorar.
- Sonraki platformlar: Windows → WinSparkle (aynı appcast mantığı); Ubuntu → AppImage +
  AppImageUpdate (GitHub Releases zsync) veya uygulama içi GitHub API kontrolü + bildirim.

## Apple imzalama (çözüldü)
- Kullanıcının Apple Developer Program hesabı var (2026-11-14). Developer ID Application sertifikası
  ve App Store Connect API anahtarı (notarization) oluşturulup GitHub secret'larına eklenecek.
  Secret değerleri hiçbir dosyaya, loga veya sohbete yazılmaz.
