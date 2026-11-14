---
name: project-stack-status
description: Teknoloji yığını kararı ve platform/lisans tercihleri
type: project
---

2026-11-14'te kullanıcıyla birlikte **karar verildi** (ADR: `docs/decisions/0001-tech-stack.md`):

- Arayüz + RDP: **Qt 6 (C++) + FreeRDP 3**. Motor soyutlama arkasında; IronRDP ileride eklenebilir.
- Ağ: **Go netcore** (tsnet + libopenconnect + gVisor netstack). Tailscale'i gömmenin pratik tek
  yolu Go olduğu için.
- **Açık kaynak.** LGPL bağımlılıklar (Qt, libopenconnect) dinamik bağlanır. Proje lisansı **MIT**
  (kullanıcı Apache-2.0 önerisi yerine MIT seçti).
- **Önce macOS**, sonra Ubuntu, sonra Windows.

Elenenler: Go + Wails (webview'da çoklu 4K çizim yavaş), Rust + IronRDP (AVC444 yok, masaüstü
multimon kanıtlanmamış), .NET/Flutter (gömülü örnek yok).

**Nasıl uygulanır:** Yığın değişikliği önerilecekse yeni ADR yazılır ve kullanıcıyla tartışılır.
Bkz. [[project-goal]].
