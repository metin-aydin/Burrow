---
name: project-brand
description: Burrow logosu ve renk paleti (Toprak), ikon üretimi
type: project
---

- Logo: `app/resources/icon.svg` — kullanıcının UPaste/JumpDesk uygulamalarıyla aynı tarz (renkli yuvarlak kare,
  üstte parlaklık, altta beyaz şerit, kalın beyaz çizgiyle iç içe iki tünel kemeri). Renk 2026-11-14'te kullanıcıyla
  seçildi: **D — Toprak** (#C99A5B → #BF8C4B → #B37D3D).
- İkonlar `tools/render-icons.sh` ile üretilir (headless Chrome; QtSvg gölge filtrelerini çizmez):
  `app/resources/icons/*.png`, `Burrow.icns`, `Burrow.ico`. SVG değişince betik yeniden çalıştırılır.
- README ekran görüntüleri `tools/readme-shots.sh` ile çekilir (demo veri, İngilizce, gerçek pencere çerçevesi);
  renk/arayüz değişince yeniden çalıştırılır.
- Uygulama teması `app/qml/Theme.qml`: vurgu #A26E35 (açık) / #B07A3E (koyu), sıcak nötr zeminler.
  Tünel kimlik renkleri (2026-11-15, colorize): GlobalProtect **denim mavisi**, Tailscale **turkuaz**, Cloudflare
  **erik**. Kırmızı/turuncu bilerek bırakıldı: danger, warning ve marka vurgusuyla karışıp tünel çipini hata gibi
  gösteriyordu. Önizlemesi olmayan kart zemini OS başına tek tonlu koyu renk (mavi-mor gradyan kaldırıldı).

**Neden:** Kullanıcının uygulama ailesiyle görsel bütünlük.
**Nasıl uygulanır:** Yeni ekranlarda yalnızca Theme renklerini kullan; logo için ikon PNG'lerini kullan, elle çizme.
Bkz. [[feedback-ui-title-case]].
