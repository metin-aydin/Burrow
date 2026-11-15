# ADR 0008 — macOS hedefleri için gömülü VNC istemcisi

- **Durum:** KABUL EDİLDİ (2026-11-15)
- **Karar verenler:** kullanıcı + AI asistan

## Bağlam

Kullanıcı MacBook'una da Burrow ile bağlanmak istiyor. macOS RDP sunucusu içermez; uzaktan erişim için
yerleşik Ekran Paylaşımı / Uzaktan Yönetim (Apple Remote Desktop) servisi **VNC (RFB 3.8)** konuşur, port 5900.
Üç seçenek konuşuldu:

1. Burrow'a gömülü kendi VNC istemcisi.
2. Burrow tüneli açar, yalnız bu Mac'e açık yerel bir port verir ve macOS'un Ekran Paylaşımı uygulamasını başlatır.
3. Hazır `libvncclient` kütüphanesi: GPLv2 olduğu için Burrow'un MIT lisansıyla dağıtımı uyumsuz olur; elendi.

## Karar

- **Seçenek 1**: Burrow kendi RFB istemcisini C++ ile yazar (üçüncü taraf GPL kod yok, MIT ile uyumlu).
- Cihaz kaydına protokol alanı eklenir: `rdp` (varsayılan) | `vnc`. `os` alanına `macos` eklenir (rozet ve
  varsayılan port 5900).
- Bağlantı RDP ile aynı yoldan gider: tünel (GlobalProtect, Tailscale, Cloudflare) ya da doğrudan, netcore'un
  socketpair'i üzerinden; yerelde dinleyen port açılmaz.
- **Kimlik doğrulama:** Apple'ın Uzaktan Yönetim girişi (RFB güvenlik türü 30: Diffie-Hellman + AES-128,
  Mac kullanıcı adı/şifresi) ve klasik VNC şifresi (tür 2). Şifreler yalnız şifreli kasada; başarısız giriş otomatik
  tekrar denenmez.
- **Kodlamalar (ilk sürüm):** Raw, CopyRect, ZRLE (zlib), DesktopSize ve imleç. Görüntü mevcut `RdpView` yerine
  ortak bir görüntü/oturum arayüzüyle aynı pencere, sekme ve tam ekran deneyimine bağlanır.
- **Klavye/fare:** Mac tuşları (Cmd, Option) X11 keysym olarak doğrudan iletilir.
- **Pano:** ilk sürümde düz metin (RFB ClientCutText/ServerCutText).
- **Çoklu monitör (2026-11-15 güncellendi):** macOS standart `ExtendedDesktopSize`'ı desteklemiyor (denendi). Bu
  yüzden Mac'e Apple'ın kendi sürümü **RFB 003.889** ile bağlanılır (kullanıcı adı varsa; `BURROW_VNC_STANDARD=1`
  standart 3.8'e zorlar): tür 30 giriş → ClientInit `0x81` → açık metin ön adım (ViewerInfo, SetMode, SetEncryption
  1 ve 2) → Mac'in anahtar değişimi (0x44f; anahtar ve IV, girişteki MD5 anahtarıyla AES-ECB) → sonrası her iki yönde
  kayıt katmanı (AES-128-CBC, yön başına zincir ve sayaç, SHA-1 bütünlük) → `AppleDisplayLayout` (0x451) her ekranın
  çerçeve içindeki yerini verir; ekranlar RDP'deki gibi sekme/pencere olur, düzenden sonra `AutoFrameBufferUpdate`
  ile güncellemeler kurulur. Apple'a özgü: sağ/orta fare bitleri yer değiştirir, Option `Meta` olarak gönderilir.
  Biçim zDesk projesinin (MIT, `THIRD_PARTY_NOTICES.md`) macOS 26.5–26.6'da ölçtüğü notlara dayanır; belgelenmemiştir,
  bir macOS güncellemesi bozabilir. Apple modunda pano henüz yok (Apple'ın kendi pano protokolü sonraki adım).

## Sonuçlar

- Tahmini iş: 2–4 gün (protokol ve giriş 1–2 gün, görünüm/klavye/pano entegrasyonu 1–2 gün).
- Hedef Mac'te Sistem Ayarları → Genel → Paylaşım → Ekran Paylaşımı (veya Uzaktan Yönetim) açık olmalı.
- Uygulama Sırası: oturum arayüzünü RDP'den ayır → RFB bağlantı ve giriş → kodlamalar → giriş olayları → pano →
  cihaz düzenleyicide protokol seçimi → README.
