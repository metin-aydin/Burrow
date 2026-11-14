# İş Günlüğü

En yeni kayıt en üstte.

## 2026-11-14

- **Proje iskeleti kuruldu** (git: `development` branch, henüz commit yok):
  - `netcore/` (Go): loopback forwarder (`internal/forward`), TOTP + otpauth/migration parse
    (`internal/totp`), C API (`netcore.go`, c-archive). `go test -race` geçti (RFC 6238 vektörleri dahil).
  - `app/` (Qt 6 C++): ana pencere iskeleti, NetCore wrapper, `RdpEngine` arayüzü, `FreeRdpEngine` iskeleti
    (FreeRDP 3.33.1 ile bağlanıyor). Derleniyor, açılıyor (offscreen 4 sn çökmeden).
  - `.github/`: `ci.yml`, `release.yml` (imza/notarization secret varsa çalışır), `next-version.sh`
    (geçici repoda test edildi). Sparkle appcast henüz yok.
  - Bu Mac'te derleme notu: Command Line Tools SDK'sı bozuk (tapi hatası) → Xcode SDK'sı ve clang ile
    yapılandır (`docs/memory/reference-local-build.md`).
- Kullanıcı Ubuntu'ya agent kurulabileceğini onayladı → ADR 0003 KABUL EDİLDİ. Açık işler:
  GitHub reposu (MCP bağlantısı bekleniyor), GP ön testi, proje iskeleti, agent doğrulama testleri.
- Kullanıcı: Ubuntu yükseltilemez, tüm sürümlere uyumlu olmalı. SPEC §5.2'ye öneri: Desktop
  Sharing + Ubuntu tarafında gremote-agent (bağlanınca kilit aç, kopunca kilitle); opsiyonel ayrı
  kalıcı oturum (26.04+: Remote Login, 22.04/24.04: xrdp + ayrı kullanıcı). Agent kurulumunun
  iş bilgisayarında mümkün olup olmadığı soruldu.
- SPEC §5.0: çift yönlü pano (metin, HTML, görsel; dosya sonraki faz). §5.0.1: Mac kısayol
  eşleme (Cmd→Ctrl çevirme, tam yer değiştirme, özel eşleme tablosu).
- Ubuntu oturum araştırması bitti → `docs/research/2026-11-14-ubuntu-rdp-sessions.md`, SPEC §5.2
  güncellendi. Kullanıcı Ubuntu 24.04'te; Windows benzeri kalıcı oturum için 26.04 (GNOME 50)
  gerekiyor. Kullanıcıdan seçim bekleniyor.
- Kullanıcının Apple Developer hesabı var → ADR 0002 imzalama/notarization kesinleşti.
- SPEC §4.1: cihaz bazında şifre sakla, otomatik giriş, kapanınca kilitle, oturum seçimi.
  §5.2: Ubuntu'da oturumun kapanması sorunu (araştırma sürüyor).
- Otomatik güncelleme + otomatik release gereksinimi eklendi: SPEC §6, ADR 0002, CLAUDE.md'ye
  git akışı (development/master, Conventional Commits) ve "GitHub sadece MCP ile" kuralı.
  GitHub MCP araçları bu oturumda hâlâ yüklü değil (giriş hatası) → repo bekliyor.
- Lisans: **MIT** seçildi, `LICENSE` eklendi.
- **Karar:** FreeRDP 3 + Qt 6, Go netcore, açık kaynak, önce macOS. ADR 0001 → KABUL EDİLDİ.
  Kalan: proje lisansı seçimi, GP ön testi, GitHub reposu, iskelet kurulumu.
- Araştırmalar tamamlandı → `docs/research/` altına yazıldı. Öneri ADR'ı:
  `docs/decisions/0001-tech-stack.md` (durum: ÖNERİ). Ana bulgu: Tailscale için Go zorunlu
  → ağ katmanı Go netcore; önerilen UI/RDP: Qt 6 + FreeRDP 3.
- Teknoloji yığını araştırması başlatıldı (RDP motoru + UI; gömülü GP/Tailscale tünelleri; TOTP).
- Proje içi hafıza yapısı kuruldu: `CLAUDE.md` (KURAL 0), `AGENTS.md`, `docs/memory/`,
  `docs/decisions/`. `SPEC.md` → `docs/SPEC.md` taşındı.
- SPEC'e TOTP desteği eklendi (authenticator QR kodu ile secret içe aktarma, otomatik OTP).
- Gereksinimler `docs/SPEC.md` v0.1 olarak yazıldı: uygulama kilidi, GP + OTP, Tailscale,
  cihaz kayıtları, uygulama içi RDP, sekme/pencere çoklu monitör, düzenin cihaz bazında
  hatırlanması.
- GitHub reposu oluşturma denendi: `gh` kurulu değil, GitHub MCP girişi "Couldn't start
  sign-in" hatası verdi. Repo henüz yok.
