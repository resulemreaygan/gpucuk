# Donanım

[English](../en/hardware.md)

GPU'cuk, cebe sığan bir e-kitap okuyucu olan **XTEINK X3** üzerinde çalışıyor. Burada kullanılan cihaz Developer Edition: kutudan çıktığı gibi USB üzerinden firmware yüklemeye izin veriyor.

## İçinde ne var?

| Parça | Ayrıntı |
|---|---|
| Çip | ESP32-C3 (QFN32, bu cihazda revizyon v0.4) |
| İşlemci | Tek 32 bit RISC-V çekirdek (RV32IMC), 160 MHz. **Kayan nokta birimi (FPU) yok**: her `float` işlemi yazılımla yapılıyor. |
| RAM | 400 KB SRAM, PSRAM yok |
| Flash | 16 MB SPI NOR |
| Ekran | 792×528 e-ink. Bu cihazda denetleyici UC8253; başka üretim partilerinde UC8279 var, FreeInk SDK hangisi olduğunu açılışta buluyor. |
| Tuşlar | Geri, Onay, Sol, Sağ, Yukarı, Aşağı ve üst kenarda güç tuşu |
| Hareket sensörü | QMI8658 (GPU'cuk jiroskobunu sallamayı algılamak için kullanıyor) |
| USB | 4 pinli manyetik pogo kablo, çipin yerleşik USB Serial/JTAG'ı |

Wi-Fi, Bluetooth, microSD yuvası, gerçek zamanlı saat ve pil göstergesi kullanılmıyor.

Asıl kısıt işlemci: GPU olmaması zaten belli, ama FPU da yok. Bir dil modeli çoğunlukla `float` çarpma-toplamadan ibaret; burada her biri bir kütüphane çağrısı.

## Kasayı açmadan firmware yükleme

`esptool`, USB üzerinden ESP32-C3'ün ROM bootloader'ıyla iletişim kuruyor. Test cihazında:

- `esptool flash-id` ESP32-C3'ü ve 16 MB flash'ı gördü; yani USB'den yükleme kilitli değildi.
- `esptool get-security-info` **Secure Boot: kapalı** ve **Flash Encryption: kapalı** gösterdi.

Üçüncü taraf mağazalardan satılan bazı X3'lerin USB'den yüklemeye kilitli geldiği söyleniyor. GPU'cuk yalnızca burada anlatılan Developer Edition cihazda denendi.

## Flash düzeni

Bölüm tablosu, fabrika flash'ının tam yedeğinden okundu. GPU'cuk onu olduğu gibi bırakıyor (`firmware/partitions.csv` aynı tabloyu üretiyor) ve yalnızca ilk uygulama bölümüne yazıyor.

| Ad | Adres | Boyut | İçerik |
|---|---|---|---|
| (bootloader) | `0x0` | | ESP-IDF bootloader |
| (bölüm tablosu) | `0x8000` | | |
| nvs | `0x9000` | 20 KB | Fabrika ayarları ve kalibrasyon |
| otadata | `0xE000` | 8 KB | Hangi uygulama yuvasının açılacağı (bu cihazda app0) |
| **app0** | **`0x10000`** | 7,44 MB | Fabrika firmware'i; **GPU'cuk buraya yazılıyor** |
| app1 | `0x780000` | 7,44 MB | İkinci (OTA) yuva, bu cihazda boş |
| spiffs | `0xEF0000` | 1 MB | |
| coredump | `0xFF0000` | 64 KB | |

Cihazlar farklı olabilir. Bu cihazda fabrika firmware'i app0'daydı ve otadata app0'ı seçiyordu; bu yüzden app0'a yazmak yetti. Sizin cihazınızda otadata app1'i seçiyorsa (internette paylaşılan bir yükleme rehberi fabrika uygulamasını `0x780000`'de gösteriyor), yalnızca app0'a yazmak fabrika firmware'inin açılmaya devam etmesine yol açar; GPU'cuk'un betikleri bu durumu desteklemiyor. Kendi bölüm tablosuyla başka bir firmware çalıştıran cihazlar için de aynısı geçerli.

**Flash'ın tamamını asla silmeyin** (`esptool erase-flash`): NVS de silinir ve onu yalnızca kendi cihazınızın tam yedeği geri getirebilir.

## Güç ve uyku

Gerçek bir "kapalı" durumu yok. Güç tuşu kartın beslemesini açıyor, firmware de açılışta bu beslemeyi açık tutmak zorunda (FreeInk SDK'da `BoardConfig::holdPowerRails()`). "Kapalı" aslında derin uyku. İki sonucu var:

- ESP32, güç tuşuyla uyanmayı uykudan dönüş olarak değil, sıfırdan açılış olarak görüyor (`ESP_RST_POWERON`). Bu yüzden GPU'cuk her seferinde baştan başlıyor: Türkçe, ilk soru, seed 42.
- Hiç uyumayan bir firmware pili bitirir. GPU'cuk, güç tuşu 2 saniye basılı tutulunca ya da 5 dakika hiçbir tuşa basılmayınca uyuyor.

E-ink ekran görüntüsünü güç olmadan da koruyor, yani uyuyan cihaz açık gibi görünür. GPU'cuk uyumadan önce bir "uyuyor" ekranı çiziyor ki fark edilsin.

## USB'nin huyları

- USB portu yalnızca cihaz uyanıkken var. Fabrika firmware'iyle kablo takılınca yaklaşık bir saniye görünüp kayboluyor. `tools/` içindeki betikler portu bekliyor ve göründüğü anda bağlanıyor; önce cihazı güç tuşuyla uyandırın, sonra kabloyu takın.
- GPU'cuk USB seri portunu açık tutuyor; cihaz uyanıkken port kaybolmuyor ve yeniden yüklemek için özel bir şey yapmak gerekmiyor.
- Seri portu açmak çipi yeniden başlatıyor (USB Serial/JTAG reset), bu yüzden seri log hep açılıştan başlıyor.
- Kablonun veri kablosu olması gerekiyor. Kutudan çıkan kablo öyle.

## Ekran

- Framebuffer: 792×528, piksel başına 1 bit, en anlamlı bit (MSB) önce, 1 = beyaz. GPU'cuk dikey (528×792) çiziyor ve her noktayı `phyX = y`, `phyY = 527 − x` ile dönüştürüyor.
- Tek bir 52 KB'lık framebuffer (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1`) modele daha çok RAM bırakıyor.
- SDK'daki yenileme modları: `FULL_REFRESH`, `HALF_REFRESH` (iz temizleyen dalga formu) ve `FAST_REFRESH` (hızlı ama biriken hafif izler bırakıyor). Bu panelde tam yenileme yaklaşık 2 saniye sürüyor.
- SD kart ekranla aynı SPI hattını kullanıyor. Önceki firmware SD'nin besleme hattını kapalı bıraktıysa ekran komutları yerine ulaşmıyor; açılışta önce o hat açılıyor (`BoardConfig::releaseSdRail()`).

Metnin bu panelde, model yazdıkça nasıl gösterildiği [firmware.md](firmware.md) içinde.
