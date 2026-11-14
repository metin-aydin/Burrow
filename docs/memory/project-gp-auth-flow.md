---
name: project-gp-auth-flow
description: Kullanıcının kurumundaki GlobalProtect giriş akışı ve OTP davranışı
type: project
---

Kurumda GlobalProtect kullanılıyor. Giriş akışı:
1. Şifrenin sonuna OTP eklenerek girilir (şifre+OTP).
2. Ardından tekrar şifre istenir; yeni OTP beklenip yine şifre+yeni OTP girilir.

Bu muhtemelen GP'nin portal girişi + gateway girişi (iki ayrı kimlik doğrulama) davranışı.
Henüz test edilmedi.

**Neden önemli:** Uygulama bu akışı kullanıcıdan girdi almadan tamamlayabilmeli. Kullanıcı
authenticator QR kodunu (TOTP secret) uygulamaya verirse OTP'ler otomatik üretilip şifreye
eklenecek; ikinci adımda bir sonraki 30 sn'lik OTP penceresi beklenecek.

**Nasıl uygulanır:** openconnect `--protocol=gp` ile ön test yapılacak: (a) HIP kontrolü var mı,
(b) doğrudan gateway'e bağlanınca tek OTP yetiyor mu. Bkz. [[project-goal]].
