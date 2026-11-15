---
name: feedback-github-via-mcp
description: GitHub işlemleri GitHub MCP veya gh CLI ile; tarayıcıya yönlendirme yok
type: feedback
---

Kullanıcı 2026-11-14'te: GitHub işlemleri için GitHub MCP kullanılacak, tekrar sorulmayacak.
2026-11-15'te: "github'da yapılacak her şeyi cli ya da mcp ile yap" — `gh` artık kurulu ve oturum açık
(`metin-aydin`, kapsamlar `repo`, `workflow`).

**Neden:** gh kurulumu ve tarayıcı alternatifleri tekrar tekrar soruldu; kullanıcı bundan rahatsız oldu.

**Nasıl uygulanır:** GitHub işini doğrudan MCP ile, MCP'de araç yoksa (ortam, secret, dal koruması vb.)
`gh` / `gh api` ile yap; kullanıcıyı tarayıcıya gönderme. Tek istisna parola/anahtar içeren secret'lar:
Claude bunları girmez, kullanıcı `.github/scripts/set-release-secrets.sh` betiğini kendi terminalinde çalıştırır.
Bkz. [[feedback-memory-in-repo]], [[project-release-flow]].
