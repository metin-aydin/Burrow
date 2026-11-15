# ADR 0009 — macOS hedeflerinde iki yönlü dosya aktarımı

- **Durum:** KABUL EDİLDİ (2026-11-15) — uygulama henüz başlamadı (kod yok)
- **Karar verenler:** kullanıcı + AI asistan

## Bağlam

RDP hedeflerinde (Windows, Ubuntu) cliprdr dosya panosu iki yönlü çalışıyor (`app/src/rdp/RdpClipboard.cpp`).
macOS hedefleri ADR 0008'e göre Burrow'un kendi RFB istemcisiyle (`app/src/vnc/VncSession.cpp`) Apple'ın
**RFB 003.889** sürümüne bağlanıyor; orada yalnızca standart modda düz metin panosu var, Apple modunda pano hiç yok.
Kullanıcı dosya kopyalamanın her işletim sisteminden her işletim sistemine iki yönlü çalışmasını istiyor.

Araştırma bulguları (2026-11-15):

- **Standart RFB:** dosya aktarımı tanımlı değil.
- **Extended Clipboard (pseudo-encoding `0xC0A1E5CE`):** TigerVNC/UltraVNC/QEMU/noVNC uzantısı. macOS'un bunu
  ilan ettiğine dair kaynak yok; tersine mühendislik yapan bir istemci (remotex) Mac'in kodlamayı kabul etse de
  panoyu Client/ServerCutText'e bağlamadığını bildiriyor. Ayrıca yalnızca metin/RTF/HTML taşır, dosya taşımaz.
- **Apple'ın pano protokolü:** zDesk belgeledi (`docs/apple-vnc-889.md`): `AutoPasteboard` (0x15),
  `MiscStatus` komut 2/3, fetch (0x0b), yanıt/gönderim (0x1f; zlib arşivi, her öğenin her flavor'ı, 100 MiB sınır,
  boş veri = promise). Finder'da kopyalanan dosya için arşive dosya **yolu** (`public.file-url`) girer, içerik girmez.
- **Ekran Paylaşımı'nın kendi dosya kopyası:** `screensharingd`, `SSFileCopySender` / `SSFileCopyReceiver`
  yardımcı süreçleriyle yapıyor. Tel biçimi hiçbir yerde belgelenmemiş. Bu yardımcılar CVE-2026-43760 ve
  CVE-2026-65400 ile gündemde (eski VNC şifresiyle girişte root yetkisiyle dosya okuma); Apple bu yolu yamalarla
  değiştiriyor.

## Değerlendirilen seçenekler

| Seçenek | Artı | Eksi | Tahmin |
|---|---|---|---|
| 1. Apple panosu (0x1f) + aynı tünelden SFTP (port 22) | Belgeli parçalar; RDP gibi Cmd+C/Cmd+V | Mac'te "Uzaktan Giriş" açık olmalı | 4–6 gün |
| 2. Yalnız SFTP paneli (sürükle-bırak yükleme, listeden indirme) | En düşük risk | Pano deneyimi yok | 2–3 gün |
| 3. **Apple SSFileCopy tersine mühendislik** | Mac'te ek servis gerekmez; Apple'ın Ekran Paylaşımı ile aynı deneyim | Belgesiz, şifreli kanalda; macOS güncellemesi bozabilir; sonuç garantisiz | 1–3 hafta |
| 4. SMB (Dosya Paylaşımı) | Yerleşik servis | İstemci OS'una göre farklı mount; yerelde dinleyen port gerekir (ADR 0008'e aykırı) | 4–7 gün |

## Karar

**Seçenek 3**: Burrow, Apple Ekran Paylaşımı'nın kendi dosya aktarım protokolünü tersine mühendislikle çözüp
kendi RFB istemcisine ekler. Mac'te Ekran Paylaşımı / Uzaktan Yönetim dışında bir servis açılması gerekmez.

### Yöntem

1. **Önce Apple panosu (0x1f)** uygulanır: metin için zaten gerekli (ADR 0008'de "sonraki adım") ve dosya
   aktarımı büyük olasılıkla panodaki dosya URL'leri / promise flavor'ları üzerinden tetikleniyor.
2. **Gözlem:** Mac'ten Mac'e Apple'ın Screen Sharing.app'i ile dosya kopyalanırken trafik izlenir. Kayıt katmanı
   AES-128-CBC ile şifreli olduğu için ağ yakalaması tek başına yetmez; şifre çözülmüş mesajlar Screen
   Sharing.app'in (veya `screensharingd` / `SSFileCopy*`) gönderme/alma fonksiyonlarına süreç içi izleme (Frida /
   REA) ile alınır. Yalnız test Mac'i ve kullanıcının kendi hesabı kullanılır.
3. **Belgeleme:** bulunan mesaj türleri, akış ve durum makinesi `docs/research/apple-file-copy.md`'ye yazılır;
   kod bu belgeden yazılır (Apple kodu kopyalanmaz, yalnızca birlikte çalışabilirlik için tel biçimi).
4. **Uygulama:** yerelden Mac'e ve Mac'ten yerele; klasörler, büyük dosyalar (parçalı, ilerleme göstergesi,
   iptal), ad çakışması. Arayüz RDP'deki dosya panosu ile aynı davranır.

### Güvenlik kuralları

- Dosya aktarımı yalnızca **tür 30 (Mac kullanıcı adı/şifresi)** ile açılmış oturumlarda etkin; eski VNC şifresi
  (tür 2) ile açılan oturumlarda kapalı (CVE-2026-43760'taki root bağlamı nedeniyle).
- Mac'ten gelen dosya adları yol geçişine (`..`, mutlak yol, symlink) karşı süzülür; yerelden yalnızca kullanıcının
  panoya koyduğu dosyalar okunur (RdpClipboard'daki kurallarla aynı).
- Kimlik doğrulama denemelerinde ilk hatada durulur, otomatik tekrar yok (`docs/memory/feedback-auth-retry-safety.md`).
- Test kayıtlarına ve fixture'lara gerçek kullanıcı adı, şifre, anahtar veya dosya içeriği yazılmaz.

### Kontrol noktası

Tersine mühendislik adımı (2–3) yaklaşık **5 iş günü** sonunda değerlendirilir. Tel biçimi o noktada okunabilir
değilse durum kullanıcıya sunulur ve yol birlikte yeniden seçilir (seçenek 1 ve 2 bu ADR'da kayıtlı). Bu, kararı
kendiliğinden değiştirmez.

## Sonuçlar

- Tahmini iş: 1–3 hafta (pano 1–2 gün, tersine mühendislik 3–10 gün, uygulama ve arayüz 3–5 gün); sonuç garantisiz.
- Belgesiz protokol: her macOS sürümünde (özellikle güvenlik yamalarından sonra) yeniden doğrulanmalı. Doğrulanan
  sürümler ADR 0008'deki gibi kaydedilir.
- Standart RFB modu (`BURROW_VNC_STANDARD=1`) ve Mac olmayan VNC sunucularında dosya aktarımı olmaz.
- Cihaz editöründeki "macOS'ta dosya aktarımı yok" uyarısı özellik gelene kadar kalır.
