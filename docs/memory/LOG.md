# İş Günlüğü

En yeni kayıt en üstte.

## 2026-11-15

- **`master` dalı ve kural setleri kuruldu (`gh api`):** `master` ilk commit'te (47a0d0e) oluşturuldu; dal
  oluşturma push'u release'i tetiklemesin diye Release iş akışı o an kapatılıp açıldı (çalışma yok). Kural
  setleri: `development` (silme/force push yok), `master` (yalnız PR + merge commit, zorunlu kontroller: Go netcore
  tests, macOS build, Release scripts, Analyze (go), Analyze (actions)), `release tags` (`v*` silinemez/taşınamaz).
  İlk `development` → `master` PR'ı ilk imzalı release'i çıkaracak.
- **Apple imza malzemesi incelendi:** `~/Downloads/apple-signing-*.zip` içinde Developer ID Application sertifikası
  (ekip `JTRUY8Y2ZF`, geçerlilik 2027-11-15'e kadar), eşleşen özel anahtar, iki .p12 ve notarization için uygulamaya
  özgü parola var. Release iş akışı ve güncelleyicide kod değişikliği gerekmiyor. GitHub `release` ortamı ve
  gizli değerler kullanıcı tarafından eklenecek (Claude parola/anahtar girmez; GitHub MCP'de secret aracı yok).
- **GitHub `release` ortamı `gh api` ile kuruldu:** dağıtım yalnız `master` dalından; `APPLE_TEAM_ID` eklendi.
  Kalan 4 secret için `.github/scripts/set-release-secrets.sh` yazıldı (gizli istemle okur, gh'ye stdin ile verir,
  .p12 parolasını önce doğrular). Kullanıcı artık GitHub işlerinde gh CLI'ye de izin verdi.
- Zip'teki iki .p12'nin parolası bilinmiyor (OpenSSL "Mac verify error"). Betiğe `developer-id.key cert.pem` modu
  eklendi: anahtar-sertifika eşleşmesini denetler, yeni parolayla SHA-1/3DES .p12 üretir (macOS `security import`
  ile test edildi).
- **Release secret'larının beşi de `release` ortamında** (`gh secret list --env release`, 12:17 UTC). Betik yalnızca
  .p12 parolası doğrulanınca kaydettiği için CSC_LINK/CSC_KEY_PASSWORD çifti tutarlı. İlk gerçek kanıt ilk release olacak.
  İmza zip'i ve açılmış klasör Downloads'tan silindi.
- **macOS dosya aktarımı araştırıldı, ADR 0009 yazıldı:** standart RFB'de dosya aktarımı yok; Extended Clipboard
  (0xC0A1E5CE) Mac'te panoya bağlanmıyor; Apple panosu (0x1f, zDesk belgeli) yalnız dosya yolu taşıyor; Ekran
  Paylaşımı'nın dosya kopyası belgesiz `SSFileCopySender/Receiver` ile. Dört seçenek sunuldu (Apple panosu + SFTP,
  yalnız SFTP, SSFileCopy tersine mühendislik, SMB). Kullanıcı **tersine mühendisliği** seçti. Önce Apple panosu,
  sonra süreç içi izlemeyle tel biçimi; ~5 gün sonra kontrol noktası. Kod henüz yazılmadı.
- **Dosya panosu iki yönlü ve platformdan bağımsız:** kullanıcı "her işletim sisteminden her işletim sistemine,
  iki yönlü" istedi. Yerelden uzağa: panodaki dosya URL'leri (Finder, Explorer, Nautilus) klasörler açılarak
  listeleniyor, `FileGroupDescriptorW` (id 0xC0F1) olarak sunuluyor; uzak taraf `FileContents` ile parça istedikçe
  dosya okunuyor (en fazla 8 MiB/istek, yalnızca listedeki dosyalar, klasör içindeki symlink'ler atlanıyor,
  260+ karakterlik adlar atlanıyor). Uzaktan gelenler Linux'ta Nautilus için `x-special/gnome-copied-files` da alıyor;
  Windows'ta geçersiz karakterler `_` oluyor. Metinlerden "Mac" çıkarıldı ("bu bilgisayar"). macOS hedefleri (VNC)
  yalnızca metin: editör bunu söylüyor. Gidiş-dönüş birim testi geçti; Windows/Linux derlemesi ve gerçek sunucuyla
  uçtan uca deneme yapılmadı.
- **Ubuntu→Mac dosya kopyalama (pano):** kullanıcı Ubuntu'da kopyaladığı dosyayı Mac'e yapıştıramıyordu. Neden:
  `RdpClipboard` yalnızca metin (CF_UNICODETEXT) ve görsel (CF_DIB) taşıyordu; dosya listesi hiç yoktu. Eklendi:
  istemci `CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS | CB_HUGE_FILE_SUPPORT_ENABLED` bildiriyor;
  sunucu `FileGroupDescriptorW` sunarsa liste alınıp `FileContents` ile 1 MiB parçalarla
  `~/Library/Caches/<app>/Pasted Files/<oturum>/<n>/` altına iniyor, sonra dosya URL'leri Mac panosuna konuyor
  (Finder'da Cmd+V). Sınır 2 GB / 10.000 öğe; yol temizliği (`..`, mutlak yol, sürücü harfi reddedilir); yeni kopyada
  iş iptal; klasör oturum kapanınca, çökme artığı 1 gün sonra silinir. Oturum penceresinde toast (`RemoteSession::notice`).
  Mac→Ubuntu dosya yok; cihaz editörü bunu açıkça söylüyor. SPEC §5.0 güncellendi (kullanıcı kararı: dosyalar
  geçici olarak diske yazılabilir). Birim testi `clipboardFileList` geçti; gerçek GNOME Remote Desktop/xrdp ile
  uçtan uca deneme kullanıcıda bekliyor (lokal derleme + demo açılışı hatasız).
- **README JumpDesk formatında yeniden yazıldı:** ortalanmış logo + başlık, CI/lisans rozetleri, giriş, öne çıkan bölüm
  ("Tunnels inside the app" + Security), Features, Screenshots tablosu, Download, Development, Branches and releases,
  Packaging, Issues and contributions, License. Türkçe bölüm kaldırıldı (README tamamen İngilizce).
  `tools/readme-shots.sh` 8 görüntü üretiyor (kilit ekranı, tünel listesi, Settings, Tailscale editörü eklendi).
- **İkinci critique 28/40 + düzeltmeler:** Cmd+F aramaya odaklar, Cmd+, Settings'i açar (dialog açıkken çalışmaz), kartta
  sağ tık menüsü. Tünel editörü: OTP Type seçiminin ne demek olduğunu söyleyen satır, Gateway / Group ipucu,
  Cloudflare test alanı "Hostname To Test" + yalnızca test için olduğunu söyleyen ipucu (alan zaten profile
  kaydedilmiyordu; critique'teki "yanlış yerde" bulgusu kısmen yanlıştı). Settings ayrı pencere: yapılmayacak (kullanıcı kararı).
- **Impeccable polish:** Türkçe arayüz, açık/koyu temada 20 ekran kontrol edildi; taşma/kesilme yok. README görüntüleri
  (`docs/images/`) yeni renklerle `tools/readme-shots.sh` ile yeniden çekildi (çerçeve + gölge, 1600 px).
  İlk critique kaydı `app/qml` değiştiği için araç tarafından kapatıldı. Kalan: elle VoiceOver testi.
- **Impeccable layout:** `BDialog` uzun içerikte kaydırma işareti (başlık altında ince çizgi, footer'a doğru fade);
  tünel editöründe Key tam genişlik, Algorithm/Digits/Period "Advanced Settings" altında (standart dışı değer
  varsa kendiliğinden açılır). Tünel satırı hizası zaten doğruydu (critique bulgusu yanlıştı).
- **Impeccable typeset:** `Theme.qml`'e role göre yazı ölçeği (9/11/12/13/14/15/18/22); 81 sabit `pixelSize` token'a
  taşındı (17→18, 20→22). `SectionTitle` 15 DemiBold + üstte 14 px boşluk, `Heading` rolü; Settings ve tünel
  editöründe bölümler artık alan etiketlerinden ayrışıyor. Connect butonuna dokunulmadı (kullanıcı kararı).
- **Impeccable harden:** VoiceOver adları (BButton tip→ad, BSwitch, BField, Chip, Segmented, kart, "Connect To %1",
  kart menüsü klavye odağında görünür), Chip/Segmented klavye desteği + odak halkası, gösterim çipleri `interactive: false`.
  Sonuçsuz aramada neyin gizlediğini söyleyen mesaj + Clear Search / Show All Devices; aramada Esc temizler.
  "1 devices" düzeltildi. 15 yeni metin çevrildi. Demo: `BURROW_DEMO_OPEN=nomatch`. VoiceOver ağacı terminalden
  doğrulanamadı (System Events izni yok); elle VoiceOver testi bekliyor.
- **Impeccable critique + colorize:** `app/qml` ilk critique 27/40 (`.impeccable/critique/`). Colorize: GlobalProtect
  kırmızı → denim, Cloudflare turuncu → erik (danger/warning/vurgu ile çakışıyordu), kart yer tutucusundaki mavi-mor
  gradyan → OS başına tek tonlu koyu zemin. Kontrast ≥4.9:1 hesaplandı, açık/koyu ekran görüntüsüyle doğrulandı.
  `docs/images/` README görüntüleri henüz eski renklerde.
- Güvenlik incelemesi (CI): govulncheck `@v1.1.4`'e sabitlendi; CodeQL PR'larda da çalışıyor (yalnız development/master hedefli;
  fork PR'ları zaten salt-okunur jeton alır, `pull_request_target` kullanılmaz). Eylemleri SHA'ya sabitleme doğrulanamadığı için yapılmadı; Dependabot etiketleri izliyor.

- GitHub hazırlığı: README güncel özelliklerle yeniden yazıldı (RDP + H.264, Mac/VNC, tüneller, güvenlik, kurulum;
  açık/koyu temaya göre değişen galeri görüntüsü, demo verisiyle yeni ekran görüntüleri). `SECURITY.md`,
  `.github/dependabot.yml` (Actions + Go, haftalık), `codeql.yml` (Go + Actions), CI'da ayrı `govulncheck` işi.
  GitHub tarafı ayarlar (açıklama, konular, varsayılan branch, kurallar, `release` ortamı ve sırlar, güvenlik
  ayarları) `docs/github-settings.md`'de; GitHub MCP bağlanamadığı için ("Incompatible auth server: does not
  support dynamic client registration") henüz uygulanmadı. Yerel govulncheck çalıştırması izin sistemince
  reddedildi; sonuç ilk CI çalışmasında görülecek.

- Güvenlik incelemesi (RDP codec): eski RemoteFX kapatıldı (Progressive kapsıyor), H.264 yalnız FreeRDP
  `WITH_GFX_H264=ON` ile derliyse isteniyor, CMake en az FreeRDP 3.33 istiyor (test edilen sürüm).

- RDP internet hızı: FreeRDP'de H.264 ve diğer sıkıştırmalar varsayılan kapalıydı. Artık GFX H.264 (AVC444v2/AVC444,
  AVC420) — çözme Mac donanımında (FreeRDP VideoToolbox ile derli) — ve yedek olarak Progressive RemoteFX isteniyor;
  bağlantı türü `freerdp_set_connection_type(AUTODETECT)` ile, sunucu süsleri ölçülen hıza göre ayarlıyor. Yerel
  shadow sunucusuyla AVC444v2 anlaşıldı, hatasız çözüldü. Gerçek Windows/GNOME sunucuda denenmedi.

- Güvenlik incelemesi: VNC okuma iş parçacığı dolu kuyrukta beklerken oturum kapanırsa sonsuza kadar bekliyordu
  (join takılırdı). Bekleme artık kapanış bayrağını (`m_ioDone`) da dinliyor.

- Kullanıcı "aşırı yavaş" dedi (ölçümde 30–38 kare/s). Neden büyük ihtimalle girdi gecikmesi: Mac güncelleme
  yazarken girdiyi okumuyor; aralık 0'da sürekli değişen ekranda tıklama/tuşlar saniyelerce bekliyor (zDesk
  ölçümü). `AutoFrameBufferUpdate` aralığı artık 33.333 µs (~30/s). Ayrıca: görünüm görüntüyü 512 px karolara
  bölüp GPU'ya yalnız değişen karoları yüklüyor; VNC'de soket okuma + kayıt şifre çözme ayrı iş parçacığında
  (32 MB'a kadar kuyruk), çözücü ağı beklemiyor. Apple ve standart VNC yolları, kapanışta kilit ve RDP doğrulandı.

- Adaptive gerçek ölçüm: veri 55–143 → 1–2 Mbit/s, kare 4–6 → 20–25/s; ama çözücü %98 dolu (15–17 ms/güncelleme)
  çünkü yerel derleme **Debug (-O0)**. Görüntü kodu (`AppleAdaptive.cpp`, `VncSession.cpp`, `RdpView.cpp`) artık
  Debug'da da `-O2` derleniyor: tam Retina karesi (3456×2234, 120.960 DCT karosu) 661 → 35 ms (tst_vault
  `adaptiveSpeed`). Renk DC kısayolu ve tam yazılan karolarda okuma atlama eklendi (testler geçiyor).

- Ölçüm: Burrow artık darboğaz değil (görünür ekrana dokunan güncellemelerin hepsi kareye dönüşüyor, okuma
  %25–32 dolu); veri Mac'ten yavaş geliyor. **Apple Adaptive (0x3f3)** eklendi: zDesk çözücüsünün C++ çevirisi
  (`src/vnc/AppleAdaptive.*`), zDesk testleri tst_vault'a taşındı ve geçiyor; sahte Apple sunucusu gerçek 0x3f3
  akışı (tablolar, renk karoları, DCT, iyileştirme, önbellek) gönderiyor, uçtan uca doğrulandı. Çözülemeyen
  dikdörtgende oturum kopmadan ZRLE'ye dönüyor (test edildi). ADR 0008 ve THIRD_PARTY_NOTICES güncellendi.

- GPU görünümü: görüntü gelmeden boş resim düğümü çizildiği için "No QSGTexture provided" uyarısı yağıyordu; düğüm
  artık yalnız görüntü varken var. "view:" satırı görünüme dokunan güncelleme, yenileme ve kare sayısını birlikte
  gösteriyor (kaybın hangi aşamada olduğu görülsün). Kullanıcının ölçümleri Burrow'un çalıştığı Mac'in kendisine
  yapılıyor (kendini besleyen döngü, 49–166 Mbit/s); temsilî değil.

- Ölçüm (yine aynı Mac): yeni çözücüyle güncellemeler 2–4 kat arttı, işlemci 4–5 ms/güncelleme, okuma %20–47 dolu;
  ama pencereler 5 sn'de yalnız 6–109 kez yenileniyordu. Neden: `QQuickPaintedItem` her karede Retina ekranı
  işlemcide yumuşatarak ölçekleyip çiziyordu. `RdpView` artık `QQuickItem` + scene graph dokusu
  (`QSGImageNode`): ölçeklemeyi GPU yapıyor; "view:" satırı saniyedeki kareyi gösteriyor. Sahte Apple sunucusunda
  her güncelleme bir kare (10/s), yerel RDP'de 24 kare/s.

- Gerçek ölçüm (kullanıcı yine Burrow'un çalıştığı Mac'e bağlandı → görüntü kendini besleyen döngü, en kötü
  durum): 30–88 Mbit/s, 5 sn'de 250–400 Mpx, güncelleme başına 32–78 ms; okuma iş parçacığı dolu, arayüz kopyası
  önemsiz. ZRLE çözücü satır bazlı yeniden yazıldı (ham karo satır satır, tek renk/RLE `fill_n`, çıktı kalıcı
  tampona tek seferde açılıyor); hız satırı artık işlemci süresini ve doluluk yüzdesini ayrı gösteriyor
  ("cpu", "busy"). Gerçek bir başka Mac'te tekrar ölçülecek; yetmezse Adaptive (0x3f3).

- Mac oturumu: kapanırken Control+Command+Q ile kilitleme ("Bağlantı Kesilince Kilitle", Mac'te de Session
  sekmesinde); Apple imleç şekilleri (0x450 önbelleği: BGRA + ayrı alfa düzlemi, seçim/kayıt) yerel imlece
  uygulanıyor; VNC 5 sn'de bir hız satırı loglar (güncelleme, MB, Mbit/s, Mpx, çözme ve kopya süresi).
- Görünüm hızlandı (RDP ve VNC): her güncellemede tüm ekranı kopyalayıp çizmek yerine yalnız değişen
  dikdörtgenler kopyalanıyor (`copyInto`, ara görüntü yok) ve yalnız o bölge yeniden çiziliyor. Sahte Apple
  sunucusunda hareketli desenle ve yerel RDP sunucusuyla doğrulandı. Adaptive formatı ölçüm sonucuna göre.

- Güvenlik incelemesi (Apple VNC): kayıt katmanı yalnız tamamlanmış bir Apple (tür 30) girişinin anahtarıyla
  başlar (başka giriş türünde 003.889 reddedilir); DH'de sunucunun açık değeri (1, p-1) aralığında ve asal
  uzunluğu doğru olmalı; kayıt gönderilemezse oturum kapanır ve hiçbir şey açık metin gitmez; bütünlük hatası ayrı
  hata mesajı verir. Not: kayıtlardaki SHA-1 etiketi Apple protokolünün kendisi gereği anahtarsızdır (şifreli
  blok içinde); güvenlik sınırı sayılmaz, değiştirilemez.

- **Mac çoklu ekran (Apple yolu):** gerçek Mac standart `ExtendedDesktopSize` bildirmedi (log). VNC artık Mac'e
  RFB 003.889 ile bağlanıyor: ViewerInfo/SetMode/SetEncryption ön adımı, anahtar değişimi, şifreli kayıt katmanı
  (AES-128-CBC + SHA-1), `AppleDisplayLayout` ile ekranlar sekmelere bölünüyor, `AutoFrameBufferUpdate` ile
  güncellemeler; sağ tık bitleri ve Option→Meta düzeltmesi. Yerel sahte Apple sunucusuyla (scratchpad/vnc/
  apple_server.py, libcrypto ctypes) uçtan uca doğrulandı; standart 3.8 yolu da tekrar test edildi. Gerçek Mac'te
  denenmedi. zDesk (MIT) atfı `THIRD_PARTY_NOTICES.md`'de; ADR 0008 güncellendi. Apple modunda pano yok.

- QR menüsü düğmenin sağ kenarına hizalı açılıyor (önceden pencerenin dışına taşıyordu); iki temada görüntüyle
  doğrulandı. Geliştirme kancası `BURROW_DEMO_OPEN=tunnel-qr`.

- Açılır menüler yeniden tasarlandı: ortak `BPopupSurface` (yükseltilmiş yüzey, 1px kenar, yumuşak gölge;
  tooltip de bunu kullanıyor), `BMenu` + `BMenuItem` (ikon, seçili öğede onay, tehlikeli işlemde kırmızı, hover/
  basılı/pasif durumlar, kısa açılış geçişi). `BCombo`: Unicode `⌄` glifi yerine SVG chevron (açılınca döner), hover
  kenarı, seçili öğede onay. Cihaz kartı, sekme ve QR menüleri bunlara geçti (QR menüsü daha önce hiç stillenmemişti).
  Geliştirme kancası `BURROW_DEMO_OPEN=combo`.

- Şifre alanlarına göster/gizle: `BField` (`password: true`) sağında göz ikonlu ghost düğme (`eye`/`eye-off`
  SVG'leri eklendi). Düğme odak almaz (Tab akışı ve imleç alanda kalır); alan gizlenince yeniden maskelenir.
  Tüm şifre alanları (kilit ekranı, ayarlar, cihaz/tünel editörü, kimlik ve VPN istemi, token) bunu otomatik alır.
- VNC çoklu ekran, standart yol: `ExtendedDesktopSize` (-308) istenip ayrıştırılıyor; sunucu birden çok ekran
  bildirirse her biri RDP'deki gibi ayrı sekme/pencere (test sunucusuyla doğrulandı). Bildirmezse logda
  "the server doesn't report its screens" yazar. Apple'ın kendi düzen mesajı (AppleDisplayLayout 0x451) yalnız
  003.889 el sıkışması + şifreli kayıt katmanından sonra geliyor (zdesk projesinin tersine mühendislik notları);
  standart yol yetmezse bu ikinci adım. Kullanıcının denediği "macbook-pro" Burrow'un çalıştığı Mac'in kendisiydi
  (görüntü kendini tekrar ediyordu).

- Tailscale cihaz içe aktarma "Loading"de kalıyordu: pencere listeyi yalnız tünel durumu *değişince* alıyordu;
  "Tailscale İle Giriş Yap"tan sonra tünel zaten bağlı olduğu için değişiklik gelmiyordu. Artık açılışta durum hemen
  kontrol ediliyor; bağlandıktan hemen sonra makine listesi boşsa 1 sn arayla 6 kez yeniden soruluyor. Bu tünelde
  zaten ekli makineler (MagicDNS adı ya da Tailscale IP'si eşleşirse) "Zaten Ekli" olarak gösteriliyor, seçilemiyor
  ve `importDevices` de atlıyor (`hasDevice`). Tailscale'den gelen macOS makineleri macOS/VNC/5900 olarak ekleniyor.

- **VNC / macOS (ADR 0008) ilk sürüm:** ortak `RemoteSession` arayüzü (RdpView, oturum pencereleri ve
  AppController artık protokolden bağımsız; `RdpSession` bunu uygular). Yeni `VncSession` (src/vnc): RFB 3.8, Apple
  girişi (tür 30: DH + AES-128-ECB, MD5 anahtar; OpenSSL), VNC şifresi (tür 2, DES; `openssl enc` ile doğrulandı,
  birim testi), Raw/CopyRect/ZRLE/DesktopSize, işaretçi + tekerlek, keysym ile klavye (görünüm `usesKeysyms`
  oturumlara karakteri gönderir; Cmd → Super_L, Option → Alt), düz metin pano. Cihaz kaydında `protocol` (rdp|vnc),
  `os`'ta `macos`; düzenleyicide macOS seçilince port 5900, Domain/Session ayarları gizli, Ekran Paylaşımı açıklaması.
  Yerel Python RFB test sunucusuyla (scratchpad) tüm ZRLE alt türleri, Raw, CopyRect ve girdi olayları doğrulandı;
  bu Mac'teki Apple sunucusu tür 30 [30, 33, 36, 35] sunuyor. Apple girişi kullanıcının gerçek Mac'inde henüz
  denenmedi (şifre girilmedi). Geliştirme kancaları: `BURROW_DEMO_PROTOCOL=vnc`, `BURROW_DEMO_INPUT=1`,
  `BURROW_DEMO_OPEN=device-mac`.
- Not (test hatası): `cliclick` ile gönderilen tuşlar Burrow öne gelmediği için kullanıcının ön plandaki uygulamasına
  gitmiş olabilir; panodaki metin yerel test sunucusuna ulaştı, log silindi. Sentetik masaüstü girdisi artık
  kullanılmıyor; girdi testleri uygulama içi kancayla yapılıyor ve demo VNC cihazında pano kapalı.

- **Karar:** MacBook hedefleri için gömülü, kendi yazdığımız VNC (RFB) istemcisi; Apple Uzaktan Yönetim girişi + VNC
  şifresi, tünel üzerinden, aynı pencere/sekme deneyimi. libvncclient GPL olduğu için elendi. ADR 0008.

- Demo verisi (`--seed-demo`) artık çizim değil gerçek ekran görüntüleri kullanıyor: Wikimedia Commons'tan GPL
  lisanslı Ubuntu 24.04 görselleri (App Center, Dosyalar, masaüstü) `docs/assets/demo/` altında, kaynak/lisans
  `ATTRIBUTION.md`'de; uygulamaya gömülmüyor, kaynak ağacından okunuyor (`BURROW_DEMO_ASSETS`). Windows için özgür
  lisanslı yüksek çözünürlüklü görsel yok (Wikipedia'dakiler düşük çözünürlük + adil kullanım; Microsoft izni kırpmaya
  izin vermiyor), demo Windows cihazı önizlemesiz durumu gösteriyor. "Home Desktop" artık Ubuntu.

- Cihaz kartı önizlemesi pencere boyutlanırken yanıp sönüyordu: görsel `sourceSize`'ı kart boyutuna bağlıydı, her
  adımda yeniden yükleniyor ve arada "Preview After First Connection" görünüyordu. Artık sabit boyutta (1280 px)
  çözülüyor, yüklenirken eskisi kalıyor (`retainWhileLoading`), yer tutucu yalnız görsel yoksa çıkıyor.
- Sekme adı düzenleme: alan dışında herhangi bir yere tıklamak (ya da odağın gitmesi) Esc gibi düzenlemeyi iptal
  ederek bitirir; tıklama tıklanan yere de ulaşır. Enter kaydeder.
- Tooltip'ler: yeni `BToolTip` (yükseltilmiş yüzey, 1px kenar, yumuşak gölge, 550 ms gecikme, kısa geçiş);
  `BButton.tip` ile kullanılır. Ayarlar penceresindeki "Lock Now" butonu kaldırıldı (kilit başlık çubuğunda).

- Tema seçimi: Ayarlar → Görünüm (Sistemle Aynı / Açık / Koyu). Dil gibi `QSettings` `ui/theme`'da (kilit ekranı da
  uyar), `QStyleHints::setColorScheme` ile tüm uygulamaya (macOS başlık çubukları dahil) uygulanır; `Theme.dark` bunu
  izler. QML'deki son sabit renkler Theme token'larına taşındı (önizleme üstü metin/örtü, toast, modal örtü, gölge).
  Kontrast ≥4.5:1 için açık temada vurgu, durum, tünel ve soluk metin renkleri koyulaştırıldı; koyu temada vurgu
  (#9C6B34) ve soluk metin (#858C99) ayarlandı.

- Pencere minimum genişliği artık galeri başlık satırının doğal genişliğinden hesaplanıyor (dile ve güncelleme
  rozetine göre değişir; en az 760). En dar hâlde EN 841, TR 815 pt; hiçbir buton kesilmiyor. Minimum yükseklik 520.

- Çoklu monitörde kopma ve siyah kenar kesin çözüm: GNOME kaynağı ve loglar incelendi. Sunucu oturumu hata kodu
  vermeden normal kapatıyor (FreeRDP bunu `ERRINFO_LOGOFF_BY_USER` gösteriyor); tetikleyen, oturum devrinden sonra
  canlı gönderilen monitör düzeni. GNOME Remote Login canlı düzen değişikliğinde oturumu kapatıyor ya da yeniden
  devredip eski boyutlara dönüyor. Artık sunucu oturumu devrettiyse (`FreeRDP_RedirectionFlags` ≠ 0) canlı düzen
  hiç gönderilmez; sunucu düzenini bildirmeden de gönderilmez. `layout.sizes` artık sunucunun boyutu değil, her
  monitörün resminin gerçekten gösterildiği alanın boyutu (ayrık pencere ya da sekme görünümü); sonraki bağlantı
  tam bu boyutlarla başlar. Arada kalan küçük oran farkında (%8'e kadar) resim görünümü kenarsız doldurur.

- Güvenlik incelemesi (monitör düzeni): sunucunun bildirdiği monitörler en fazla 16 ve makul boyutla sınırlı,
  geçersizse yok sayılır; `setMonitorDefs` sayı/boyutu doğrular ve canlı bağlantıda masaüstü boyutuna dokunmaz
  (yalnız bağlanmadan önce); sunucu düzeni reddederse yeniden istek en fazla 3 kez, artan aralıkla.
- GNOME oturumu girişten sonra bir kez daha devrediyor (ikinci sunucu yönlendirmesi); yeni bağlantı FreeRDP
  ayarlarındaki ilk monitör listesini gönderdiği için monitörler ilk boyutlara dönüp siyah kenar kalıyordu. Artık her
  düzen isteğinde `MonitorDefArray` de güncelleniyor; sunucu istenenden farklı düzene dönerse düzen yeniden isteniyor.
- Başlık çubuğu: kontroller 32 pt, üst/alt/sağ boşluk eşit (10 pt, `Theme.titleBarMargin`), başlık metni trafik
  ışıklarıyla optik olarak hizalı.
- Çoklu monitörde bağlantının kendiliğinden kopması: GNOME, bir düzen değişikliğinin yeniden kurulumu bitmeden yeni
  bir istek gelince oturumu kapatıyordu (`ERRINFO_LOGOFF_BY_USER`, pencereler yerleşirken saniyeler içinde 3 istek).
  Artık istekler sırayla: bağlantı/devir sonrası 2,5 sn beklenir, önceki düzen uygulanmadan (en fazla 6 sn) yenisi
  gönderilmez, sunucuda zaten aynıysa gönderilmez, 600 ms birleştirme. Son monitör boyutları `layout.sizes` olarak
  kaydedilir ve sonraki bağlantı doğrudan bu boyutlarla başlar (ekran sayısı aynıysa), böylece siyah kenar kalmaz.
- Tailscale/Cloudflare bağlama sadeleşti: yeni profil kimliği baştan üretilir, Tailscale node adı otomatik
  (`burrow-<bilgisayar adı>`), Control Server/Node Name ve Cloudflare bastion "Gelişmiş Ayarlar"da. Tailscale'de
  "Tailscale İle Giriş Yap" kaydedip cihaz içe aktarmayı açar (tarayıcı girişi → tailnet makineleri). Cloudflare'de
  "Kaydet Ve Cihaz Ekle" → "Cloudflare'de Token Oluştur" izni önceden seçili token sayfasını açar; kopyalanan token
  Burrow öne gelince (veya "Token'ı Yapıştır") panodan alınır, bir kez kullanılır, pano temizlenir (ADR 0007).
- Monitör boyutları canlı: her uzak monitör onu gösteren pencerenin boyutunu alır (disp kanalı, tüm düzen yan yana,
  250 ms birleştirme). Tam ekranda çentik/oran farkından kalan siyah boşluklar ve farklı boyutlu ekranda dar görüntü
  bunun içindi. Ana oturum penceresinin ekranı ve tam ekran durumu `layout.main` olarak kaydedilir; bağlantı
  başlarken o ekranda ortalanmış açılır, gerekiyorsa tam ekran olur. Yeni cihaz penceresi ana ekranda ortalanır.
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
