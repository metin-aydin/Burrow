---
name: feedback-memory-in-repo
description: Tüm hafıza, kurallar ve markdownlar yalnızca proje klasöründe; aşılamaz kural
type: feedback
---

Kullanıcı 2026-11-14'te bunu aşılamaz kural olarak koydu: projeyle ilgili tüm hafıza, kurallar
ve markdown dosyaları proje klasöründe tutulur; tüm okuma/yazma bu dosyalara yapılır. Global
veya araçlara özel hafıza konumları kullanılmaz. Kural metni: [CLAUDE.md](../../CLAUDE.md) → KURAL 0.

**Neden:** Dosyalar GitHub'a gönderilecek; sonraki yazılımcı aynı hafıza ve kurallarla devam
edebilmeli.

**Nasıl uygulanır:** Her anlamlı işten sonra [LOG.md](LOG.md)'ye kayıt düş, kararları
`docs/decisions/` altına ADR olarak yaz, [MEMORY.md](MEMORY.md) dizinini güncelle.
