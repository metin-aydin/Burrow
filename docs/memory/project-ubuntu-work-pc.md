---
name: project-ubuntu-work-pc
description: Kullanıcının iş bilgisayarı metin-pc (Ubuntu 24.10, GNOME 47) ve istediği Ubuntu bağlantı modu
type: project
---

İş bilgisayarı `metin-pc`: **Ubuntu 24.10, GNOME Shell 47, gnome-remote-desktop 47** (2026-11-19, `tools/agent-probe.sh`).
Eski araştırma ve ADR 0003 24.04 / GNOME 46 varsayıyordu; bu bilgisayarda değil.

Kullanıcı 2026-11-19'da: Burrow'dan **ofisteki ekranda açık olan masaüstüne** (Desktop Sharing) bağlanmak istiyor,
ayrı uzak oturuma (Remote Login) değil: "iş yerinde açık bıraktığım ekranlar evde de aynı şekilde devam etsin".
O gün betiği Remote Login oturumundan çalıştırmıştı.

**Neden:** Desktop Sharing kilitli ekrana bağlanamıyor ve bağlantı kopunca ekranı kilitsiz bırakıyor; burrow-agent
(ADR 0003) bunun için. Remote Login bu ihtiyacı karşılamıyor (ayrı oturum, açık pencereler yok).
**Nasıl uygulanır:** Ubuntu tarafı tasarımlarında GNOME 47 + Desktop Sharing'i esas al. Bkz. [[project-goal]].
