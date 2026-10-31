# Nasıl yapıldı

[English](../en/journey.md)

Hedef netti: e-kitap okuyucu metni **kendi işlemcisinde**, karakter karakter, ağırlıkları cihazın içinde duran bir modelden üretecek. Hiçbir cevapta bilgisayar ya da telefon rol almayacak, ekran da kanıtı gösterecek: parametre sayısı, seed ve hız. Yedek planlara (daha küçük bir int8 model ya da modelin telefonda çalıştığı, e-kitap okuyucunun ekran olduğunu açıkça belirten bir düzen) hiç gerek kalmadı.

Bu repoda v0.4 var. Önceki sürümler yalnızca burada anlatılıyor.

## Cihaza dokunmadan önce

- llama2.c tarzı modeller ESP32 kartlarında daha önce çalıştırılmıştı: PSRAM'li ESP32-S3'lerde ([esp32-llm](https://github.com/DaveBben/esp32-llm)) ve ağırlıkları flash'tan okuyan, PSRAM'siz klasik bir ESP32'de ([CircuitDigest](https://circuitdigest.com/articles/running-260K-tiny-llm-on-esp32)). X3'teki ESP32-C3 bunların en zayıfı: tek çekirdek, SIMD yok, FPU yok.
- Kâğıt üstündeki tahmin: ağırlıklar flash'ta kalmalı, KV cache RAM'e sığsın diye bağlam kısa olmalı, ve yazılımsal kayan nokta hesabıyla bu boyutta bir model saniyede yaklaşık 3 token üretir.
- llama2.c'nin 260 bin parametrelik TinyStories modeli gibi hazır bir model İngilizce çocuk hikâyeleri anlatıyor. GPU'cuk ise kendi esprilerini Türkçe yapacaktı; bu yüzden sıfırdan eğitilen, token olarak karakter kullanan kendi minik modeline kavuştu.

## M0: araçlar, ilk bağlantı, yedek

- Fabrika firmware'inde X3'ün USB portu bir saniye kadar görünüp kayboluyor. İşe yarayan yöntem: portu 0,1 saniyede bir yokla, göründüğü anda esptool'u `--after no-reset` ile bağla. Bu, çipi ROM bootloader'ında bırakıyor ve port kalıcı oluyor.
- `esptool flash-id`: ESP32-C3, 16 MB flash. `get-security-info`: Secure Boot ve flash şifreleme kapalı. Kendi firmware'imizi yüklemenin önünde engel yoktu.
- 16 MB'lık tam yedek (72 saniye), `verify-flash` ile cihazla karşılaştırıldı.
- Bölüm tablosu ve NVS yedekten okundu: fabrika firmware'i `0x10000`'daki app0'da, NVS'te de tuş kalibrasyonu dahil fabrika ayarları var. O andan sonra yalnızca uygulama bölümüne yazıldı.

## M1: modelsiz firmware

- PlatformIO ve FreeInk SDK (CrossPoint Reader'ın kullandığı SDK), CrossPoint'le aynı derleme ayarlarıyla.
- Noto Sans'tan üretilen bitmap fontlarla Türkçe metin; altı tuşun hepsi, sallama algılama, uyku ve uyanma.
- Yolda öğrenilenler (bir kısmı CrossPoint'in ve SDK'nın kaynak kodundan, bir kısmı cihazda):
  - Güç tuşuyla uyanmak ESP32'ye soğuk açılış gibi görünüyor.
  - Ekranın cevap vermesi için önce SD kartın besleme hattı açılmalı.
  - X3'te SDK'nın arka planda yenileme fonksiyonu sessizce bekliyor; yalnızca "no shadow" türü gerçekten arka planda çalışıyor.
  - Hızlı yenilemeler biriken izler bırakıyor; birkaç hızlı yenilemede bir, iz temizleyen yarım yenileme gerekiyor.
  - Elle sallama yaklaşık 310–320 °/s ölçüldü; 300 °/s eşiği fazla sıkıydı, 220 °/s yapıldı.
- Ekran, IMU ve USB çalışırken boş heap: 229.604 bayt (224 KB). Modelin bütçesi buydu.

## M2: veri, eğitim ve bilgisayarda inference

- İlk veri seti: 9 soru ve 88 Türkçe cevap, kendini dev bir GPU sanan minik bir cihazın ağzından.
- `tools/train.py`, Mac'in işlemcisinde 141.312 parametrelik bir model eğitti.
- Bu model için yazılan `llm.c` önce Mac'te çalıştı. Temperature 0'da 9 sorunun hepsinde PyTorch'la birebir aynı cevabı verdi.

## M3: cihazda

E-kitap okuyucudaki ilk çalıştırma (v0.2), tahminle uyumlu olarak **saniyede 3,9 token** üretti ve yaklaşık 147 KB heap boş kaldı. 43 karakterlik bir cevap 13,2 saniye sürdü. Planlanan int8 hızlandırmasına gerek kalmadı.

## M4: arayüz ve sürümler

| Sürüm | Değişiklik | Parametre | Normal mod | Overclock |
|---|---|---|---|---|
| v0.2 | Cihazdaki ilk demo, Türkçe | 141.312 | temperature 1,2, top-p 0,9 | – |
| v0.3 | Türkçe ve İngilizce tek modelde, overclock modu | 141.504 | temperature 1,2, top-p 0,9 | temperature 1,6, top-p kapalı |
| v0.4 | Kalıplı veri ve dropout: daha az ezber | 141.696 | temperature 1,0, top-p 0,9 | temperature 1,4, top-p 0,9 |

v0.3 çalışıyordu, ama normal modda örneklenen 720 cevabın hepsi veri setindeki bir cümlenin kelimesi kelimesine aynısıydı. [Ezber deneyi](memorization.md) bunu değiştirmeyi denedi; en iyi sonucu veren ayarlar v0.4 oldu.

## Yapılmayanlar

- int8 ağırlıklar ve tam sayı aritmetiği (ağırlıklar dörtte bir boyutta, büyük ihtimalle çok daha hızlı); demo için gerekmedi.
- Dil seçiminin uykudan sonra hatırlanması; bu, fabrika NVS'ine yazmak demekti.

## Öğrenilenler

- **Önce tahmin et, sonra ölç.** Kâğıt üstündeki tahmin (saniyede yaklaşık 3 token) ölçüme (3,9) yakın çıktı; "ne olur ne olmaz" diye planlanan optimizasyon gereksiz kaldı.
- **Bütçe flash değil, RAM'di.** 16 MB flash ağırlıkları rahatça alıyor; KV cache'in framebuffer'ın, USB stack'inin ve SDK'nın yanına sığması gerekiyordu. Grouped-query attention onu yarıya indirdi.
- **Sürücünün kaynağını oku.** Bu panelde "arka planda" gerçekten arka planda demek değildi; nedeni sürücünün kodundaydı.
- **Minik model ezberler.** Daha az eğitmek ya da daha çok dropout yalnızca işleri kötüleştirdi; daha çeşitli veri ve biraz dropout ise modeli parçaları birleştirmeye itti.
- **Bilgisayar cihazı birebir taklit etsin.** İkisi için tek C dosyası ve tek rastgele sayı üreteci; cihazın logladığı her cevap bilgisayarda yeniden üretilip incelenebiliyor.
- **Geri getiremeyeceğin şeyi silme.** Önce yedek al, yalnızca uygulama bölümünü yaz, checksum'ları kendin karşılaştır: `sha256sum -c`'nin biçimi bozuk bir checksum dosyasında bile başarıyla döndüğü görüldü.
