# ADR 0004 — Şifreli kasa (vault)

- **Durum:** KABUL EDİLDİ (2026-11-14); Touch ID bölümü 2026-11-15'te kaldırıldı (aşağıya bakın)
- **Karar verenler:** kullanıcı + AI asistan

## Bağlam

SPEC §2 ve §4: cihazlar, RDP şifreleri, GP şifresi ve TOTP secret'ları lokalde, ana şifreyle korunan
bir kasada saklanmalı. Ana şifre unutulursa veri kurtarılamaz. Kayıt sayısı küçük (onlarca cihaz).

## Seçenekler

1. **Go netcore içinde tek şifreli dosya** (Argon2id + XChaCha20-Poly1305) — seçildi.
2. SQLCipher — ek native bağımlılık, her platformda derleme/paketleme; bu veri boyutunda gereksiz.
3. Sadece işletim sistemi kasası (Keychain/Credential Manager/libsecret) — "uygulamaya şifre koyma"
   isteğini karşılamaz, Ubuntu'da tutarsız.

## Karar

- Kasa **Go netcore**'da (`internal/vault`), saf Go (`golang.org/x/crypto`); üç platformda aynı kod.
- **Anahtar hiyerarşisi:**
  - Rastgele 32 baytlık veri anahtarı (DEK) içeriği şifreler.
  - Ana şifreden Argon2id ile türetilen anahtar (KEK) DEK'i sarar (wrap).
  - **Şifre değiştirmede DEK de yenilenir** (2026-11-15, güvenlik incelemesi): belge ve tüm blob'lar yeni
    DEK ile yeniden şifrelenir, eski şifreyle sarılı `.bak` silinir. Böylece
    sızmış eski şifre veya eski yedek dosya güncel kasayı açamaz.
- **Argon2id parametreleri** (dosyada saklanır, ileride artırılabilir): t=3, m=64 MiB, p=4, 16 bayt salt.
- **Şifreleme:** XChaCha20-Poly1305 (24 bayt rastgele nonce). AAD, format sürümünü ve KDF
  parametrelerini bağlar (parametre değiştirme saldırısına karşı).
- **Dosya:** JSON zarf (`v`, `kdf`, `key`, `data`); `0600` izin; geçici dosya + fsync + rename ile
  atomik yazma; bir önceki sürüm `.bak` olarak tutulur (şifre değişiminde silinir).
- **İçerik:** kasa, şifreli içeriği opak bir JSON belgesi olarak saklar; şema (cihazlar, tünel
  profilleri, ayarlar) uygulama tarafında (Qt) tanımlanır ve `schema` alanıyla sürümlenir.
- **Hata ayrımı:** yanlış şifre / bozuk dosya / G/Ç hatası ayrı kodlarla döner; yanlış şifrede
  kullanıcıya anlaşılır mesaj, otomatik tekrar yok.

## Touch ID — kaldırıldı (2026-11-15)

İlk tasarımda DEK'in bir kopyası Data Protection keychain'de `kSecAccessControlBiometryCurrentSet` ile
saklanıyordu. Bu keychain `keychain-access-groups` entitlement'ı, Developer ID dağıtımında da provisioning
profile ister; profil olmadığı için release'te Touch ID hiç çalışmadı. Seçenekler (profil eklemek, login
keychain + LAContext ile daha zayıf bir yol, hibrit) kullanıcıya soruldu; kullanıcı **Touch ID'yi kaldırmayı**
seçti.

- Qt tarafı (`platform/Biometric*`, kilit ekranı butonu, ayar anahtarı), C API'deki
  `NetcoreVaultOpenWithKey` / `NetcoreVaultExportKey` ve Go'daki `OpenWithKey` / `ExportKey` silindi:
  DEK artık süreç dışına hiçbir yoldan aktarılamaz, kasa yalnız ana şifreyle açılır.
- Kasa belgesindeki eski `settings.touchId` alanı okunurken yok sayılır, yeniden yazılmaz.
- İleride biyometrik açma geri gelirse provisioning profile'lı yol (Secure Enclave ACL) tercih edilir.

## Sonuçlar

- Ek native kütüphane yok; kasa Go testleriyle doğrulanır.
- Kasa açıkken DEK ve çözülmüş içerik süreç belleğinde durur (kilitlemede sıfırlanır; Go GC nedeniyle
  bellekten silinme garantisi sınırlı — kabul edilen risk).
- SPEC §2 ve §8'deki "SQLCipher" ifadesi bu ADR ile değişti.
