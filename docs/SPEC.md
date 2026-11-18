# Burrow — Gereksinimler (taslak v0.2)

Mac, Windows ve Ubuntu'da çalışan; kendi içinde GlobalProtect veya Tailscale tüneli açıp
kayıtlı bilgisayarlara RDP ile bağlanan masaüstü uygulaması.

## 1. Temel ilke

- **Ek kurulum yok:** openconnect (libopenconnect + TLS/XML bağımlılıkları), FreeRDP, Qt ve Go netcore
  uygulama paketinin içinde gelir. Kullanıcı sadece uygulamayı kurar. (Tek istisna: Ubuntu'daki uzak
  bilgisayara kurulan burrow-agent — ADR 0003.)
- VPN / Tailscale **sistem genelinde açılmaz**. Tünel uygulamanın içinde (userspace) çalışır,
  sadece uygulamanın RDP trafiği tünelden geçer.
- Tüm veriler **lokalde, şifreli** saklanır. Bulut/sunucu yok.

## 2. Uygulama kilidi

- İlk açılışta ana şifre (master password) belirlenir.
- Her açılışta ana şifre sorulur. Şifre olmadan cihaz listesi, kayıtlı şifreler ve
  bağlantılar erişilemez.
- Veri kasası ana şifreden türetilen anahtarla şifrelenir (Argon2id + XChaCha20-Poly1305, ADR 0004); yalnız ana şifreyle açılır (Touch ID 2026-11-15'te kaldırıldı, ADR 0004).
- Ana şifre unutulursa veri kurtarılamaz (bilinçli tercih).

## 3. Tünel profilleri

### 3.1 GlobalProtect
- Alanlar: GP host, kullanıcı adı, şifre, gateway/auth grubu (opsiyonel, ör. `--authgroup`).
- VPN sunucu adı sistem DNS'iyle çözülemezse (split-DNS: kurum domaini iç DNS'e yönlendirilmiş
  olabilir) genel DNS / DNS-over-HTTPS ile çözülür.
- "Şifreyi sakla" seçeneği (kasada şifreli).
- "OTP kullan" checkbox'ı + OTP tipi:
  - **Şifrenin sonuna ekle** (mevcut kurum akışı: şifre+OTP)
  - Ayrı alan / ayrı soru (challenge)
- OTP kaynağı:
  - **Elle gir:** uygulama OTP'yi kullanıcıdan ister.
  - **Otomatik (TOTP):** kullanıcı authenticator QR kodunu verir; uygulama TOTP secret'ını
    kasada şifreli saklar ve OTP'yi kendisi üretip şifreye ekler. **Kullanıcıdan hiç girdi
    istenmez.**
    - QR içe aktarma: ekran görüntüsü/görsel dosyası, kamera (opsiyonel), panodan görsel,
      ya da `otpauth://` metnini yapıştırma / secret'ı elle yazma.
    - Desteklenen formatlar: `otpauth://totp/...` (RFC 6238, Google Authenticator key URI) ve
      Google Authenticator dışa aktarma QR'ı (`otpauth-migration://offline?data=...`);
      birden fazla hesap içeriyorsa kullanıcı hangisi olduğunu seçer.
    - Parametreler: algoritma (SHA1/SHA256/SHA512), basamak (6/8), periyot (30 sn) — URI'dan okunur.
    - Çift OTP akışında ikinci adım için **bir sonraki** TOTP penceresi beklenir (aynı kod
      tekrar kullanılamaz); bekleme süresi ekranda geri sayımla gösterilir.
    - Bilgisayar saati kayıksa uyarı verilir (TOTP saate bağlıdır).
- Kurum akışı: portal girişinde şifre+OTP, ardından gateway girişinde tekrar şifre+**yeni** OTP.
  Uygulama ikinci soruda şifreyi otomatik doldurur, sadece yeni OTP'yi ister.
  (Not: doğrudan gateway'e bağlanarak tek OTP'ye düşürmek denenecek.)
- Motor: `openconnect --protocol=gp` + userspace ağ yığını (ocproxy/tunsocks benzeri).
- Durum göstergesi: bağlanıyor / bağlı / koptu, otomatik yeniden bağlanma.

### 3.2 Tailscale
- Uygulama kendi Tailscale düğümü olarak ağa katılır (`tsnet` / `libtailscale`).
- İlk girişte Tailscale login linki uygulama içinden açılır; düğüm durumu kasada saklanır.
- (İsteğe bağlı) Headscale / özel control server URL'si.

### 3.3 Cloudflare Tunnel (ADR 0007)
- Hedef bilgisayarda `cloudflared` RDP portunu bir Cloudflare Access uygulaması olarak yayınlar (ücretsiz Zero Trust
  planı yeterli; Cloudflare'de bir alan adı gerekir).
- Cihazın adresi Access host adıdır (ör. `rdp.example.com`); opsiyonel bastion ile iç ağ adresleri de kullanılabilir.
- İlk bağlantıda tarayıcıda Access girişi; erişim anahtarları kasada şifreli. Trafik WebSocket üzerinden, yalnız
  uygulamanın içinde (WARP gibi sistem geneli istemci kullanılmaz).

## 4. Cihazlar

- Cihaz kaydı: ad, host/IP, port (varsayılan 3389), kullanıcı adı, şifre (opsiyonel sakla),
  domain, hangi tünel profiliyle bağlanılacağı (GP / Tailscale / doğrudan).
- Lokalde, şifreli kasada saklanır.
- RDP sunucu sertifikası ilk bağlantıda kullanıcıya parmak iziyle gösterilir; onaylanırsa `host:port`a bağlı
  olarak sabitlenir, sonradan değişirse bağlanılmaz. Yalnız NLA/TLS güvenlik katmanları kullanılır.
- Liste, arama, düzenle, sil, kopyala.
- Cihaz türü: Windows / Ubuntu (oturum ve kilit davranışı buna göre değişir).

### 4.1 Cihaz bazında ayarlar (hepsi cihaz ayarları ekranından açılıp kapatılır)
- **Şifreyi sakla:** bilgisayarın (RDP) şifresi kasada şifreli saklanır.
- **Otomatik giriş:** bağlanırken kayıtlı kullanıcı adı/şifre ile otomatik login (NLA/CredSSP);
  şifre kayıtlı değilse bağlanırken sorulur. Uzak taraftaki GNOME/Windows giriş ekranı çıkarsa
  şifre oraya da otomatik yazılır (ayar açıksa).
- **Bağlantı kapanınca kilitle:** pencere kapatıldığında/bağlantı kesildiğinde uzak bilgisayarın
  ekranı kilitli kalır. Davranış:
  - Uygulama bağlantıyı kesmeden hemen önce uzak tarafa kilit kısayolunu gönderir (Windows: Win+L,
    Ubuntu/GNOME: Super+L; aynı tuş kodları). Ubuntu'da ileride burrow-agent ile `loginctl lock-session`.
- **Bağlantı kapanınca:** "Oturumu açık bırak (kilitli)" (şu an tek seçenek) veya "Oturumu kapat (sign out)"
  — oturumu kapatma henüz yok, arayüzde gizli (2026-11-15).
- **Oturum seçimi:** "Mevcut oturuma bağlan" / "Yeni oturum aç" / "Her bağlantıda sor"
  (ayrıntı §5.2).
- **Ses (RDP):** "Bu bilgisayarda çal" (varsayılan) / "Uzak bilgisayarda çal" / "Kapalı" (2026-11-18).

## 5. RDP istemcisi

- Uzak masaüstü **uygulamanın kendi pencerelerinde** çizilir (harici istemci açılmaz).
- Motor: FreeRDP 3 (kütüphane olarak gömülü).
- Klavye, fare, pano, ses (sonraki faz), dinamik çözünürlük.

### 5.0 Pano (kopyala/yapıştır) — çift yönlü
- Kendi bilgisayarında kopyalanan uzak bilgisayara, uzakta kopyalanan kendi bilgisayarına
  yapıştırılabilir (RDP clipboard kanalı, FreeRDP `cliprdr`).
- İçerik türleri: düz metin, zengin metin/HTML, görsel (ekran görüntüsü vb.) ve dosya/klasör — RDP'de iki yönde,
  istemci hangi işletim sisteminde çalışırsa çalışsın (Windows, Linux, macOS). Uzaktan gelen dosyalar bir seferde
  en fazla 2 GB; bir kopyada en fazla 10.000 öğe. macOS hedefleri (VNC) yalnızca düz metin taşır: standart RFB'de
  dosya aktarımı yok.
- Cihaz ayarı: pano paylaşımı açık / kapalı / sadece yerelden uzağa / sadece uzaktan yerele.
- Pano içeriği loglanmaz, diske yazılmaz. İstisna: uzaktan kopyalanan dosyalar yapıştırılabilmek için oturuma ait
  özel önbellek klasörüne iner ve oturum kapanınca silinir.

### 5.0.1 Klavye kısayol eşleme (Mac)
- Mac'te Cmd+C/V/X/Z/A/S/F… gibi kısayollar uzak bilgisayara Ctrl+C/V/X/Z/A/S/F… olarak gönderilir;
  kullanıcı Mac alışkanlığıyla (Cmd) çalışır, uzak taraf standart Ctrl kısayolunu alır.
- Modlar (cihaz bazında, ayarlardan):
  - **Kısayolları çevir** (varsayılan): sadece yaygın kısayollarda Cmd → Ctrl.
  - **Cmd ↔ Ctrl tamamen yer değiştir.**
  - **Kapalı:** tuşlar olduğu gibi gönderilir (Cmd → Windows/Super tuşu).
- Ek eşlemeler: Option → Alt, isteğe bağlı özel eşleme tablosu (ör. Cmd+C → Ctrl+Shift+C, Linux
  terminali için).
- Cmd+Tab, Cmd+Space gibi macOS sistem kısayollarının uzağa gönderilmesi opsiyonel (tam ekranda);
  bunun için macOS Erişilebilirlik izni gerekir.

### 5.1 Çoklu monitör
- Tek RDP oturumu, uzak tarafta N monitörlü sanal masaüstü (RDP multimon).
- Her uzak monitör bir **sekme** olarak görünür.
- Sekme ayrılıp ayrı pencereye alınabilir ve istenen fiziksel monitöre taşınabilir;
  geri sekmeye birleştirilebilir.
- **Son düzen cihaz bazında hatırlanır** (sekme/pencere, hangi fiziksel monitör, tam ekran,
  çözünürlük) ve sonraki bağlantıda aynen uygulanır.

### 5.2 Oturum seçimi (mevcut oturum / yeni oturum)
- Sorun: Ubuntu'ya uzaktan bağlanınca işyerindeki açık oturum kapanıyor (veya tersi); en son
  hangisiyle bağlanıldıysa diğerindeki tüm uygulamalar kapanıyor.
- Hedef: Windows'taki gibi "mevcut oturuma bağlan" veya "yeni oturum aç" seçimi; bağlantı
  kesilince oturum kapanmadan beklemeli, tekrar bağlanınca kaldığı yerden devam etmeli.
- Kök neden: GNOME kullanıcı başına tek grafik oturuma izin veriyor; davranışı sunucu belirliyor.
  Ayrıntı: `docs/research/2026-11-14-ubuntu-rdp-sessions.md`.
- İstemci, Ubuntu cihazı için iki profil sunar:
  - **Mevcut oturuma bağlan** → GNOME Desktop Sharing (port 3390).
  - **Yeni / kalıcı oturum** → GNOME Remote Login (port 3389). Server Redirection + RDSTLS desteklenir.
- Remote Login'e bağlanmadan önce uyarı: aynı kullanıcı yerelde açıksa o oturum kapanabilir.
- Kilitli konsol hatası anlaşılır mesajla gösterilir ("yerelde kilidi aç veya yeni oturumu kullan").
- Uygulamayla birlikte Ubuntu tarafı için kurulum betiği gelir (iki modu açma, portlar, güvenlik duvarı).
- **Tüm Ubuntu sürümleriyle uyumlu olmalı** (kullanıcı yükseltme yapamıyor; 2026-11-14).
- Tasarım (KABUL EDİLDİ, ADR 0003; teknik doğrulama prototipte):
  - **Mevcut oturum (tüm sürümler, ana mod):** GNOME Desktop Sharing + Ubuntu'da küçük bir
    **burrow-agent** (systemd user servisi):
    - Bağlanmadan önce oturumun kilidini açar (`loginctl unlock-session`) → kilitli ekran sorunu çözülür.
    - Bağlantı bitince (normal kapanış veya kopma) oturumu kilitler (`loginctl lock-session`).
    - Bağlıyken ofisteki monitörü kapatmayı dener (Mutter PowerSaveMode) — çalışıp çalışmadığı
      test edilecek.
    - Agent ile iletişim tünel üzerinden, kimlik doğrulamalı (SSH anahtarı veya agent'a özel anahtar).
  - **Ayrı kalıcı oturum (opsiyonel):**
    - Ubuntu 26.04+ (GNOME 47+): GNOME Remote Login (port 3389), kalıcı.
    - Ubuntu 22.04 / 24.04: xrdp (Xorg), ayrı kullanıcı hesabıyla; kopunca kaldığı yerden devam.
  - Kurulum betiği Ubuntu sürümünü algılar, uygun modları ve agent'ı kurar.

## 6. Otomatik güncelleme ve release

- Uygulama yeni sürümleri **GitHub Releases**'ten kontrol eder ve kendini günceller
  (macOS: Sparkle 2; ayrıntı: `docs/decisions/0002-release-and-auto-update.md`).
- Açılışta + 24 saatte bir kontrol; "Şimdi kur / Sonra / Atla"; ayarlardan kapatılabilir.
- Aktif RDP oturumu varken zorla yeniden başlatma yok.
- Güncellemeler EdDSA + Apple kod imzasıyla doğrulanır.
- `development` → `master` merge'ünde GitHub Actions otomatik olarak sürüm numarasını belirler
  (Conventional Commits → SemVer), build/imza/notarization yapar ve GitHub Release oluşturur.

## 7. Açık konular

- [ ] GP sunucusunda HIP kontrolü var mı? (openconnect ile ön test)
- [ ] Doğrudan gateway girişi tek OTP ile çalışıyor mu?
- [ ] Teknoloji yığını kesinleştirme (bkz. aşağı)
- [x] Ses çıkışı (RDP): 2026-11-18. Mikrofon, sürücü (klasör) ve yazıcı yönlendirme: henüz yok

## 8. Teknoloji (KARAR: docs/decisions/0001-tech-stack.md)

- **Qt 6 (C++)**: arayüz + çoklu native pencere + monitör yönetimi
- **FreeRDP 3**: RDP motoru (multimon, GFX/H.264; macOS'ta VideoToolbox)
- **Go netcore** (c-shared): tsnet (Tailscale) + libopenconnect (GlobalProtect) + gVisor netstack
- **Kasa**: Go netcore içinde şifreli dosya (ADR 0004)
- Açık kaynak. Platform sırası: macOS → Ubuntu → Windows.
