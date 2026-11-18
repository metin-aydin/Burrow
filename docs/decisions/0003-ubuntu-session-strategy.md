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

## burrow-agent tasarımı (2026-11-19, kullanıcıyla kararlaştırıldı)
Kullanıcı ofisteki masaüstüne (Desktop Sharing) bağlanmak istiyor ve şu ödünü **kabul etti**: bağlıyken ofisteki
oturum kilitsizdir (GNOME fiziksel ekranla uzak görüntüyü ayıramaz); biri ofiste fareyi oynatırsa monitör anlık
açılır, masaüstü birkaç saniye görünür ve kullanılabilir. Kullanıcının şartı: **cihaz başına bir ayar**, yalnız
Ubuntu cihazı tanımlarken görünür, kapatılabilir; kapalıyken Burrow agent'a hiç bağlanmaz.

- **Program:** `agent/` (ayrı Go modülü, yalnız standart kütüphane; `loginctl` ve `gdbus` çağırır). systemd
  **kullanıcı** servisi; root gerekmez (2026-11-19 denemesi: oturum sahibi kendi oturumunu kilitleyip açabiliyor).
- **Kurulum:** `burrow-agent install` → 32 baytlık rastgele anahtar (`~/.config/burrow-agent/key`, 0600), servis
  dosyası, servisi başlatır ve **eşleştirme kodunu** bir kez gösterir. Kod Burrow'da cihaz ayarına girilir, kasada
  şifreli saklanır. `burrow-agent pair` yeni anahtar üretir (eskisi geçersiz olur), `uninstall` kaldırır.
- **Ağ:** TCP 47391 (değiştirilebilir), tüm arayüzler: ofis bilgisayarı VPN/Tailscale ile LAN adresinden
  erişiliyor, ADR'nin "yalnız tünel arayüzü" fikri uygulanamaz; güvenlik kimlik doğrulamadan gelir.
- **Kimlik doğrulama (ADR'deki anahtar çifti yerine):** ortak anahtarla HMAC-SHA256, iki yönlü ve bağlantıya
  bağlı: agent her bağlantıda tek kullanımlık rastgele bir değer (challenge) gönderir, Burrow komutu onunla birlikte
  imzalar, agent'ın yanıtı da aynı değerle imzalıdır (Burrow agent'ı doğrular). Kaydedilmiş bir komut sonraki hiçbir
  bağlantıda, agent yeniden başlasa bile, işe yaramaz (ilk tasarımdaki zaman damgası + nonce belleği, yeniden
  başlatmada tekrar penceresi bıraktığı için güvenlik incelemesi üzerine değiştirildi). Anahtar çiftinin buradaki
  tek artısı (Ubuntu'da yalnız açık anahtar durması) anlamsız: anahtar dosyasını okuyabilen zaten o oturumun
  sahibi. Bir adresten dakikada 5 hatalı imza → o adres 5 dk yok sayılır; hatalı imzaya yanıt verilmez.
- **Güvenli tarafta kalma:** kilit başarısız olursa her 2 sn yeniden denenir ve o arada monitör kapalı tutulur;
  servis durdurulurken açık oturum kilitlenir; oturum sürerken çöken agent yeniden açılınca (durum dosyası
  `~/.config/burrow-agent/session`) ekranı kilitler.
- **Komutlar:** `start` (kilidi aç, monitörü kapat, 20 sn'lik kira başlat), `beat` (kirayı uzat; Burrow 5 sn'de bir
  gönderir), `end` (kilitle, monitörü aç). Kira dolarsa (bağlantı koptu, Mac uyudu) agent kilitler ve monitörü açar.
  Bağlıyken agent 2 sn'de bir monitörün kapalı olduğunu denetler, uyandıysa yeniden kapatır.
- **Burrow:** `start` RDP'den önce tünel üzerinden gönderilir (aynı `NetcoreConnectFD` yolu); başarısız olursa
  bağlanma yine denenir (ekran zaten açık olabilir) ve bağlantı adımlarında uyarı görünür.

## Güvenlik notu
Agent oturum kilidini açabildiği için en hassas bileşen. Anahtarı ele geçiren biri kilidi açabilir;
bu yüzden agent anahtarı istemcide şifreli kasada, Ubuntu'da sadece kullanıcıya okunur dosyada tutulur
ve agent yalnızca VPN/Tailscale arayüzünden dinler.
