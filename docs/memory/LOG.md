# İş Günlüğü

En yeni kayıt en üstte.

## 2026-11-15

- Ana pencerede kendi başlık çubuğumuz (macOS): içerik başlık çubuğunun altına uzanır (Qt ExpandedClientAreaHint +
  NoTitleBarBackgroundHint), boş birleşik NSToolbar başlığı 52 pt yapıp trafik ışıklarını ortalar
  (`platform/WindowChrome_mac.mm`). Galeri başlığı bu satırda; boş yerden sürükleyince pencere taşınır, çift tık büyütür.
  Token'lar: `Theme.titleBarHeight`, `Theme.trafficLightsInset`. ApplicationWindow safe-area dolgusu kapatıldı.
- Kaydırmada lastik bant (bounce) kaldırıldı: diyaloglar, açılır listeler ve galeri `StopAtBounds`.
- Varsayılan monitör ayarı "Her Mac ekranı için bir monitör"; mevcut cihazlar bir kerelik geçirildi
  (`settings.multiMonitorDefault` bayrağı).
- Monitör sekmeleri iyileştirmeleri: ayrık pencere ilk kareyi hemen çeker (önceden uzak ekran değişene kadar siyah
  kalıyordu); her görünüm yalnızca kendi monitör bölgesini kopyalar ve güncellemeler ~16 ms'de birleştirilir.
  Düzen ekranı merkez noktasıyla kaydedilir (isim yedek), pencere önce o ekranda gösterilip sonra kademeli tam
  ekran yapılır (macOS gizli pencereyi ana ekranda tam ekran yapıyordu). Tek sekme kalınca sekme barı gizlenir.
  Sekmeler sürüklenerek sıralanır, çift tık / sağ tık ile yeniden adlandırılır; sıra ve isimler `layout.order`,
  `layout.names` alanlarında saklanır. Geliştirme: `BURROW_DEMO_LAYOUT=<json>`.
- Çoklu monitör sekmeleri: GNOME Remote Login 3 monitörü kabul etti ama kendi düzenine yerleştirdi; Burrow bölemedi
  ve hepsini tek resimde gösterdi. Gerçek düzen artık GFX ResetGraphics'ten okunuyor (`rdp: server desktop …` logu).
  Uzak monitörler ana pencerede sekme; sekme pencere dışına sürüklenince (veya sağ tık → "… Üzerinde Aç") o
  ekranda ayrı pencere (MonitorWindow) açılır. Ayrı pencere kapanınca sekmeye döner, bağlantı sürer. Hangi monitörün
  hangi ekranda/tam ekran açıldığı cihazın `layout.detached` alanına kaydedilir ve sonraki bağlantıda geri yüklenir.
  Kayıt 800 ms gecikmeli: çıkışta pencereler kapanırken düzen silinmez. Geliştirme: `BURROW_DEMO_SPLIT=N`.
- Çoklu monitör düzeltmesi: FreeRDP 3 düzende ana ekranın 0,0 olmasını ister; ana ekran ortada/sağdayken bağlantı
  ön kontrolde reddediliyordu. Düzen artık ana ekrana göre gönderiliyor (görüntü yine sol üstten başlar).
  `BURROW_DEMO_DISPLAYS=all` demo kancası eklendi.
- Cihaz düzenleyici her açılışta General sekmesiyle açılır (tıklama Segmented bağlamasını bozuyordu; değer elle sıfırlanıyor).
- Oturum penceresi birleşti: Bağlan'a basınca cihaz penceresi hemen açılır; tünel ve RDP aşamaları tek listede
  gösterilir (ConnectProgress kaldırıldı). Bağlanamazsa pencere hatayı ve Tekrar Dene'yi gösterir. Aşama işaretleri
  hep aynı boyda (22 px), ayrıntı satırı ayrılmış: tamamlanınca liste zıplamıyor. Onaylanan sertifika penceresi kapanır.
- Çözünürlük: varsayılan artık Mac'in mantıksal boyutu (yazılar Mac uygulamalarıyla aynı boyda); cihazda "Retina
  Çözünürlüğü" ile fiziksel piksel + %200 ölçek ipucu. Pencere ekranın %90'ı ile açılır.
- Çoklu monitör: cihazda "Ekranlar: Tüm ekranlar" → her Mac ekranı için bir uzak monitör (MonitorDefArray), her
  ekranda tam ekran pencere. Sunucu düzeni reddederse (Ubuntu ekran paylaşımı `mirror-primary` yalnızca ana monitörü
  gönderir) tek pencereye döner. Ubuntu'da birden çok monitör için Uzaktan Oturum Açma veya `screen-share-mode extend`.
- Galeri başlığındaki logo kaldırıldı; "Burrow" adı filtre çipleri ve kartlarla aynı sol hizada (28 px).
- Güvenlik düzeltmesi (inceleme): pencere kapanınca uygulama menü çubuğunda açık kasayla kalıyordu → Ayarlar'a
  "Otomatik Kilit" (5/15/30/60 dk ya da Hiçbir Zaman, varsayılan 15). Kullanıcı girdisi zamanlayıcıyı sıfırlar; açık
  uzak masaüstü varken kilitlenmez. Tray menüsü kendi eyleminin içinden yeniden kurulmuyor (kuyruklu bağlantılar,
  ertelenmiş eylemler).
- Hesap bağlama / cihaz içe aktarma: Tünel Profilleri'nde Tailscale ve Cloudflare için "Cihazları İçe Aktar". Tailscale:
  tünel açılır (gerekirse tarayıcı girişi), tailnet makineleri listelenir (`NetcoreTunnelPeers`, MagicDNS adı/IP, OS,
  çevrimiçi), seçilenler cihaz olur. Cloudflare: OAuth yok → API anahtarıyla (kaydedilmez) Access uygulamaları listelenir
  (`cf.ListApps`, testli), seçilenler cihaz olur. Hedefte cloudflared kurulumu hâlâ elle. Gerçek hesaplarda denenmedi.
- Tray menüsü (`app/src/app/Tray.*`, QSystemTrayIcon; uygulama artık QApplication): kayıtlı bilgisayarlar (tıklayınca
  bağlanır, gerekirse önce kilit açtırır), Burrow'u Aç, Kilitle, Çık. macOS'ta tek renkli şablon simge. Pencereyi kapatmak
  uygulamayı tray'de bırakır. macOS'ta durum öğesinin oluştuğu erişilebilirlik API'siyle doğrulandı.
- OTP içe aktarma: boş alanda "İçe Aktar" panodaki otpauth bağlantısını kullanıyor, yoksa ne yapıştırılacağını
  anlatan ipucu gösteriyor; QR butonu: görsel dosyası, panodaki görsel, kamera (QtMultimedia, `NSCameraUsageDescription`,
  özel Info.plist). Çözücü: gozxing (Apache-2.0) `netcore/internal/qr` + `NetcoreDecodeQR`; görüntüler yalnız bellekte.
  Qt testi gerçek QR görseliyle (tests/qr-fixture.png, RFC örnek anahtarı) geçti.
- Arayüz: eşit genişlikte segment/sekmeler, diyalog altında boşluk, yeni yay animasyonlu yükleme simgesi (`Spinner`),
  çarkın yanında kilit butonu, kartta oynat katmanı kaldırıldı ve menü açıkken kart büyük kalıyor.
- Tünel yaşam döngüsü: tünel yalnız bağlanırken açılıyor, son oturum kapandıktan 20 sn sonra kapanıyor; aynı tüneli
  kullanan cihazlar aynı oturumu paylaşıyor (VPN'de tek oturum). Tünel Profilleri'nde elle "Bağlan" kaldırıldı.
- Bağlantı testi + adım adım ilerleme: Go durumuna `step`/`wait` (OTP penceresi geri sayımı); `ConnectionSteps`
  (onay animasyonu, yay, geri sayım halkası), tünel düzenleyicide "Bağlantıyı Test Et" (GP başarılı olunca tüneli hemen
  kapatır), tünel beklenirken `ConnectProgress` diyaloğu (hata + Tekrar Dene), oturum penceresinde RDP aşamaları
  (bağlanma → sertifika → giriş → masaüstü). Tailscale testiyle görsel olarak doğrulandı.
- ✅✅ **Uçtan uca hedefe ulaşıldı:** Burrow kendi içinde GlobalProtect tüneli açıp (sudo/sistem VPN'i yok, çift OTP otomatik)
  iş bilgisayarına (Ubuntu 24.04, GNOME Remote Desktop) kendi RDP penceresinde bağlandı. Önceki NLA hatası yanlış
  girilen paylaşım bilgilerindendi.
- Kullanıcının yeni istek listesi (2026-11-15): diyaloglarda alt boşluk (tüm ekranlar), VPN yalnız bağlanırken açık / son
  oturumla kapansın / aynı tüneli kullanan cihazlar paylaşsın (VPN'de tek oturum), tünel düzenleyicide bağlantı testi ve
  adım adım animasyonlu ilerleme (OTP bekleme süresi dahil; cihaz bağlanırken de), OTP içe aktarmada yönlendirme + QR
  (kamera/resim), Tailscale/Cloudflare hesabına bağlanarak otomatik kurulum, modern yükleme simgesi, kartta oynat butonu
  olmasın ve menü açıkken büyütme kalsın, çarkın yanında kilit butonu, tüm sistemlerde tray menüsü (cihazlar, tek tıkla
  bağlan), cihaz düzenleyicide eşit genişlikte sekmeler.
- **Dinleyicisiz bağlantı (ADR 0001 hedef tasarımı uygulandı):** FreeRDP `TCPConnect` → `socketpair` + `NetcoreConnectFD`
  (tünel ya da doğrudan); yerel port yok, yeniden bağlanma sorunsuz. Yönlendiricinin kabul döngüsü artık yuva beklerken
  takılmıyor. Yerel test sunucusunda doğrulandı (oturum + önizleme).
- Güvenlik incelemesi (yönlendirici): çoklu kabul yalnız bağlantı kurulurken — RDP oturumu `connected` olunca
  `NetcoreForwardSeal` dinleyiciyi kapatıyor, ayrıca en geç 60 sn'de kendiliğinden kapanıyor (`AcceptWindow`). Testli.
- ✅ **GP girişi uygulama içinde kurumda başarılı** (çift OTP: portal formu → gateway 512 → ikinci form yeni OTP →
  tünel 172.16.100.x, ESP yok → HTTPS). RDP bağlanmadı; iki neden: (1) openconnect macOS/BSD'de tun paketlerine 4 baytlık
  AF başlığı ekliyor (yalnız --script-tun'da eklemiyor) → netstack'e `AFPrefix` seçeneği; (2) FreeRDP müzakere sırasında
  yeniden bağlanıyor, OneShot dinleyici kapanmıştı → tünel yönlendirmesi `MaxAccepts: 4` + kısa yuva beklemesi.
  Ayrıca "GPST Dead Peer Detection detected dead peer" görüldü (muhtemelen bozuk paketler; yeniden denemede izlenecek).
- Güvenlik incelemesi (Cloudflare, 1 bulgu): org anahtarı değişiminde yönlendirmeler yalnız https ve yalnız uygulama
  host'u ile Access alanına izinli; org anahtarı yalnız `*.cloudflareaccess.com` alanına gidiyor. Testli.
- Güvenlik incelemesi (log temizleyici, 3 bulgu): URL'ler `net/url` ile ayrıştırılıp yalnız `scheme://host` bırakılıyor
  (yol/sorgu/kullanıcı bilgisi yok), uzun anahtar benzeri diziler maskeleniyor, gizlenen hata satırı sabit mesajla
  kaydediliyor (sunucu metninden durum çıkarımı yok).
- **Cloudflare Tunnel (ADR 0007 KABUL):** `netcore/internal/cf` — cloudflared `token` paketiyle doğrulanmış uygulama
  bilgisi + tarayıcı girişi, org anahtarıyla tarayıcısız yenileme, anahtarlar kasada (`cloudflare-<id>`), TCP-over-
  WebSocket (`cf-access-token`, bastion için `Cf-Access-Jump-Destination`). Yerel TLS WebSocket sunucusuyla testli.
  Arayüz: üçüncü tünel türü, bastion alanı, turuncu çip/bulut ikonu. Gerçek bir Cloudflare hesabında denenmedi.
- Arayüz düzeltmeleri (kullanıcı geri bildirimi): Türkçe kalan örnek metinler, yazarken genişleyen alanlar (BField/
  BCombo sabit implicitWidth), küçük pencerede alta taşan diyalog (yükseklik sınırı + kaydırma). Kullanıcı Cloudflare
  Tunnel'ın üçüncü tünel türü olarak eklenmesini onayladı.
- Güvenlik incelemesi (GP tanılama logu): openconnect mesajları artık `redactSecrets` ile temizleniyor (kimlik bilgisi
  geçen satır atılır, URL sorgu/kullanıcı bilgisi silinir); testli.
- **Uygulama içi tüneller:** netcore'a `tunnel` (ortak arabirim), `netstack` (gVisor, socketpair üzerinde; iki yığınlı
  64 KB TCP testi geçti — macOS datagram soket arabelleği büyütülmeli, yoksa patlamada paket kaybı/kilitlenme),
  `ts` (tsnet, durum kasada şifreli, log yükleme kapalı), `gp` (libopenconnect cgo, çift OTP, tekrar yok, public
  DNS yedeği). C API: `NetcoreTunnelStart*`, `Status`, `SubmitOTP`, `Stop`, `NetcoreStartTunnelForward`.
  Uygulama: `TunnelManager`, tünelli cihaza bağlanınca tünel açılır ve bağlanınca oturum başlar; VPN şifresi
  kayıtlı değilse sorulur (yalnız bellekte), manuel OTP sorulur, Tailscale giriş bağlantısı gösterilir; tünel
  durum noktaları (çip), Tünel Profilleri'nde Bağlan/Bağlantıyı Kes. Tailscale giriş adımına kadar canlı doğrulandı.
  GP kurumda denenmedi. CI'a libopenconnect kurulumu eklendi.
- Güvenlik incelemesi (pano, 3 bulgu) düzeltildi: uzaktan gelen pano verisi 32 MB ile sınırlı ve uzunluk `int`'e
  kesilmiyor; DIB başlığı 64 bit aritmetikle doğrulanıyor (başlık boyutu, bit derinliği, yalnız BI_RGB/BI_BITFIELDS,
  renk sayısı, piksel verisinin sınırlar içinde olması); kanal kopunca geri çağrılar temizleniyor ve her geçişte
  boş işaretçi kontrolü var. Kötü niyetli başlık testleri eklendi.
- Güvenlik incelemesi (sertifika, 2 bulgu): pin varken eşleşmeyen sertifika artık her durumda reddediliyor (adres/biçim
  farkıyla yeniden sorma yok), pin tek seferlik yazılıyor; değiştirmek için cihaz düzenleyicide "Kayıtlı Sertifikayı
  Unut" veya adres/port değişikliği. Eski çıplak parmak izi biçimi de karşılaştırılıyor.
- **Pano + dinamik çözünürlük:** `RdpClipboard` (cliprdr; metin CF_UNICODETEXT + görsel CF_DIB, iki yönlü, cihaz
  ayarına göre yön; içerik loglanmaz/diske yazılmaz), uzak kopyalama Mac panosuna anında alınıyor. `disp` kanalıyla
  pencere boyutu değişince (400 ms sonra) uzak masaüstü yeniden boyutlanıyor; Retina'da %200 ölçek. İşçi iş parçacığına
  görev kuyruğu (`post`). DIB dönüşümü birim testli. Test sunucusunda kanallar açıkken bağlantı doğrulandı; shadow
  sunucu pano/yeniden boyutlandırmayı desteklemediği için bu ikisi gerçek Windows/GNOME'da denenmedi.
- Güvenlik incelemesi (otomatik, RDP, 3 bulgu) düzeltildi: eski "RDP security" katmanı kapatıldı (yalnız NLA/TLS);
  sertifika ilk bağlantıda kullanıcıya parmak iziyle soruluyor ("Trust And Connect"), pin `host:port|sha256`,
  aynı adreste değişen sertifika sormadan reddediliyor; "kapanınca kilitle" Windows'ta da Win+L gönderiyor,
  henüz olmayan "oturumu kapat" seçeneği arayüzden kaldırıldı. Test sunucusunda diyalog ve bağlantı doğrulandı.
- ✅ **RDP motoru ilk sürüm çalışıyor:** `RdpSession` (FreeRDP 3, işçi iş parçacığı, GFX açık, GDI BGRX32 →
  QImage), `RdpView` (QQuickPaintedItem; ölçekleme, fare, tekerlek, klavye — Mac sanal tuş kodu → RDP scancode),
  `KeyMapper` (Cmd→Ctrl kısayol / tam değiş / kapalı; birim testli), `SessionWindow.qml`, `CredentialsDialog.qml`.
  Sertifika TOFU: ilk bağlantıda parmak izi cihaz kaydına (`certFingerprint`) sabitlenir, değişirse bağlanılmaz.
  Oturum kapanınca son kare şifreli önizleme olur; Ubuntu + "kapanınca kilitle" ise önce Super+L gönderilir.
  Yerel `freerdp-shadow-cli` (NLA, rastgele test kullanıcısı) ile uçtan uca doğrulandı: bağlandı, ekran çizildi,
  önizleme kaydedildi. Hata: macOS'ta pencere kapatmayı reddedince "Çık" iptal oluyordu → düzeltildi.
  Henüz yok: pano, çoklu monitör, dinamik çözünürlük, uygulama içi tünel (tünelli cihazlar şimdilik bağlanmaz).
- Güvenlik incelemesi (otomatik, 4 bulgu) düzeltildi: güncelleme dosya adı manifestten yol olarak alınmıyor
  (beklenen `Burrow-<sürüm>-<platform>.zip` olmalı, sürüm X.Y.Z biçiminde); `BURROW_UPDATE_URL` release'te yalnız
  https (testler `BURROW_UPDATER_ALLOW_INSECURE` ile); kurulum 0700 rastgele geçici dizinde, yardımcı betik
  değiştirmeden hemen önce imza + ekip kimliğini yeniden doğruluyor; release.yml'den `workflow_dispatch`
  kaldırıldı, işler `master` ile sınırlı, `contents: write` yalnız gereken işlerde.
- **Impeccable kuruldu (AI slop engeli):** pbakaus/impeccable v4.9.1 skill'i ve 4 alt ajanı proje içine kopyalandı
  (`.claude/`, Codex için `.agents/`), telemetri kapalı (`.impeccable/config.json`), `.gitignore`'a çalışma dosyaları
  eklendi. `CLAUDE.md`'ye QML'e uyarlanmış "AI slop yasak" bölümü yazıldı. Design hook kurulmadı (QML taramıyor).
- **Release + güncelleme JumpDesk/UPaste gibi (ADR 0002 güncellendi):** yeni `release.yml` (prepare/taslak →
  macOS arm64 imza+notarize → `latest.json` + yayımla → hata olursa sil), `release-notes.py` + testi, PR şablonu,
  `entitlements.mac.plist`, CI'a Qt testleri ve çeviri kontrolü. Sparkle bırakıldı; `Updater` (C++): 15 sn/6 saat
  denetim, sormadan indirmez, SHA-256 + aynı ekip imzası, ayrık yardımcıyla paket değişimi. Ayarlar → Güncellemeler,
  galeride "Güncelleme X" çipi. `tst_vault`'a yerel sahte sürümle güncelleyici testi (kurcalanmış dosya reddi dahil).
  Workflow GitHub'da henüz çalıştırılmadı (gizli değerler/ortam yok).
- Logo (UPaste/JumpDesk tarzı, kullanıcı rengi seçti: Toprak) + ikon setleri (`tools/render-icons.sh`), uygulama
  teması bu renge göre güncellendi; macOS paketine `.icns`, Windows için `.rc/.ico` eklendi.
- **Çoklu dil (ADR 0006):** kaynak metinler İngilizceye çevrildi, Türkçe katalog (`app/i18n/burrow_tr.ts`, 173
  metin) eklendi; ilk açılışta sistem diline göre seçiliyor, Ayarlar → Dil ile değişiyor (canlı). CI çeviri
  kontrolü betiği eklendi. İki dilde ekran görüntüsüyle kontrol edildi.
- GitHub MCP kontrol edildi: `plugin:engineering:github` hâlâ "Incompatible auth server: does not support
  dynamic client registration" → yeniden bağlanmakla düzelmez; kullanıcının PAT ile yapılandırması gerekiyor.
- Referans incelendi: kullanıcının UPaste ve JumpDesk repoları (logo tarzı: renkli yuvarlak kare + parlaklık +
  alt beyaz şerit + kalın beyaz çizgi sembol; release: prepare/taslak → 3 platform build → yayımla/sil, PR
  açıklaması sürüm notu, `release` ortamı, aynı Apple gizli değer adları; güncelleme: açılıştan 15 sn sonra
  ve 6 saatte bir denetim, sormadan indirmez).
- Güvenlik incelemesi (otomatik): ana şifre değişince `.bak` eski şifreyle açılabiliyor ve DEK aynı kaldığı
  için güncel kasayı da açıyordu. Düzeltme: şifre değişiminde DEK yenileniyor, belge + blob'lar yeniden
  şifreleniyor, `.bak` siliniyor, Touch ID kopyası yenileniyor (olmazsa Touch ID kapanıyor). Go + Qt
  testleri güncellendi/eklendi, ADR 0004 güncellendi.

## 2026-11-14

- İlk push: `development` dalı `github.com/metin-aydin/Burrow`'a gönderildi (4 commit). `master` henüz yok;
  varsayılan dal/master koruması ve repo açıklaması GitHub MCP bağlanınca ayarlanacak.
- Input etiketleri de baş harfleri büyük yapıldı ("Kullanıcı Adı", "Ana Şifre (Tekrar)"); kural kaydı
  güncellendi. Tam genişlikteki butonlarda metin ortalandı.
- **Arayüz QML'e taşındı (ADR 0005 KABUL):** kullanıcı B (kart galerisi) tasarımını seçti, kartlarda son ekran
  görüntüsü önizlemesi istedi. Widgets arayüzü kaldırıldı. C++: `AppController` (QML `App` singleton),
  `DeviceModel`/`TunnelModel`/`DeviceFilterModel`, `PreviewProvider` (image://preview). Go kasasına şifreli
  blob API'si (`PutBlob/Blob/DeleteBlob`, ad AAD'de) + test. QML: Theme (açık/koyu), kilit/kurulum ekranı,
  galeri (arama, tünel filtre çipleri, hover'da oynat ve menü), cihaz/tünel düzenleyici (TOTP canlı kod),
  ayarlar. Baş harfler büyük kuralı uygulandı. Geliştirici yardımcıları: `--seed-demo` +
  `BURROW_DEMO_PASSWORD`/`BURROW_DEMO_OPEN`/`BURROW_GRAB` (pencereyi odak çalmadan PNG'ye yakalar).
  Ekran görüntüleriyle kontrol edildi (koyu tema). Açık tema görsel olarak kontrol edilmedi.
  Not: QML'de `on` + büyük harfle başlayan özellik adı (ör. `onAccent`) sinyal işleyicisi sanılır.
- **Proje adı: Burrow** (eski: gremote). Klasör `~/projects/Burrow`, origin `github.com/metin-aydin/Burrow`
  (kullanıcı GitHub'da açtı, boş). Kod/dokümanda yeniden adlandırma: CMake hedefi `burrow`, `Burrow.app`,
  bundle id `io.github.metin-aydin.burrow`, Go modülü `burrow/netcore`, `BURROW_*` değişkenleri,
  `burrow-agent`. Bu günlükteki eski kayıtlar tarihsel olarak "gremote" der. Derleme ve testler geçti.
- Arayüz "çok basit" bulundu; buton/başlıklarda her kelimenin baş harfi büyük olmalı
  (feedback-ui-title-case). Modern arayüz için örnek tasarımlar hazırlanıyor.
- **Kasa + cihaz ekranları** (ADR 0004 KABUL: Go'da Argon2id + XChaCha20-Poly1305 tek dosya, DEK/KEK
  hiyerarşisi, opsiyonel Touch ID). Go `internal/vault` + testler; C API (`vault_api.go`: kasa + TOTP parse/kod);
  Qt: `VaultStore`, şema `Model` (cihaz/tünel/ayar), kilit açma/oluşturma, cihaz listesi/düzenleme (genel,
  oturum, pano/klavye), tünel profilleri (GP + TOTP içe aktarma ve canlı kod, Tailscale), ana şifre
  değiştirme, Touch ID (Data Protection keychain; imzasız build'de devre dışı mesajı). `tst_vault` (QtTest)
  ve `go test -race` geçti. Arayüz görsel olarak henüz kullanıcı tarafından denenmedi. QR görselden okuma
  ve otomatik kilit (süre) sonraki iş. Commit edilmedi.
- ✅ **Uçtan uca doğrulandı:** sudo'suz GP tüneli üzerinden Windows App ile Ubuntu 24.04'e RDP bağlantısı
  çalıştı (3. deneme). Önceki siyah ekran / 0x207 Ubuntu tarafında aralıklı (Desktop Sharing durumu);
  agent ile ele alınacak. Sıradaki iş: kasa + cihaz ekranları.
- RDP 2. deneme: doğru paylaşım şifresiyle 0x207 (NLA reddi). SSH ile Ubuntu logu alınmak istendi: tünel
  çalışıyor ama iş bilgisayarında 22 portu kapalı (banner yok). Ubuntu tarafı ancak fiziksel erişimle
  incelenebilir (openssh-server/gremote-agent kurulumu).
- Tünelden RDP: Windows App kimlik sordu, bağlandı ama ekran siyah. Tünelden yoklama: sunucu GNOME Remote
  Desktop (sertifika CN=GNOME), NLA istiyor, TLS 1.3 tamam → tünel uçtan uca çalışıyor; siyah ekran Ubuntu
  tarafında (kullanıcının baştaki "bazen ekran siyah" şikâyetiyle aynı). Olası nedenler: oturum kilitli /
  ekran güç tasarrufunda (ADR 0003'teki agent'ın çözeceği durum). Sunucu logu (journalctl) henüz görülmedi.
- ✅ **GP userspace tüneli sudo'suz kuruldu** (openconnect `--script-tun` + ocproxy, SHA512 TOTP, çift OTP
  otomatik). Gateway ilk reddi beklenen adım olarak geçti, 2. girişte HTTP 200. ESP (UDP) başarısız →
  HTTPS tüneline düştü (çalışıyor). HIP raporu istenmiyor. RDP doğrulaması kullanıcıda.
- Betik "yalnızca istemde gönder" değişikliğiyle takıldı: openconnect stdout'u pipe'ta tamponlu, ilk istem
  görünmeden stdin'de bekliyor. İlk kimlik bilgisi yine baştan gönderiliyor (masaüstü betiğiyle aynı).
- SHA512 ile test: portal girişi başarılı, gateway ilk denemede `auth-failed` → betik durdu. Analiz:
  bu red çift OTP akışının beklenen adımı (openconnect kullanılmış OTP'yi gateway'e tekrar yolluyor).
  Betik düzeltildi: portal reddinde dur, gateway'de 1 red beklenen, 2. redde dur; kimlik bilgisi yalnızca
  istem gelince gönderiliyor. project-gp-auth-flow güncellendi.
- Hesap açıldı (masaüstü betiği bağlandı) ama test betiği yine `auth-failed` aldı. Muhtemel neden: kullanıcı
  geçmişteki `--otp-digest` içermeyen komutu çalıştırdı → varsayılan SHA1. Düzeltme: `--otp-digest`
  artık zorunlu (varsayılan yok), ağa çıkmadan kodu gösteren `--show-otp` eklendi (önce telefonla karşılaştır).
- **Olay:** GP test betiği SHA1 TOTP ile 6 başarısız giriş yaptı → kullanıcının VPN hesabı kilitlendi.
  Kurumun TOTP'si SHA512. Betik ilk `auth-failed`'da duracak ve `--otp-digest` alacak şekilde düzeltildi.
  Ders: feedback-auth-retry-safety. GP testleri hesap açılana kadar durduruldu.
- Güvenlik incelemesi: loopback dinleyicisine başka yerel süreçler bağlanabilir. Forwarder'a OneShot
  eklendi (ilk bağlantıdan sonra dinleyici kapanır), test eklendi. Kalıcı çözüm (socketpair +
  rdpTransportIo) ADR 0001'e yazıldı. Commit edilmedi.
- GP testi için ocproxy kuruldu. Test 1 DNS hatası verdi (split-DNS). Kullanıcının mevcut VPN betiği
  incelendi: openconnect kurumda zaten çalışıyor; authgroup ve genel DNS ile çözme gerekiyor
  (bkz. project-gp-auth-flow). `tools/gp-userspace-test.py` yazıldı (Keychain + TOTP, sudo'suz
  ocproxy tüneli). SPEC GP alanları güncellendi.
- Kullanıcının Mac'indeki (repo dışı) split-DNS düzeni düzeltildi: kurum domaini için `/etc/resolver`
  dosyası artık kalıcı değil; kullanıcının split-dns vpnc betiği bağlanınca yazıyor, kopunca siliyor,
  reset betiği de siliyor (yedekler `.bak`). Mevcut dosyayı kullanıcı `sudo rm` ile silecek.
- **Proje iskeleti kuruldu** (git: `development` branch, henüz commit yok):
  - `netcore/` (Go): loopback forwarder (`internal/forward`), TOTP + otpauth/migration parse
    (`internal/totp`), C API (`netcore.go`, c-archive). `go test -race` geçti (RFC 6238 vektörleri dahil).
  - `app/` (Qt 6 C++): ana pencere iskeleti, NetCore wrapper, `RdpEngine` arayüzü, `FreeRdpEngine` iskeleti
    (FreeRDP 3.33.1 ile bağlanıyor). Derleniyor, açılıyor (offscreen 4 sn çökmeden).
  - `.github/`: `ci.yml`, `release.yml` (imza/notarization secret varsa çalışır), `next-version.sh`
    (geçici repoda test edildi). Sparkle appcast henüz yok.
  - Bu Mac'te derleme notu: Command Line Tools SDK'sı bozuk (tapi hatası) → Xcode SDK'sı ve clang ile
    yapılandır (`docs/memory/reference-local-build.md`).
- Kullanıcı Ubuntu'ya agent kurulabileceğini onayladı → ADR 0003 KABUL EDİLDİ. Açık işler:
  GitHub reposu (MCP bağlantısı bekleniyor), GP ön testi, proje iskeleti, agent doğrulama testleri.
- Kullanıcı: Ubuntu yükseltilemez, tüm sürümlere uyumlu olmalı. SPEC §5.2'ye öneri: Desktop
  Sharing + Ubuntu tarafında gremote-agent (bağlanınca kilit aç, kopunca kilitle); opsiyonel ayrı
  kalıcı oturum (26.04+: Remote Login, 22.04/24.04: xrdp + ayrı kullanıcı). Agent kurulumunun
  iş bilgisayarında mümkün olup olmadığı soruldu.
- SPEC §5.0: çift yönlü pano (metin, HTML, görsel; dosya sonraki faz). §5.0.1: Mac kısayol
  eşleme (Cmd→Ctrl çevirme, tam yer değiştirme, özel eşleme tablosu).
- Ubuntu oturum araştırması bitti → `docs/research/2026-11-14-ubuntu-rdp-sessions.md`, SPEC §5.2
  güncellendi. Kullanıcı Ubuntu 24.04'te; Windows benzeri kalıcı oturum için 26.04 (GNOME 50)
  gerekiyor. Kullanıcıdan seçim bekleniyor.
- Kullanıcının Apple Developer hesabı var → ADR 0002 imzalama/notarization kesinleşti.
- SPEC §4.1: cihaz bazında şifre sakla, otomatik giriş, kapanınca kilitle, oturum seçimi.
  §5.2: Ubuntu'da oturumun kapanması sorunu (araştırma sürüyor).
- Otomatik güncelleme + otomatik release gereksinimi eklendi: SPEC §6, ADR 0002, CLAUDE.md'ye
  git akışı (development/master, Conventional Commits) ve "GitHub sadece MCP ile" kuralı.
  GitHub MCP araçları bu oturumda hâlâ yüklü değil (giriş hatası) → repo bekliyor.
- Lisans: **MIT** seçildi, `LICENSE` eklendi.
- **Karar:** FreeRDP 3 + Qt 6, Go netcore, açık kaynak, önce macOS. ADR 0001 → KABUL EDİLDİ.
  Kalan: proje lisansı seçimi, GP ön testi, GitHub reposu, iskelet kurulumu.
- Araştırmalar tamamlandı → `docs/research/` altına yazıldı. Öneri ADR'ı:
  `docs/decisions/0001-tech-stack.md` (durum: ÖNERİ). Ana bulgu: Tailscale için Go zorunlu
  → ağ katmanı Go netcore; önerilen UI/RDP: Qt 6 + FreeRDP 3.
- Teknoloji yığını araştırması başlatıldı (RDP motoru + UI; gömülü GP/Tailscale tünelleri; TOTP).
- Proje içi hafıza yapısı kuruldu: `CLAUDE.md` (KURAL 0), `AGENTS.md`, `docs/memory/`,
  `docs/decisions/`. `SPEC.md` → `docs/SPEC.md` taşındı.
- SPEC'e TOTP desteği eklendi (authenticator QR kodu ile secret içe aktarma, otomatik OTP).
- Gereksinimler `docs/SPEC.md` v0.1 olarak yazıldı: uygulama kilidi, GP + OTP, Tailscale,
  cihaz kayıtları, uygulama içi RDP, sekme/pencere çoklu monitör, düzenin cihaz bazında
  hatırlanması.
- GitHub reposu oluşturma denendi: `gh` kurulu değil, GitHub MCP girişi "Couldn't start
  sign-in" hatası verdi. Repo henüz yok.
