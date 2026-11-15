# ADR 0010 — Windows portu

- **Durum:** KABUL EDİLDİ (2026-11-15)
- **Karar verenler:** kullanıcı + AI asistan
- Kullanıcının isteği: Windows'ta paket, imza ve güncelleme davranışı **JumpDesk ile aynı** olsun.

## Bağlam

Burrow macOS ve Ubuntu'da çıktı (v0.1.0). Windows'ta değişmesi gerekenler:

- **Go netcore cgo kullanıyor** (`internal/gp` → libopenconnect). cgo Windows'ta gcc (MinGW) ister; Go'nun
  c-archive çıktısı MSVC ile resmi olarak linklenmez.
- **libopenconnect hiçbir Windows paket deposunda yok** (MSYS2, vcpkg). Kaynaktan derlenmeli.
- **GP tüneli:** openconnect Windows'ta `openconnect_setup_tun_fd()`'ye bir dosya tanıtıcısı (HANDLE) alır ve
  üzerinde overlapped `ReadFile`/`WriteFile` yapar (TAP sürücüsü yolu). Unix'teki `socketpair` yok.
- **RDP/VNC soket çifti:** `RdpSession`/`VncSession` netcore'a `socketpair(AF_UNIX)` ile bağlanıyor; Windows'ta
  yok. Go tarafı `net.FileConn` kullanıyor; Windows'ta desteklenmiyor.
- macOS'a özel: `SO_NOSIGPIPE` (Windows'ta SIGPIPE yok), Touch ID (stub), `Updater` içindeki `codesign`/`ditto`.
- Kasa ve dosya yolları `QStandardPaths` ile zaten taşınabilir (`%APPDATA%\Burrow`). `0600` izni Windows'ta
  yalnız salt-okunur bitine dokunur; dosya kullanıcı profilindedir, varsayılan ACL yalnız kullanıcıya açıktır.

## Kararlar

1. **Araç zinciri: MSYS2 UCRT64 (MinGW-w64 gcc).** Qt 6 (6.12), FreeRDP 3.33.1, OpenSSL 3, zlib MSYS2
   paketlerinden; Go c-archive aynı gcc ile doğal linklenir. MSVC + vcpkg reddedildi (Qt/FreeRDP kaynaktan
   1–2 saat, Go c-archive için ek köprü DLL).
2. **libopenconnect kaynaktan** (`tools/windows-deps.sh`): sabit sürüm + SHA-256, GnuTLS ve libxml2 MSYS2'den.
   CI'da önbelleklenir (Linux işindeki gibi yalnız kendi dallarımıza push önbelleği yazar).
3. **GP tüneli yamasız:** Go tarafı **mesaj modlu, overlapped bir named pipe** açar (rastgele ad, tek örnek,
   yalnız bu kullanıcıya açık, uzak istemci reddedilir); istemci ucu `CreateFile(FILE_FLAG_OVERLAPPED)` ile
   açılıp openconnect'e verilir. Mesaj modu paket sınırlarını korur (bir `ReadFile` = bir IP paketi), TAP
   yolunun beklediği davranış budur. Paketlerin önünde adres ailesi başlığı yok.
   Gerçek GP sunucusuyla doğrulama kullanıcıda (CI'da GP sunucusu yok); pipe davranışı Go testiyle sınanır.
4. **RDP/VNC soket çifti:** Windows'ta loopback TCP çifti (127.0.0.1:0'da dinle, bağlan, kabul et, dinleyiciyi
   hemen kapat; kabul edilen bağlantının karşı ucu bizim bağlantımızın yerel adresi değilse çift reddedilir —
   başka yerel süreç araya giremez). Go tarafı Windows'ta SOCKET'i engelleyen `recv`/`send` ile saran bir
   `net.Conn` kullanır.
5. **Kurulum paketi (JumpDesk gibi):** NSIS ile **kullanıcı başına kurulum** `Burrow-X.Y.Z-windows-x64-setup.exe`
   (yönetici izni yok, `%LOCALAPPDATA%\Programs\Burrow`, Başlat menüsü ve masaüstü kısayolu, kaldırıcı) ve
   **taşınabilir** `Burrow-X.Y.Z-windows-x64-portable.zip`. (JumpDesk'in taşınabilir sürümü tek exe; Qt
   uygulamasında her açılışta yüzlerce MB açmamak için zip.) İlk tartışmada Inno Setup seçilmişti; kullanıcı
   JumpDesk ile aynılığı istediği için NSIS'e geçildi.
6. **Kod imzalama:** ilk Windows sürümü **imzasız** (JumpDesk gibi; SmartScreen "Bilinmeyen yayıncı" uyarısı:
   Daha fazla bilgi → Yine de çalıştır). Sertifika gelince CI'a imza adımı eklenir.
7. **Uygulama içi güncelleme (JumpDesk/electron-updater davranışı):** Windows `latest.json`'a girer
   (`windows-x64` → setup.exe). Doğrulama: manifestteki SHA-256 tutmalı; **çalışan Burrow Authenticode ile
   imzalıysa** yeni kurulum dosyası da geçerli imzalı olmalı ve aynı yayıncıyı taşımalı (imzasız sürüm bu
   ikinci kontrolü yapamaz — electron-updater ile aynı). Kurulum: Burrow kapanır, kurulum dosyası sessiz
   (`/S`) çalışır, Burrow yeniden açılır. Taşınabilir sürüm kendini güncellemez (bildirimi gösterir, indirme
   GitHub'a yönlendirir).
8. **Windows Hello:** bu portta yok (kasa ana şifreyle açılır); sonraki faz.

## Sonuçlar

- CI'a `Windows build` işi (windows-2025, MSYS2) eklenir ve `master`'ın zorunlu kontrollerine girer.
- `release.yml`'e `windows` işi: kurulum + taşınabilir paket + manifest parçası.
- İmzasız sürümde güncellemenin güvenliği GitHub hesabının ve HTTPS'in güvenliğine dayanır (macOS'taki ikinci
  kontrol yok). Sertifika alındığında aynı-yayıncı kontrolü kendiliğinden devreye girer.
