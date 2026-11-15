---
name: project-goal
description: Burrow'un amacı ve kullanıcının motivasyonu
type: project
---

Kullanıcı şu an iş bilgisayarına sistem genelinde GlobalProtect VPN açıp RDP ile bağlanıyor;
MacBook'tan bağlanınca RDP sık sorun çıkarıyor (bazen siyah ekran).

Kullanıcının iş yerindeki bilgisayarı **Ubuntu 24.04** (GNOME 46). Uzaktan bağlanınca yerel
oturumu kapanıyor (veya tersi) — bkz. SPEC §5.2. **Yükseltme mümkün değil; çözüm tüm Ubuntu
sürümleriyle (22.04+) uyumlu olmalı.**

Hedef: Mac, Windows ve Ubuntu'da çalışan, GlobalProtect veya Tailscale tünelini **sadece
uygulamanın içinde** açan ve kendi gömülü RDP istemcisiyle kayıtlı cihazlara bağlanan uygulama.
Ayrıntılar: [docs/SPEC.md](../SPEC.md).

**Neden:** Sistem genelinde VPN istemiyor; tüm bağlantı akışını (VPN girişi + RDP) tek
uygulamada, kendi istemcisiyle yönetmek istiyor.

**Nasıl uygulanır:** Harici RDP istemcisi açan veya sistem VPN/TUN arayüzü kuran çözümler
kabul edilmez. Bkz. [[project-gp-auth-flow]], [[project-stack-status]].
