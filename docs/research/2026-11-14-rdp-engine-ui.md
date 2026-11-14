# Araştırma: RDP motoru + arayüz yığını (2026-11-14)

Kaynak: alt ajan web araştırması. Doğrulanamayan noktalar en altta.

## 1. FreeRDP 3 (+ Qt 6 C++)
- En kapsamlı açık kaynak RDP; çok aktif (3.33.1, 2026-11-10; ~aylık sürüm). https://github.com/FreeRDP/FreeRDP/releases
- H.264: 3.33.x'te Apple VideoToolbox decoder, Media Foundation, FFmpeg HW düzeltmeleri.
- Özellikler: GFX/AVC444, RemoteFX, pano, ses, NLA/CredSSP/Kerberos, RD Gateway, smartcard (genel bilgi).
- **Özel transport: VAR.** `freerdp_set_io_callbacks` (rdpTransportIo: TCPConnect, ReadPdu/WritePdu…)
  ve 3.9+ `rdpTransportLayer`. Tünel akışı doğrudan beslenebilir.
  https://pub.freerdp.com/api/structrdp__transport__io.html
- Multimon: `MonitorDefArray`/`MonitorCount`/`UseMultimon`; Display Control `SendMonitorLayout`.
  SDL3 istemcisi monitör başına native pencere açıyor → referans kod (`client/SDL/SDL3/`).
  SDL multimon 3.23'te (2026-02) geldi, hâlâ düzeltme alıyor.
- Lisans Apache-2.0 (genel bilgi). Derleme/paketleme ağır (OpenSSL, FFmpeg/OpenH264, codec lisansları).
  Ubuntu paketinde FFmpeg yok → kendi derlememiz gerekir.
- Kullananlar: Devolutions RDM (Mac), Remmina, Thincast.
- KRDC (Qt6+FreeRDP3) zayıf referans: multimon yok, pano/resize eksikti.

## 2. Rust + IronRDP (Devolutions)
- MIT/Apache-2.0, çok aktif (2026 Ekim–Kasım'da onlarca EGFX/multimon PR'ı).
  https://github.com/Devolutions/IronRDP
- **Özel transport: en iyisi** (sans-I/O çekirdek).
- Multimon: connector `monitor_layout`; Display Control çoklu monitör PR #2182 (2026-10-21) birleşti.
- GFX/H.264: AVC420 + OpenH264 birleşti (2026-03); **AVC444 PR #2112 birleşmeden kapandı** → renkli
  metin bulanık olabilir. Açık EGFX hataları var.
- GPU render yok; wgpu/Metal/D3D çizimini biz yazarız.
- Kullananlar: Devolutions Gateway web, Cloudflare Access, Teleport.
- API sık kırılıyor.

## 3. Rust/diğer + FreeRDP FFI
- Sadece `freerdp-sys` 0.1.0 (docs build başarısız). Kendi C shim'imiz gerekir; 1'e göre avantajı yok.

## 4. .NET Avalonia + FreeRDP
- Ciddi gömülü örnek yok (RDPilot deneysel, Mac yok). Kendi wrapper + GPU yüzeyi gerekir.

## 5. Webview (Electron/Tauri) canvas
- IronRDP-web çalıştığını kanıtlıyor ama N×4K + ayrılabilir native pencere için uygun değil (değerlendirme,
  benchmark değil). Tauri sadece ayar/kasa ekranları için düşünülebilir.

## Ajanın sıralaması
1. **Qt 6 + FreeRDP 3** (SDL3 istemcisi referans). Riskler: C bellek güvenliği/CVE sıklığı, multimon kodu
   genç, ağır derleme + H.264 lisansı, soket olmayan transportta event yönetimi.
2. **Rust + IronRDP + winit/wgpu** (Slint/egui). Riskler: AVC444 yok, uçtan uca masaüstü multimon
   kanıtlanmamış, tüm GPU çizimi bizde, API kırılmaları.
- Orta yol: motoru bir arayüzün (trait) arkasına koy; FreeRDP ile çık, IronRDP'yi ikinci motor tut.

## Doğrulanamayanlar
- KRDC 25.x durumu, FreeRDP 3.26 ayrıntıları, IronRDP'nin RDM Mac/Linux'ta kullanılıp kullanılmadığı,
  FreeRDP lisansı ve Remmina/Thincast ayrıntıları yeniden kontrol edilmedi. Çoklu 4K benchmark bulunamadı.
