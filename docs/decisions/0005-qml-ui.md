# ADR 0005 — Arayüz: Qt Quick (QML), kart galerisi, şifreli önizlemeler

- **Durum:** KABUL EDİLDİ (2026-11-14)
- **Karar verenler:** kullanıcı + AI asistan

## Bağlam

İlk arayüz Qt Widgets ile yazıldı; kullanıcı "çok basit" buldu. İki tasarım yönü örnekle gösterildi
(A: kenar çubuğu + detay, B: kart galerisi). Kullanıcı **B'yi** seçti ve kartlarda cihazın **son ekran
görüntüsünün önizleme** olarak görünmesini istedi.

## Karar

- Arayüz **Qt Quick / QML** ile yazılır (Qt Quick Controls, Basic stil üzerine kendi bileşenlerimiz ve
  `Theme` singleton'ı; açık/koyu tema sistemi izler). Qt Widgets arayüzü kaldırılır.
- İş mantığı C++'ta kalır: `AppController` (kasa durumu, işlemler), `DeviceModel` / `TunnelModel`
  (`QAbstractListModel`), önizlemeler için `QQuickImageProvider`. QML yalnızca görünüm.
- **Ana ekran kart galerisi:** her cihaz bir kart — üstte son ekran görüntüsü (16:10), altında ad, tünel
  profili adı (kullanıcının verdiği ad, ör. "İş VPN") ve adres, "Bağlan" butonu; düzenle/kopyala/sil
  menüsü. Önizleme yoksa işletim sistemine göre renkli yer tutucu.
- **Metin kuralı:** buton, başlık ve menülerde her kelimenin baş harfi büyük (feedback-ui-title-case).
- **Önizleme görüntüleri şifreli saklanır:** uzak masaüstü görüntüsü hassas veridir. RDP bağlantısı
  kapanırken küçültülmüş görüntü (JPEG, ~640 px) kasanın veri anahtarıyla (DEK, ADR 0004)
  XChaCha20-Poly1305 ile şifrelenip kasa dosyasının yanındaki `blobs/preview-<cihaz-id>.bin` dosyasına yazılır (blob adı AAD'ye bağlı; dosyalar cihazlar arasında yer değiştirilemez).
  Ana JSON belgesine konmaz (her kaydetmede büyük veriyi yeniden şifrelememek için). Cihaz silinince
  önizlemesi de silinir. Ayarlardan önizlemeler kapatılabilir (sonraki adım).
- RDP görüntüsü ileride QML penceresine (QQuickItem / QSGTexture) çizilir; çoklu monitör ayrı
  `Window`'lar olarak açılır (SPEC §5.1).

## Sonuçlar

- Daha modern, animasyonlu, temalı arayüz; QML'de tasarım değişikliği hızlı.
- Qt Quick, Qt Quick Controls, Quick Layouts modülleri pakete girer (uygulama boyutu artar).
- Önizleme dosyaları kasa olmadan okunamaz; kasa şifresi değişince yeniden şifreleme gerekmez (DEK aynı).
