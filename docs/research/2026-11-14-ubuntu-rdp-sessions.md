# Araştırma: Ubuntu'da RDP oturum davranışı (2026-11-14)

Kullanıcının iş bilgisayarı: **Ubuntu 24.04 (GNOME 46)**.

## Kök neden
GNOME bir kullanıcı için aynı anda **tek grafik oturuma** izin verir. Fiziksel ekrandaki oturum ile
uzak (headless) oturum birbirini engeller. Remote Login, "oturumlar uyumsuz" deyip mevcut oturumu
**zorla kapatmayı** önerir → kullanıcının yaşadığı sorun bu.
https://www.suse.com/c/headless-remote-sessions-in-gnome-part-3 ,
https://ubuntu.com/desktop/docs/en/latest/how-to/share-your-desktop-remotely/

## GNOME RDP modları
| | Desktop Sharing (mevcut oturum) | Remote Login (yeni oturum) |
|---|---|---|
| Ne yapar | Fiziksel ekrandaki oturumu aynalar | GDM üzerinden ayrı headless oturum açar |
| Port (ikisi açıksa) | 3390 | 3389 |
| Ekran kilitliyken | **Bağlanamaz** ("Session creation inhibited"), GNOME 49'a kadar düzelmedi | Sorun yok |
| Fiziksel monitör | Yapılan her şey ofisteki ekranda görünür | Görünmez |
| Bağlantı kesilince | Oturum **kilitsiz** kalır (`lock-screen-on-disconnect` çalışmıyor) | GNOME 46: **oturum kapanır**; GNOME 47+: oturum kalıcı |

Kaynaklar: https://gitlab.gnome.org/GNOME/gnome-remote-desktop/-/raw/main/NEWS ,
https://gitlab.gnome.org/GNOME/gnome-remote-desktop/-/issues/119 ,
https://gitlab.gnome.org/GNOME/gnome-remote-desktop/-/issues/153 ,
https://gitlab.gnome.org/GNOME/gnome-remote-desktop/-/issues/16

## Sürümler
- 22.04 (GNOME 42): sadece Desktop Sharing.
- **24.04 (GNOME 46): Remote Login var ama kalıcı değil** (bağlantı kesilince oturum kapanır).
  Bir Almanca kılavuz güncellemeyle 24.04'te de kalıcı olduğunu iddia ediyor — **doğrulanmadı**.
- GNOME 47: kalıcı remote login oturumları.
- **26.04 LTS: GNOME 50**, sadece Wayland (Xorg oturumu yok → xrdp GNOME başlatamıyor). Kalıcı
  Remote Login + daha güvenilir session handover. Tek bir forum raporunda menünün görünmediği
  söyleniyor. https://discourse.ubuntu.com/t/26-04-resolute-need-help-to-fix-window-manager-exit-code-2-with-xrdp/70116 ,
  https://linux.how2shout.com/set-up-remote-desktop-ubuntu-26-04/

## xrdp
- Kopan oturuma tekrar bağlanır (sesman). Aynı kullanıcı yerelde açıkken siyah ekran / kopma.
  Konsol oturumuna (Wayland) bağlanamaz. 26.04'te GNOME ile çalışmaz.
  https://c-nergy.be/blog/?p=16698

## İstemcinin yapabilecekleri
- Davranışı **sunucu belirler**; RDP alanlarıyla oturum seçilemez.
- Remote Login, RDP **Server Redirection** (routing token + tek kullanımlık kimlik + RDSTLS) kullanır;
  FreeRDP destekliyor, istemcimizde tam desteklenmeli.
- Bağlantıyı kesmeden önce Super+L göndermek ekranı kilitlemeli (test edilmedi).
- "Allow Locked Remote Desktop" GNOME eklentisi kilitliyken Desktop Sharing'e izin veriyor (GNOME 47'de
  çöktüğü bildirilmiş; fiziksel ekran yine masaüstünü gösterir).
  https://extensions.gnome.org/extension/4338/allow-locked-remote-desktop

## Windows notu
- RDP bağlanınca fiziksel konsol kilitlenir; bağlantı kesilince oturum kilitli kalır.
