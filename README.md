# gremote

Mac, Ubuntu ve Windows için, GlobalProtect veya Tailscale tünelini **sadece uygulamanın içinde**
açan (sistem genelinde VPN kurmadan) ve gömülü, çoklu monitör destekli RDP istemcisiyle kayıtlı
bilgisayarlara bağlanan masaüstü uygulaması.

> Durum: iskelet. Uygulama açılıyor; tüneller ve RDP oturumu henüz yok.

- Gereksinimler: [docs/SPEC.md](docs/SPEC.md)
- Teknoloji kararı: [docs/decisions/0001-tech-stack.md](docs/decisions/0001-tech-stack.md)
- Katkıda bulunanlar ve AI asistanları için kurallar: [CLAUDE.md](CLAUDE.md)

## Yığın

Qt 6 (C++) · FreeRDP 3 · Go netcore (tsnet + libopenconnect + gVisor netstack) · SQLCipher

## Derleme (macOS)

```bash
brew install go cmake ninja qt freerdp
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/app/gremote.app
```

Go testleri: `cd netcore && go test ./...`

## Klasör yapısı

- `app/` — Qt 6 (C++) arayüz ve RDP motoru (FreeRDP 3)
- `netcore/` — Go ağ katmanı (tüneller, loopback yönlendirme, TOTP); C arşivi olarak uygulamaya bağlanır
- `docs/` — gereksinimler, kararlar (ADR), araştırmalar, proje hafızası
- `.github/` — CI ve otomatik release

## Lisans

[MIT](LICENSE). Üçüncü parti bileşenler kendi lisanslarıyla dağıtılır (FreeRDP Apache-2.0,
Qt LGPL-3, libopenconnect LGPL-2.1, Tailscale BSD-3).
