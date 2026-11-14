# Araştırma: Gömülü tüneller (GlobalProtect, Tailscale) + TOTP (2026-11-14)

Kaynak: alt ajan web araştırması. "Doğrulanmadı" işaretli noktalar test edilmeli.

## A) GlobalProtect — openconnect `--protocol=gp`
- Olgun ve aktif (v9.21, 2026-06). https://gitlab.com/openconnect/openconnect/-/tags ,
  https://www.infradead.org/openconnect/globalprotect.html
- **HIP:** `--csd-wrapper=hipreport.sh` (shell script). Bazı gateway'ler içerik yanlışsa sessizce
  erişim vermiyor. Windows için HIP üreticisini uygulamaya gömmek gerekir. https://www.infradead.org/openconnect/hip.html
- **Çift kimlik doğrulama:** openconnect portal'dan aldığı `portal-userauthcookie`'yi gateway'e gönderir.
  Gateway yine şifre+OTP istiyorsa bu **sunucu ayarı** (gateway portal cookie'sini kabul etmiyor).
  Challenge formları callback'lerle gelir → uygulama her soruda yeni TOTP üretebilir.
  openconnect'in kendi TOTP desteği de var (`--token-mode=totp`).
- **`--usergroup=gateway`** (veya URL sonuna `/gateway`): portal atlanır, sadece gateway'de tek
  giriş → çift OTP'den kurtulabilir. Kurumun gateway ayarına bağlı, **doğrulanmadı**.

### Userspace çalışma
- `--script-tun`: IP paketleri kernel TUN yerine UNIX soketle yardımcı programa gider, root gerekmez.
  - ocproxy (`-L 3389:host:3389`, SOCKS5, lwIP) — eski, düşük aktivite.
  - tunsocks — SOCKS/HTTP proxy, düşük aktivite. İkisi de Windows desteği belirtmiyor.
- **Windows engeli (kaynakta doğrulandı):** `tun-win32.c` → "Spawning tunnel scripts is not yet
  supported on Windows"; `openconnect_setup_tun_fd` Windows'ta TAP/Wintun handle bekliyor.
  → Windows'ta userspace için libopenconnect'e küçük bir yama (socketpair/pipe tun arka ucu) gerekir.
- **libopenconnect gömme:** POSIX'te `openconnect_setup_tun_fd(socketpair ucu)` + karşı uçta kendi
  TCP/IP yığınımız (gVisor netstack / smoltcp / lwIP).

### Alternatifler
- yuezk/GlobalProtect-openconnect: gpclient GPL-3.0, gpapi MIT; libopenconnect üstüne; Windows yok.
  gpapi protokol referansı olarak faydalı.
- Olgun saf Go/Rust GP istemcisi yok.

### Lisans
- libopenconnect LGPL-2.1 → dinamik bağlama + kaynak/değişiklik paylaşımı ile kapalı kaynakta da olur.
  Statik bağlamada relink yükümlülüğü. yuezk GPL-3 crate'leri kapalı kaynakta kullanılmamalı.

## B) Tailscale
- **tsnet (Go):** userspace netstack, TUN/root yok. https://tailscale.com/kb/1244/tsnet
  - AuthKey yoksa login URL üretir. `Dir`/`Store` (state, keychain'e konabilir), `ControlURL` (Headscale),
    `Ephemeral`. `Dial()` veya `Loopback()` (127.0.0.1 SOCKS5). https://pkg.go.dev/tailscale.com/tsnet
  - Headscale uyumu muhtemel ama tsnet özelinde **doğrulanmadı**.
- **libtailscale** (tsnet'in C API'si, BSD-3): POSIX fd tabanlı; **Windows'ta derlenmediği bildiriliyor
  (doğrulanmadı)**.
- **tailscale-rs** (resmi): v0.4.0, "unstable and insecure", NAT traversal yok → kullanılamaz.
- Sonuç: Tailscale için pratikte **Go şart**.

## C) TOTP
- `otpauth://totp/Issuer:hesap?secret=BASE32&issuer=&algorithm=SHA1&digits=6&period=30`
  (RFC 6238; https://github.com/google/google-authenticator/wiki/Key-Uri-Format)
- Google Authenticator dışa aktarma: `otpauth-migration://offline?data=<base64 protobuf>` (topluluk
  tarafından çözülmüş şema). MigrationPayload: otp_parameters=1 (tekrarlı), version=2, batch_size=3,
  batch_index=4, batch_id=5. OtpParameters: secret(bytes)=1, name=2, issuer=3, algorithm=4
  (1 SHA1, 2 SHA256, 3 SHA512, 4 MD5), digits=5 (1→6, 2→8), type=6 (1 HOTP, 2 TOTP), counter=7.
  Büyük dışa aktarmalar birden çok QR'a bölünür.

## Ajanın önerdiği ağ mimarisi
1. Tek bir **Go "netcore"** (c-shared kütüphane veya yan süreç): tsnet + cgo ile libopenconnect
   (dinamik) + **gVisor netstack** (socketpair tun ucu). Windows'ta tun-win32.c yaması.
2. RDP'ye açma: her oturum için `127.0.0.1:<rastgele>` dinleyici → `gonet.DialTCP` / `tsnet.Dial`.
   Üç işletim sisteminde aynı, fd geçirme yok.
3. Kimlik doğrulama callback'leri uygulamadan; her soruda şifre+TOTP. Önce `--usergroup=gateway` dene.
4. HIP üreticisi gömülü.

## Riskler
- Windows: libopenconnect yaması şart; libtailscale Windows'ta yok (Go netcore bunu çözer).
- Kurum resmi olmayan istemciyi / HIP içeriğini reddedebilir.
- Çift OTP sunucu ayarıysa kaçınılamaz → 30 sn pencere beklemesi.
- LGPL uyumu, kod imzalama, tünel içi DNS çözümleyici.
