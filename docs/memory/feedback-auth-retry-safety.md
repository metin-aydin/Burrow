---
name: feedback-auth-retry-safety
description: Kimlik doğrulama denemelerinde asla otomatik tekrar yapma; hesap kilitlenmesi yaşandı
type: feedback
---

2026-11-14'te GP test betiği (`tools/gp-userspace-test.py`) yanlış TOTP algoritmasıyla (SHA1; kurum
SHA512 kullanıyor) üretilen kodları 6 kez üst üste gönderdi ve kullanıcının VPN hesabı kilitlendi
(kullanıcının kendi betiği de bağlanamadı).

**Neden:** Betik `auth-failed` sonrası durmak yerine her "Enter login credentials" isteminde yeniden
denedi; OTP parametreleri (algoritma/hane/periyot) çalışan mevcut çözümden doğrulanmadan varsayıldı.

**Nasıl uygulanır:**
- Gerçek bir kimlik doğrulama sistemine giden her kod/araç **ilk reddedilmede durur**; otomatik tekrar yok.
- Uygulamada (Burrow) GP girişi: `auth-failed` sonrası kullanıcıya sor, sessizce tekrar deneme;
  arka arkaya en fazla 1 deneme.
- Test etmeden önce çalışan mevcut çözümün parametrelerini (TOTP algoritması, authgroup, DNS) birebir al.
- Kimlik doğrulamayı etkileyen parametrelere (TOTP algoritması vb.) **varsayılan değer koyma**; zorunlu yap.
  2026-11-14'te SHA1 varsayılanı ikinci kez `auth-failed`'a yol açtı (kullanıcı eski komutu tekrar çalıştırdı).
- Ağa çıkmadan önce üretilen OTP'yi authenticator ile karşılaştırma adımı sun (`--show-otp`).
Bkz. [[project-gp-auth-flow]].
