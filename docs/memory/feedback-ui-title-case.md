---
name: feedback-ui-title-case
description: Arayüzde buton ve başlıkların tüm kelimelerinin baş harfi büyük olmalı
type: feedback
---

Buton, başlık, menü, pencere başlığı, sekme ve **form/input etiketlerinde** her kelimenin baş harfi büyük
yazılır ("Yeni Cihaz", "Kilidi Aç", "Kullanıcı Adı", "Ana Şifre (Tekrar)"). Yalnızca açıklama metinleri,
ipuçları (hint), placeholder'lar ve hata mesajları normal cümle düzeninde kalır.
Türkçe büyük harf kuralına dikkat: i → İ ("İptal", "İçe Aktar").

**Neden:** Kullanıcı isteği (2026-11-14): önce buton/başlıklar, ardından input etiketleri de istendi.
**Nasıl uygulanır:** Yeni eklenen her buton/başlık/menü öğesinde uygula; Qt'nin standart "Cancel/Save" gibi
butonlarını İngilizce bırakma, Türkçe ve baş harfleri büyük metinle değiştir.
