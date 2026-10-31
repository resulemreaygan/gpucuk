# Derleme ve yükleme

[English](../en/flashing.md)

> **Sorumluluk sizde.** GPU'cuk, siz fabrika firmware'ini geri yazana kadar e-kitap okuyucunuzun firmware'inin yerini alıyor. Tek bir XTEINK X3 Developer Edition üzerinde, [hardware.md](hardware.md) içinde anlatılan fabrika bölüm tablosuyla denendi. **Önce cihazınızın yedeğini alın (2. adım); geri dönmenin tek yolu o yedek.**

## Gerekenler

- USB'den firmware kabul eden bir XTEINK X3 (Developer Edition) ve kutudan çıkan manyetik kablo.
- macOS (denendi). Betikler Linux seri portlarına da (`/dev/ttyACM*`) bakıyor ama Linux'ta çalıştırılmadı.
- git, Python 3.12 ve bir C derleyicisi.

```sh
git clone --recursive https://github.com/resulemreaygan/gpucuk.git
cd gpucuk
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

`--recursive`, FreeInk SDK'yı da (`firmware/freeink-sdk`) indiriyor. Onsuz klonladıysanız `git submodule update --init` çalıştırın.

## 1. Bağlantı

Cihazı güç tuşuyla uyandırın, **sonra** kabloyu takın. Fabrika firmware'inde USB portu yalnızca bir saniye kadar görünüyor; aşağıdaki yedek, yükleme ve geri yükleme betikleri portu 3 dakikaya kadar bekliyor ve göründüğü anda bağlanıyor.

## 2. Flash'ın tam yedeği

```sh
tools/backup_stock.sh
```

Bu betik 16 MB'ın tamamını okuyup `backup/x3_backup.bin` dosyasına yazıyor, dosyayı cihazın flash'ıyla karşılaştırıyor (`esptool verify-flash`) ve ancak ondan sonra SHA-256 değerini yanına yazıyor (`backup/x3_backup.bin.sha256`). Okuma test cihazında 72 saniye sürdü. Betik cihaza hiçbir şey yazmıyor ve var olan bir yedeğin üstüne yazmayı reddediyor.

**Yedeği kimseyle paylaşmayın.** İçinde üreticinin firmware'i ve sizin cihazınıza ait kalibrasyon var. Bir kopyasını projenin dışında da saklayın. `backup/` ve `*.bin` `.gitignore`'da; yanlışlıkla commit edilemez.

## 3. Derleme

```sh
cd firmware
../.venv/bin/pio run
cd ..
```

İlk derleme toolchain'i indiriyor (geliştirme makinesinde yaklaşık 5 dakika sürdü); sonrakiler birkaç saniye. Sonuç `firmware/.pio/build/x3/firmware.bin`, yaklaşık 1 MB.

## 4. Yükleme

```sh
tools/flash.sh
```

Bu betik yalnızca `0x10000` adresindeki uygulama bölümünü yazıyor. Bootloader, bölüm tablosu ve NVS (fabrika ayarları ve kalibrasyon) olduğu gibi kalıyor. esptool iş bitince cihazı yeniden başlatıyor ve GPU'cuk açılıyor.

- `esptool erase-flash` kullanmayın: NVS'i de siler.
- Yüklemeden sonra hâlâ fabrika firmware'i açılıyorsa cihazınız büyük ihtimalle ikinci uygulama yuvasından açılıyor (bkz. [hardware.md](hardware.md)). GPU'cuk'un betikleri bu durumu desteklemiyor; bir terslik görürseniz yedeğinizi geri yükleyin (aşağıda).
- `tools/flash.sh baska/bir.bin` başka bir derlemeyi, örneğin eski bir sürümü yüklüyor.

## 5. Seri log (isteğe bağlı)

```sh
.venv/bin/python tools/serial_log.py --seconds 120 --log runs/logs/demo.log
```

Portu açmak cihazı yeniden başlattığı için log açılıştan başlıyor. Log biçimi [firmware.md](firmware.md#seri-log) içinde.

## Fabrika firmware'ine dönüş

```sh
tools/restore_stock.sh                         # backup/x3_backup.bin dosyasını kullanır
tools/restore_stock.sh yol/x3_backup.bin       # ya da yanında .sha256 dosyası olan başka bir yedeği
```

Betik önce yedeği SHA-256 değeriyle karşılaştırıyor; tutmazsa hiçbir şey yazmadan duruyor. Sonra 16 MB'ın tamamını `0x0` adresinden başlayarak geri yazıyor.

Test cihazında yedek `verify-flash` ile cihazla karşılaştırıldı, ama henüz geri yazılması gerekmedi.

## Cihaz olmadan

Bir C/C++ derleyicisi ve yukarıdaki Python ortamıyla üç şey her bilgisayarda çalışıyor:

```sh
tools/host/build.sh && tools/host/llm_host -t 1.0 -p 0.9   # model, cihazın ayarlarıyla
tools/preview/build.sh                                     # ekranlar, runs/preview/ altında PNG olarak
tools/preview/gif.sh                                       # README'deki GIF'ler: runs/preview/demo_*.gif
```

`llm_host`, aynı seed ve ayarlarla cihazla aynı cevapları veriyor. `gif.sh`, cihazın inference ve çizim kodunu birlikte çalıştırıyor ve kareleri cihazdaki gibi zamanlıyor (modelin ölçülen hızı, panelin yenilenme süreleri); README'deki GIF'ler böyle üretildi. Seçenekleri [training.md](training.md#yeni-bir-modeli-kontrol-etmek) içinde.
