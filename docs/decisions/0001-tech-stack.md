# ADR 0001 — Teknoloji yığını

- **Durum:** KABUL EDİLDİ (2026-11-14)
- **Tarih:** 2026-11-14
- **Araştırma:** [RDP motoru + UI](../research/2026-11-14-rdp-engine-ui.md),
  [Tüneller + TOTP](../research/2026-11-14-tunnels-totp.md)

## Bağlam
Uygulama içi (userspace) GlobalProtect + Tailscale tüneli, uygulama penceresinde çizilen RDP,
sekme/pencere çoklu monitör, Mac + Windows + Ubuntu. Bkz. [SPEC](../SPEC.md).

## Bulgulardan çıkan sabitler
- **Tailscale → Go zorunlu.** Kullanılabilir tek gömülü Tailscale `tsnet` (Go). Rust sürümü
  kullanılamaz durumda, C API (libtailscale) Windows'ta yok.
- **GlobalProtect → libopenconnect.** Olgun tek açık kaynak GP istemcisi. Windows'ta userspace
  için küçük bir yama gerekiyor.
- Bu yüzden hangi arayüz/RDP yığını seçilirse seçilsin, ağ katmanı bir **Go "netcore"** olur.

## Önerilen mimari
```
┌──────────────── Burrow uygulaması ────────────────┐
│  Arayüz + RDP (Qt 6 / C++ + FreeRDP 3)             │
│     │  RDP TCP                                      │
│     ▼                                               │
│  Go netcore (c-shared kütüphane)                    │
│   ├─ tsnet (Tailscale, userspace)                   │
│   ├─ libopenconnect (GP) → gVisor netstack          │
│   └─ 127.0.0.1:rastgele port → tünel                │
└─────────────────────────────────────────────────────┘
```

### Güvenlik: loopback port riski
`127.0.0.1` dinleyicisine bilgisayardaki **herhangi bir yerel süreç** bağlanıp tünel üzerinden iş
bilgisayarına erişebilir. Geçici önlem: dinleyici ilk bağlantıyı kabul edince kapanır (OneShot) ve tek
bağlantıya izin verir. **Hedef tasarım:** dinleyici yok — FreeRDP'nin `rdpTransportIo` TCPConnect
callback'i, netcore'un ürettiği bir socketpair ucunu döndürür. RDP motoru uygulanırken buna geçilecek.

## Seçenekler
| | A: Qt 6 + FreeRDP 3 (önerilen) | B: Rust + IronRDP + wgpu |
|---|---|---|
| RDP olgunluğu | En kapsamlı; AVC444, RemoteFX, Kerberos, Gateway | Hızla gelişiyor; **AVC444 yok**, EGFX hataları açık |
| Çoklu monitör | Var; SDL3 istemcisi monitör başına pencere açıyor (referans) | Protokol desteği var; masaüstünde uçtan uca kanıtlanmamış |
| GPU çizim | Hazır HW decoder'lar (VideoToolbox, MF, VAAPI) + Qt RHI | Tüm çizimi biz yazarız |
| Güvenlik | C, sık CVE | Rust, bellek güvenli |
| Derleme | Ağır (OpenSSL, FFmpeg, codec lisansı) | `cargo` ile kolay |
| Risk | Multimon kodu genç; derleme | Eksik özellikler geç ortaya çıkabilir; API kırılmaları |

Orta yol: RDP motorunu bir arayüzün arkasına koymak; FreeRDP ile çıkmak, IronRDP olgunlaşınca
ikinci motor olarak eklemek.

## Açık sorular (karar için)
1. RDP motoru: FreeRDP mi, IronRDP mi?
2. Proje açık kaynak mı, kapalı kaynak mı? (Qt LGPL, openconnect LGPL, H.264 lisansı etkiler)
3. Platform önceliği: hangi işletim sistemi önce? (Windows'ta openconnect yaması gerekiyor)

## Karar (2026-11-14, kullanıcıyla birlikte)
1. **RDP motoru: FreeRDP 3.** Arayüz: **Qt 6 (C++)**. RDP motoru bir soyutlama katmanı arkasında
   tutulur; ileride IronRDP ikinci motor olarak eklenebilir.
2. **Ağ katmanı: Go netcore** (c-shared): tsnet + libopenconnect (dinamik bağlı) + gVisor netstack.
3. **Proje açık kaynak.** Bağımlılıklar buna uygun: FreeRDP Apache-2.0, Qt LGPL-3 (dinamik),
   libopenconnect LGPL-2.1 (dinamik), tsnet BSD-3, gVisor Apache-2.0. **Proje lisansı: MIT**
   (kullanıcı seçimi, 2026-11-14). LGPL kütüphaneler dinamik bağlanır ve dağıtımda kaynak/lisans
   bildirimleri eklenir.
4. **Platform sırası: önce macOS**, sonra Ubuntu, en son Windows (libopenconnect tun-win32 yaması gerekir).
   macOS'ta H.264 için sistemin VideoToolbox decoder'ı kullanılır (FFmpeg/codec lisansı derdi yok).


## Uygulama içi tüneller — uygulama (2026-11-15)

- `netcore/internal/tunnel`: ortak arabirim (`DialContext`, `Status`, `Close`); RDP oturumu tünelin içinden
  tek kullanımlık loopback yönlendirmesiyle (`NetcoreStartTunnelForward`) bağlanır. Sertifika pini her zaman
  cihazın gerçek `host:port`'una bağlıdır.
- **Tailscale** (`internal/ts`): `tsnet`; düğüm durumu/anahtarı `ipn.StateStore` üzerinden kasada şifreli blob
  (`tailscale-<profil-id>`). Tailscale log yüklemesi kapalı (`TS_NO_LOGS_NO_SUPPORT`). Giriş: durum `needs_login`
  → uygulama giriş bağlantısını gösterir, kullanıcı tarayıcıda onaylar. Canlı doğrulandı (giriş adımına kadar).
- **GlobalProtect** (`internal/gp`): libopenconnect süreç içinde (cgo), paketler `SOCK_DGRAM` socketpair üzerinden
  gVisor ağ yığınına (`internal/netstack`) gider — tun, root, sistem rotası yok. Form geri çağrısı kullanıcı adı,
  şifre(+OTP) ve authgroup'u doldurur; ikinci şifre formu (portal→gateway) sonraki TOTP penceresini bekler;
  üçüncü form = giriş reddedildi → iptal (tekrar yok). Sunucu adı sistem DNS'iyle çözülemezse 1.1.1.1/8.8.8.8
  (split-DNS tuzağı). Sunucu sertifikası sistem güven deposuyla doğrulanır; geçmezse reddedilir.
  Kurum sunucusunda henüz denenmedi (hesap kilidi riski nedeniyle kullanıcıyla birlikte denenecek).
- Bağımlılıklar: `tailscale.com` (BSD-3), `gvisor.dev/gvisor` (Apache-2.0), libopenconnect (LGPL-2.1, dinamik).

- **Bağlantı taşıma (2026-11-15, uygulandı):** FreeRDP'nin `TCPConnect` geri çağrısı değiştirildi: her bağlantı için
  bir `socketpair` açılır, bir ucu FreeRDP'ye verilir, diğer ucu `NetcoreConnectFD` ile tünelin (veya doğrudan) hedefe
  taşınır. Hiç loopback dinleyici açılmaz; FreeRDP'nin müzakere sırasındaki yeniden bağlanmaları da her seferinde yeni
  çiftle çalışır. Eski loopback yönlendirici (`internal/forward`) yalnız yedek olarak durur.
