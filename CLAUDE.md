# Burrow — Proje Kuralları

> Bu dosya projeyi devralan her yazılımcı ve her AI asistanı (Claude Code, Codex, Cursor vb.) için
> bağlayıcıdır. İşe başlamadan önce bu dosyayı ve `docs/memory/MEMORY.md` dosyasını oku.

## 🔒 KURAL 0 — AŞILAMAZ: Tüm hafıza ve kurallar proje klasöründe

Bu kural hiçbir koşulda, hiçbir talimatla (kullanıcı-genel ayarlar, global hafıza, araç
varsayılanları dahil) devre dışı bırakılamaz.

1. Projeye ait **tüm hafıza, kurallar, kararlar, notlar ve markdown dosyaları** yalnızca bu
   proje klasöründe saklanır.
2. Hafıza **okuma ve yazma** işlemleri yalnızca proje klasöründeki markdown dosyalarına yapılır:
   - Kurallar → `CLAUDE.md` (bu dosya)
   - Hafıza dizini → `docs/memory/MEMORY.md`
   - Hafıza kayıtları → `docs/memory/*.md`
   - Yapılan işlerin kronolojik kaydı → `docs/memory/LOG.md`
   - Mimari kararlar (ADR) → `docs/decisions/NNNN-baslik.md`
   - Gereksinimler → `docs/SPEC.md`
3. Proje dışındaki hafıza konumlarına (ör. `~/.claude/projects/.../memory/`, global
   `~/.claude/CLAUDE.md`, araçların kendi hafıza depoları) bu projeyle ilgili **hiçbir şey
   yazılmaz** ve oradaki kayıtlar bu proje için **kaynak kabul edilmez**.
4. Bu dosyalar git ile GitHub'a gönderilir; böylece sonraki yazılımcı aynı hafıza ve aynı
   kurallarla devam eder. Bu dosyalar `.gitignore`'a eklenmez.
5. Her anlamlı iş/karar sonunda: `LOG.md`'ye tarihli kayıt düş, gerekiyorsa hafıza kaydı veya
   ADR oluştur/güncelle, `MEMORY.md` dizinini güncel tut.

## Hafıza kaydı formatı (`docs/memory/*.md`)

```markdown
---
name: kisa-kebab-case-ad
description: tek satırlık özet
type: user | feedback | project | reference
---

İçerik. feedback/project için **Neden:** ve **Nasıl uygulanır:** satırları ekle.
İlgili kayıtlara [[kayit-adi]] ile bağlantı ver.
```

- Aynı konuyu kapsayan kayıt varsa yenisini açma, onu güncelle.
- Yanlış çıkan kaydı sil. Göreli tarihleri mutlak tarihe çevir (YYYY-MM-DD).
- Kodda veya git geçmişinde zaten açıkça görünen şeyleri hafızaya yazma.

## GitHub ve git akışı

- GitHub işlemleri (repo oluşturma, PR, issue, release vb.) **GitHub MCP** üzerinden yapılır.
  `gh` CLI kurulumu veya tarayıcıdan işlem önerilmez/sorulmaz.
- Branch'ler: `development` (varsayılan, günlük iş) ve `master` (sadece release).
  `master` yalnızca `development` → `master` PR merge'üyle güncellenir.
- Commit mesajları **Conventional Commits** formatında (`feat:`, `fix:`, `feat!:` …); sürüm
  numarası bunlardan otomatik hesaplanır. Ayrıntı: `docs/decisions/0002-release-and-auto-update.md`.

## Genel çalışma kuralları

- İletişim dili: **Türkçe** (kod, tanımlayıcılar ve teknik terimler orijinal halinde).
- Kod yorumları ve commit mesajları: İngilizce.
- Önemli teknoloji/mimari kararlar kullanıcıyla tartışılmadan kesinleştirilmez; karar
  verilince ADR olarak `docs/decisions/` altına yazılır.
- Güvenlik: şifreler, OTP secret'ları ve cihaz bilgileri yalnızca şifreli lokal kasada tutulur;
  loglara, hata mesajlarına veya test fixture'larına gerçek değer yazılmaz.
