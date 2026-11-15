# ADR 0007 — Cloudflare Tunnel (Access) tünel türü

- **Durum:** KABUL EDİLDİ (2026-11-15)
- **Karar verenler:** kullanıcı + AI asistan

## Bağlam

Kullanıcı GlobalProtect ve Tailscale'e ek olarak Cloudflare ile, mümkünse ücretsiz, bağlanmak istedi. Cloudflare Zero
Trust ücretsiz planı (50 kullanıcıya kadar) Cloudflare Tunnel + Access'i kapsar. WARP istemcisi sistem genelinde VPN
kurduğu için "tünel yalnız uygulamanın içinde" kuralına (ADR 0001) uymaz; kullanılmaz.

## Karar

- Üçüncü tünel türü **Cloudflare**: hedef bilgisayarda `cloudflared` RDP'yi bir Access uygulaması olarak yayınlar
  (ör. `rdp.example.com` → `tcp://localhost:3389`). Burrow istemci tarafını kendi içinde yapar: TCP, Access
  uygulamasının adresine WebSocket (`wss://`) üzerinden, `cf-access-token` başlığıyla taşınır.
- İki kullanım:
  - **Cihaz başına uygulama** (varsayılan): cihazın adresi Access uygulamasının host adıdır.
  - **Bastion** (opsiyonel profil alanı): profilde bastion host adı verilir, cihaz adresi iç ağ adresidir; istek
    `Cf-Access-Jump-Destination: host:port` başlığıyla yönlendirilir.
- **Giriş:** Cloudflare'in resmi `cloudflared/token` paketi (Apache-2.0) kullanılır: uygulama bilgisi imzalı
  metadata JWT ile doğrulanır (`GetAppInfo`), giriş tarayıcıda yapılır ve anahtar şifreli aktarım servisiyle alınır
  (`RunTransfer`). Org anahtarı varsa yeni uygulama anahtarı tarayıcısız alınır (org token değişimi).
- **Saklama:** uygulama ve org anahtarları cloudflared'in diskteki dosyalarına değil, kasada şifreli blob'a
  (`cloudflare-<profil-id>`) yazılır. cloudflared yalnızca herkese açık JWKS anahtarlarını `~/.cloudflared` altında
  önbelleğe alır (gizli değil).
- Bağlanmadan önce (`prepare`) anahtar hazırlanır; gerekiyorsa tarayıcı açılır, durum `needs_login` olur; anahtar
  hazır olunca RDP oturumu başlar. Süresi dolan anahtar sunucu reddederse silinir ve yeniden alınır.

- **Cihaz içe aktarma (2026-11-15):** Cloudflare üçüncü taraf uygulamalar için hesap okumaya izin veren bir giriş
  (OAuth) sunmuyor; App Launcher'ın da belgelenmiş bir liste ucu yok. Bu yüzden Access uygulamaları API token ile
  listelenir. Burrow, `access`/`read` izni önceden seçili token sayfasını açar (dash.cloudflare.com token şablon
  URL'si); kullanıcı token'ı kopyalar, Burrow panodan bir kez okur, listeler ve panoyu temizler. Token saklanmaz.
  wrangler'ın OAuth istemci kimliğini kullanmak başka bir ürünün kimliğine bürünmek olacağından reddedildi.

## Sonuçlar

- Ücretsiz plan yeterli; Cloudflare'de bir alan adı ve hedefte `cloudflared` gerekir (kurum bilgisayarında izin
  gerekmeyebilir/olmayabilir — ev bilgisayarları için ideal).
- `github.com/cloudflare/cloudflared` bağımlılığı eklenir (yalnız `token` paketi kullanılır).
