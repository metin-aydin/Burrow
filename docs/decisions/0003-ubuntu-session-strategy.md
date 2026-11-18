# ADR 0003 — Ubuntu oturum stratejisi

- **Durum:** KABUL EDİLDİ (2026-11-14) — teknik doğrulama testleri bekleniyor
- **Araştırma:** [Ubuntu RDP oturumları](../research/2026-11-14-ubuntu-rdp-sessions.md)

## Bağlam
GNOME kullanıcı başına tek grafik oturuma izin verdiği için uzaktan bağlanınca yerel oturum kapanıyor
(veya tersi). Kullanıcı Ubuntu 24.04'te, yükseltme yapamıyor; çözüm 22.04+ tüm sürümlerde çalışmalı.
Kullanıcı Ubuntu'ya yardımcı servis kurulabileceğini onayladı (sudo var).

## Karar
1. **Ana mod — Mevcut oturum:** GNOME Desktop Sharing (tüm sürümler) + **burrow-agent**.
2. **burrow-agent** (Ubuntu'da systemd user servisi, Go):
   - İstemci bağlanmadan önce oturum kilidini açar (`loginctl unlock-session`).
   - Bağlantı bitince/kopunca oturumu kilitler (`loginctl lock-session`) — kopmayı agent kendisi
     algılar (istemci heartbeat'i kesilirse).
   - Bağlıyken fiziksel monitörü kapatmayı dener (Mutter `PowerSaveMode`).
   - Sadece tünel arayüzünden erişilebilir; karşılıklı kimlik doğrulama (cihaz eşleştirmede üretilen
     anahtar çifti). Her komut imzalı, tekrar saldırısına karşı nonce/zaman damgası.
3. **Opsiyonel — Ayrı kalıcı oturum:** GNOME 47+ → Remote Login; 22.04/24.04 → xrdp + ayrı kullanıcı.
4. **Kurulum betiği** (`burrow-agent install`): sürümü algılar; Desktop Sharing'i açar, portları ve
   güvenlik duvarını ayarlar, agent'ı kurar, eşleştirme kodu üretir.

## Doğrulanacaklar (prototipte)
- [x] `loginctl unlock-session` kullanıcı yetkisiyle (polkit) çalışıyor mu? **Evet** (GNOME 47, uzak oturumda; fiziksel
  oturumda aynısı bekleniyor, denenmedi).
- [ ] Kilit açıldıktan sonra Desktop Sharing bağlantısı kabul ediliyor mu (22.04 / 24.04 / 26.04)?
- [ ] Monitör kapalıyken (PowerSaveMode) ekran paylaşımı görüntü vermeye devam ediyor mu?
- [ ] Super+L / `lock-session` sonrası bir sonraki bağlantı agent ile sorunsuz mu?

## Doğrulama sonuçları (2026-11-19, `tools/agent-probe.sh`)
Makine: **Ubuntu 24.10, GNOME Shell 47.0, gnome-remote-desktop 47.0** (ADR'nin varsaydığı 24.04/GNOME 46 değil).
Betik bir **uzak oturumda** çalıştı (`Remote=yes`, koltuk/seat yok): kullanıcı Remote Login ile bağlıydı; fiziksel
ekran oturumu denenmedi.
- `loginctl lock-session` / `unlock-session` **systemd kullanıcı servisinden çalışıyor** (rc=0, `LockedHint`
  yes → no; GNOME'un `Lock` arayüzüyle kilitlenen oturum da `unlock-session` ile açıldı). polkit eylemi
  `auth_admin_keep` olmasına rağmen oturum sahibi kendi oturumunu açabiliyor; "textual authentication agent"
  hatası yalnız gürültü. Yani agent'ın root'a ihtiyacı yok.
- Mutter `PowerSaveMode` 3/0 kullanıcı servisinden kabul edildi (rc=0); uzak oturumda olduğu için görüntüye etkisi
  ölçülmedi.

## Güvenlik notu
Agent oturum kilidini açabildiği için en hassas bileşen. Anahtarı ele geçiren biri kilidi açabilir;
bu yüzden agent anahtarı istemcide şifreli kasada, Ubuntu'da sadece kullanıcıya okunur dosyada tutulur
ve agent yalnızca VPN/Tailscale arayüzünden dinler.
