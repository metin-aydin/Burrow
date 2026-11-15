# ADR 0004 — Şifreli kasa (vault) ve Touch ID

- **Durum:** KABUL EDİLDİ (2026-11-14)
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
  - Touch ID, DEK'in bir kopyasını Keychain'de saklar.
  - **Şifre değiştirmede DEK de yenilenir** (2026-11-15, güvenlik incelemesi): belge ve tüm blob'lar yeni
    DEK ile yeniden şifrelenir, eski şifreyle sarılı `.bak` silinir, Touch ID kopyası yenilenir. Böylece
    sızmış eski şifre, eski yedek dosya veya eski Touch ID kopyası güncel kasayı açamaz.
- **Argon2id parametreleri** (dosyada saklanır, ileride artırılabilir): t=3, m=64 MiB, p=4, 16 bayt salt.
- **Şifreleme:** XChaCha20-Poly1305 (24 bayt rastgele nonce). AAD, format sürümünü ve KDF
  parametrelerini bağlar (parametre değiştirme saldırısına karşı).
- **Dosya:** JSON zarf (`v`, `kdf`, `key`, `data`); `0600` izin; geçici dosya + fsync + rename ile
  atomik yazma; bir önceki sürüm `.bak` olarak tutulur (şifre değişiminde silinir).
- **İçerik:** kasa, şifreli içeriği opak bir JSON belgesi olarak saklar; şema (cihazlar, tünel
  profilleri, ayarlar) uygulama tarafında (Qt) tanımlanır ve `schema` alanıyla sürümlenir.
- **Hata ayrımı:** yanlış şifre / bozuk dosya / G/Ç hatası ayrı kodlarla döner; yanlış şifrede
  kullanıcıya anlaşılır mesaj, otomatik tekrar yok.

## Touch ID (opsiyonel, ayarlardan)

- Açılınca DEK, macOS Keychain'de (Data Protection keychain) `kSecAccessControlBiometryCurrentSet`
  erişim kontrolüyle saklanır: okumak için Touch ID gerekir, parmak izi seti değişirse kayıt geçersiz olur.
- Ana şifre her zaman geçerlidir; Touch ID başarısızsa ana şifre sorulur. Şifre değişince Touch ID kopyası
  yeni DEK ile güncellenir; güncellenemezse Touch ID kapatılır.
- Data Protection keychain imzalı build ve `keychain-access-groups` entitlement'ı gerektirir; imzasız
  geliştirme build'inde Touch ID seçeneği devre dışı görünür.
- Windows (Windows Hello) / Ubuntu karşılıkları sonraki fazda.

## Sonuçlar

- Ek native kütüphane yok; kasa Go testleriyle doğrulanır.
- Kasa açıkken DEK ve çözülmüş içerik süreç belleğinde durur (kilitlemede sıfırlanır; Go GC nedeniyle
  bellekten silinme garantisi sınırlı — kabul edilen risk).
- SPEC §2 ve §8'deki "SQLCipher" ifadesi bu ADR ile değişti.
