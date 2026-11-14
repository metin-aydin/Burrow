---
name: project-gp-auth-flow
description: Kullanıcının kurumundaki GlobalProtect giriş akışı, OTP davranışı ve DNS tuzağı
type: project
---

Kurumda GlobalProtect kullanılıyor. Kullanıcının mevcut çözümü (repo dışında, kendi Mac'inde):
openconnect `--protocol=gp` + Python betiği, şifre ve TOTP secret'ı macOS Keychain'de, pyotp ile
otomatik OTP; sistem genelinde tun açtığı için sudo istiyor. Yani **openconnect kurum sunucusunda
zaten çalışıyor** → resmi olmayan istemci kabul ediliyor, HIP betiği olmadan bağlanıyor.

Bulunan ayrıntılar (2026-11-14):
- **authgroup gerekli** (gateway seçimi, `--authgroup=<GRUP>`). → SPEC'te GP profiline "gateway/grup"
  alanı eklenmeli.
- **Split-DNS tuzağı:** Mac'te `/etc/resolver/<kurum-domain>` dosyası kurum domainini iç DNS'e
  yönlendiriyor; VPN yokken iç DNS'e ulaşılamadığı için VPN sunucu adı çözülemiyor. Mevcut betik IP'yi
  genel DNS'ten (1.1.1.1 / 8.8.8.8) alıp `--resolve=host:ip` veriyor. → netcore VPN sunucusunu
  sistem DNS'i başarısız olursa genel DNS/DoH ile kendisi çözmeli.
- **TOTP algoritması SHA512** (6 hane, 30 sn) — varsayılan SHA1 değil! SHA1 ile üretilen kodlar
  `auth-failed` (HTTP 512) alır. Uygulamada algoritma QR/URI'dan okunmalı, elle girişte seçilebilmeli.
- **Çift OTP'nin gerçek mekanizması (2026-11-14 log'dan):** portal (`getconfig.esp`) şifre+OTP'yi kabul
  ediyor; openconnect aynı kimlik bilgisini gateway'e (`ssl-vpn/login.esp`) yeniden gönderiyor, OTP
  kullanılmış olduğu için gateway **bir kez `auth-failed` (HTTP 512) veriyor** ve openconnect tekrar
  "Enter login credentials" soruyor; şifre + bir sonraki OTP ile giriş tamamlanıyor. Yani bu tek gateway
  reddi normal akışın parçası (kullanıcının betiği de her seferinde yaşıyor, kilitlemiyor).
  → Test betiği/uygulama: portal reddi = dur; gateway'de ilk red = beklenen; ikinci red = dur.
  → Burrow (libopenconnect): form callback'inde gateway formuna doğrudan yeni OTP verilerek bu red
  hiç yaşanmayabilir; doğrulanacak.
- Kurum sunucu adı ve kullanıcı adı **repoya yazılmaz** (açık kaynak).

Doğrulandı (2026-11-14): sudo'suz userspace tünel çalışıyor; sunucu HIP raporu istemiyor; ESP (UDP)
probe'ları başarısız → HTTPS (TLS) tüneli kullanılıyor; oturum süresi ~30 gün, idle/rekey 180 dk.
Uygulama içi GP (2026-11-15, `netcore/internal/gp`): libopenconnect form geri çağrısıyla aynı kurallar —
portal + gateway olmak üzere en fazla 2 şifre formu, ikincisinde yeni TOTP, üçüncüde iptal. Kurumda henüz
denenmedi; ilk deneme kullanıcıyla birlikte, tek seferlik yapılmalı.
Test durumu: userspace (ocproxy) + RDP testi `tools/gp-userspace-test.py` ile yapılacak.
`--usergroup=gateway` ile tek OTP denemesi de aynı betikle (`--gateway-only`).

Bkz. [[project-goal]].
