---
name: feedback-no-ai-slop-design
description: Arayüzde AI slop tasarım yasak; Impeccable skill'i proje içinde kurulu, hook bilerek kurulmadı
type: feedback
---

Kullanıcı 2026-11-15'te Impeccable (pbakaus/impeccable v4.9.1, Apache 2.0) kurulmasını ve AI slop tasarımın
engellenmesini istedi. Skill + 4 alt ajan proje içine kopyalandı (`.claude/skills/impeccable`, `.claude/agents/impeccable-*`,
Codex için `.agents/skills/impeccable`). Telemetri `.impeccable/config.json` ile kapalı. Kurallar `CLAUDE.md` →
"Arayüz tasarımı — AI slop yasak" bölümünde.

**Neden:** AI'ın varsayılan kalıpları (kart ızgarası, gradyan metin, eyebrow etiket, emoji ikon) uygulamayı sıradan
gösteriyor; kullanıcı özgün, işçilikli bir arayüz istiyor.
**Nasıl uygulanır:** Her QML işinde Impeccable'ı kullan, düzenlemeden önce `reference/craft-floor.md` oku, yalnızca
`Theme.qml` token'larını kullan. Design hook kurulmadı: dedektör QML'i taramıyor ve her düzenlemede ikili indiriyor.
Güncellemek için repo yeniden klonlanıp aynı klasörlere kopyalanır (global kurulum yapılmaz, bkz. [[feedback-memory-in-repo]]).
Bkz. [[project-brand]], [[feedback-ui-title-case]].
