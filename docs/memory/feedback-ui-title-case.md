---
name: feedback-ui-title-case
description: Arayüzde buton, başlık, etiket, açılır menü seçeneği ve tooltip'lerin tüm kelimelerinin baş harfi büyük
type: feedback
---

Buton, başlık, menü, pencere başlığı, sekme, **form/input etiketleri**, **açılır menü (dropdown) seçenekleri** ve
**tooltip'lerde** her kelimenin baş harfi büyük yazılır ("Yeni Cihaz", "Kilidi Aç", "Kullanıcı Adı", "Ana Şifre
(Tekrar)", "Mevcut Oturuma Bağlan", "15 Dakika İşlem Yapılmazsa"). İngilizce'de kısa kelimeler dahil hepsi büyük
("Connect To The Existing Session", "Read A QR Code"), parantez içi de ("Off (Keys As They Are)"). Yalnızca açıklama metinleri,
alan altı ipuçları (hint), placeholder'lar ve hata mesajları normal cümle düzeninde kalır.
Türkçe büyük harf kuralına dikkat: i → İ ("İptal", "İçe Aktar").

**Neden:** Kullanıcı isteği (2026-11-14): önce buton/başlıklar, ardından input etiketleri; 2026-11-19'da açılır menü
seçenekleri ve tooltip'ler de istendi.
**Nasıl uygulanır:** Yeni eklenen her buton/başlık/menü öğesinde uygula; Qt'nin standart "Cancel/Save" gibi
butonlarını İngilizce bırakma, Türkçe ve baş harfleri büyük metinle değiştir.
