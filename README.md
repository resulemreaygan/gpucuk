# GPU'cuk

**A 142K-parameter language model running on an XTEINK X3 e-reader. No GPU, no FPU, still cocky.**<br>
**XTEINK X3 e-kitap okuyucuda çalışan 142 bin parametrelik bir dil modeli. GPU yok, FPU yok, özgüven tam.**

[English](#english) · [Türkçe](#türkçe)

## English

<img src="docs/images/demo_en.gif" width="300" alt="GPU'cuk answering &quot;What did mdisec say?&quot; with &quot;He called me GPU'cuk. Now I generate tokens.&quot;">

*Rendered on a computer by the device's own inference and drawing code, at the device's measured speed (`tools/preview/gif.sh`). The answer is a real output of the model for the seed on the screen, and `tools/host/llm_host` gives the same answer for that seed. A photo of the e-reader itself is under [License and thanks](#license-and-thanks).*

### The story

To enter the GPU raffle at a [latentshift.ai](https://latentshift.ai/) event, I left [a comment](https://www.linkedin.com/feed/update/urn:li:activity:7517143169021493248/?dashCommentUrn=urn%3Ali%3Afsd_comment%3A%287517153224177508352%2Curn%3Ali%3Aactivity%3A7517143169021493248%29) (translated from Turkish): "A GPU would be great for local LLM experiments. Let's see if the random seed is on our side this time."

At the event a surprise second prize appeared on stage, and my name came up for that one: an XTEINK X3 e-reader. [mdisec](https://github.com/mdisec), who had read my comment, couldn't resist: *"Al GPU'un, GPU'cuk… LLM çalıştırırsın."* Here's your GPU, GPU'cuk. Go run an LLM. (GPU'cuk is Turkish for "tiny GPU".) That's when the idea hit me: why not?

### What it is

- A tiny language model that thinks it is a giant GPU, running entirely on the e-reader's own chip: an ESP32-C3 with one RISC-V core at 160 MHz, no floating-point unit and 400 KB of RAM.
- 141,696 parameters, trained from scratch on 176 hand-written answers in Turkish and English plus template variations. Trained without a GPU too: about two minutes on a Mac's CPU.
- Pick one of 9 questions and press OK: the answer appears on the e-paper as the model writes it, at almost 4 characters per second. Shake the device for a new random seed and a new answer. Back toggles "overclock": a higher temperature and wilder answers.
- No network and no phone. Every character of an answer comes out of the model, on the device.

| | |
|---|---|
| Parameters | 141,696 (dim 64, 3 layers, 96-character context, 83-token vocabulary) |
| Weights | 554 KB of `float32`, read straight from flash |
| RAM for inference | 76 KB, of which 72 KB is the KV cache; no `malloc` |
| Speed on the device | 3.8–4.0 tokens per second |
| Free RAM while answering | 146 KB |
| The same answer on a Mac | about 2 ms (12.7 s on the e-reader) |

**It does not understand anything.** It knows its 9 questions, and with the device's settings 16% of its answers are hand-written lines, 63% are template combinations, 17% are new sentences made of real words and 5% are broken. How that was measured, and how it went from reciting to combining, is in the [memorization experiment](docs/en/memorization.md).

### Try it

Without a device, with only a C compiler:

```sh
git clone --recursive https://github.com/resulemreaygan/gpucuk.git
cd gpucuk
tools/host/build.sh
tools/host/llm_host -t 1.0 -p 0.9
```

This compiles the device's inference code for your computer and asks every question three times (seeds 1 to 3) with the device's normal settings. For the same seed and settings it gives the same answers as the e-reader.

On an XTEINK X3, read [Build and flash](docs/en/flashing.md) first. In short:

```sh
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
tools/backup_stock.sh                  # full backup of the device, before anything else
(cd firmware && ../.venv/bin/pio run)  # build
tools/flash.sh                         # write the app partition only
```

### Documentation

- [Hardware](docs/en/hardware.md): the X3, its ESP32-C3, flash layout, power and USB quirks
- [Model](docs/en/model.md): architecture, parameter and memory budget, sampling, speed
- [Data and training](docs/en/training.md): dataset and template format, recipe, reproducibility
- [Memorization experiment](docs/en/memorization.md): getting a 142K model to say something new
- [Firmware](docs/en/firmware.md): controls, streaming text onto e-paper, serial log
- [Build and flash](docs/en/flashing.md): backup, build, flash, back to the factory firmware
- [How it was built](docs/en/journey.md): milestones and lessons

### Repository layout

```
data/       questions, answers and answer templates (Turkish, English)
firmware/   PlatformIO project for the X3; the FreeInk SDK is a git submodule
tools/      training, memorization experiment, fonts, flashing, host runner, screen preview
docs/       documentation in English and Turkish, images
```

### License and thanks

MIT, see [LICENSE](LICENSE). Third-party parts (FreeInk SDK, Noto Sans) and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The inference code follows the structure of Andrej Karpathy's [llama2.c](https://github.com/karpathy/llama2.c), and the X3 build setup follows [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

Thanks to [latentshift.ai](https://latentshift.ai/) for the raffle, and to [mdisec](https://github.com/mdisec) for the idea. GPU'cuk said thanks too, in a sentence that is not in its training data ("I had a latent shift. Thanks, latentshift.ai."):

<img src="docs/images/device_latentshift.jpg" width="260" alt="GPU'cuk answering &quot;latentshift.ai nasıldı?&quot; with &quot;Latent shift yaşadım. Teşekkürler latentshift.ai.&quot;">

GPU'cuk is a hobby project by Resul Emre Aygan and is not affiliated with XTEINK. Flashing your device is at your own risk; back it up first.

---

## Türkçe

<img src="docs/images/demo_tr.gif" width="300" alt="GPU'cuk &quot;mdisec ne dedi?&quot; sorusuna &quot;Al GPU'un, GPU'cuk dedi. Ben de inandım.&quot; diye cevap veriyor">

*Bilgisayarda, cihazın kendi inference ve çizim koduyla, cihazın ölçülen hızında çizildi (`tools/preview/gif.sh`). Cevap, modelin ekrandaki seed ile verdiği gerçek cevap; `tools/host/llm_host` o seed için aynı cevabı veriyor. Cihazın gerçek fotoğrafı [Lisans ve teşekkür](#lisans-ve-teşekkür) bölümünde.*

### Hikâye

[latentshift.ai](https://latentshift.ai/) etkinliğindeki GPU çekilişine katılmak için şöyle bir [yorum](https://www.linkedin.com/feed/update/urn:li:activity:7517143169021493248/?dashCommentUrn=urn%3Ali%3Afsd_comment%3A%287517153224177508352%2Curn%3Ali%3Aactivity%3A7517143169021493248%29) yazmıştım: "Local LLM denemeleri için güzel bir GPU olurdu. Bakalım bu sefer random seed bizim tarafta mı?"

Etkinlikte sahneye sürpriz bir hediye daha geldi ve o çekilişte adım çıktı. Hediye bir XTEINK X3 e-kitap okuyucuydu. Yorumumu okuyan [mdisec](https://github.com/mdisec) de fırsatı kaçırmadı ve *"Al GPU'un, GPU'cuk… LLM çalıştırırsın."* dedi. O an aklıma bir fikir geldi ve "Neden olmasın?" dedim.

### Ne yapıyor?

- Kendini dev bir GPU sanan minik bir dil modeli, tamamen e-kitap okuyucunun kendi çipinde çalışıyor: tek RISC-V çekirdekli, 160 MHz'lik, FPU'su (kayan nokta birimi) olmayan, 400 KB RAM'li bir ESP32-C3.
- 141.696 parametre. Sıfırdan, Türkçe ve İngilizce elle yazılmış 176 cevap ve bunların kalıplardan üretilen çeşitlemeleriyle eğitildi. Eğitimde de GPU yok: bir Mac'in işlemcisinde yaklaşık iki dakika.
- Dokuz sorudan birini seçip Onay'a basınca cevap, model yazdıkça e-ink ekrana geliyor: saniyede 4'e yakın karakter. Cihazı sallayınca yeni bir random seed ve yeni bir cevap geliyor. Geri tuşu "overclock"u açıp kapatıyor: temperature yükseliyor, cevaplar çılgınlaşıyor.
- İnternet yok, telefon yok. Cevabın her karakteri cihazın içinde, modelden çıkıyor.

| | |
|---|---|
| Parametre | 141.696 (dim 64, 3 katman, 96 karakterlik bağlam, 83 token'lık sözlük) |
| Ağırlıklar | 554 KB `float32`, doğrudan flash'tan okunuyor |
| Inference için RAM | 76 KB, bunun 72 KB'ı KV cache; `malloc` yok |
| Cihazdaki hız | saniyede 3,8–4,0 token |
| Cevap verirken boş RAM | 146 KB |
| Aynı cevap Mac'te | yaklaşık 2 ms (e-kitap okuyucuda 12,7 sn) |

**Hiçbir şey anlamıyor.** Yalnızca 9 sorusunu biliyor. Cihazdaki ayarlarla cevaplarının %16'sı elle yazılmış cümleler, %63'ü kalıp kombinasyonları, %17'si gerçek kelimelerden kurulmuş yeni cümleler, %5'i bozuk. Bunun nasıl ölçüldüğü ve modelin ezberden kombinasyona nasıl geçtiği [ezber deneyi](docs/tr/memorization.md) sayfasında anlatılıyor.

### Deneyin

Cihaz olmadan, yalnızca bir C derleyicisiyle:

```sh
git clone --recursive https://github.com/resulemreaygan/gpucuk.git
cd gpucuk
tools/host/build.sh
tools/host/llm_host -t 1.0 -p 0.9
```

Bu, cihazdaki inference kodunu bilgisayarınız için derliyor ve her soruyu cihazın normal ayarlarıyla üç kez soruyor (seed 1, 2 ve 3). Aynı seed ve ayarlarla e-kitap okuyucuyla aynı cevapları veriyor.

XTEINK X3'e yüklemek için önce [Derleme ve yükleme](docs/tr/flashing.md) sayfasını okuyun. Kısaca:

```sh
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
tools/backup_stock.sh                  # her şeyden önce cihazın tam yedeği
(cd firmware && ../.venv/bin/pio run)  # derle
tools/flash.sh                         # yalnızca uygulama bölümünü yaz
```

### Dokümanlar

- [Donanım](docs/tr/hardware.md): X3, ESP32-C3, flash düzeni, güç ve USB'nin huyları
- [Model](docs/tr/model.md): mimari, parametre ve bellek bütçesi, örnekleme, hız
- [Veri ve eğitim](docs/tr/training.md): veri seti ve kalıp biçimi, eğitim ayarları, yeniden üretilebilirlik
- [Ezber deneyi](docs/tr/memorization.md): 142 bin parametrelik bir modele yeni bir şey söyletmek
- [Firmware](docs/tr/firmware.md): tuşlar, metnin e-ink ekranda canlı gösterilmesi, seri log
- [Derleme ve yükleme](docs/tr/flashing.md): yedek, derleme, yükleme, fabrika firmware'ine dönüş
- [Nasıl yapıldı](docs/tr/journey.md): aşamalar ve öğrenilenler

### Repo yapısı

```
data/       sorular, cevaplar ve cevap kalıpları (Türkçe, İngilizce)
firmware/   X3 için PlatformIO projesi; FreeInk SDK bir git submodule
tools/      eğitim, ezber deneyi, fontlar, yükleme, bilgisayarda çalıştırma, ekran önizlemesi
docs/       Türkçe ve İngilizce dokümanlar, görseller
```

Kod, dosya adları ve yorumlar İngilizce; cihazın arayüzü ve veri seti Türkçe ve İngilizce.

### Lisans ve teşekkür

MIT, bkz. [LICENSE](LICENSE). Üçüncü taraf bileşenler (FreeInk SDK, Noto Sans) ve lisansları [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) içinde. Inference kodunun yapısı Andrej Karpathy'nin [llama2.c](https://github.com/karpathy/llama2.c) projesini örnek alıyor; X3 derleme ayarları [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)'dan alındı.

Çekiliş için [latentshift.ai](https://latentshift.ai/)'a, fikir için [mdisec](https://github.com/mdisec)'e teşekkürler. GPU'cuk da teşekkür etti, hem de eğitim verisinde olmayan bir cümleyle:

<img src="docs/images/device_latentshift.jpg" width="260" alt="GPU'cuk &quot;latentshift.ai nasıldı?&quot; sorusuna &quot;Latent shift yaşadım. Teşekkürler latentshift.ai.&quot; diye cevap veriyor">

GPU'cuk, Resul Emre Aygan'ın hobi projesi; XTEINK ile resmî bir bağlantısı yok. Cihaza firmware yüklemek kendi sorumluluğunuzda; önce yedek alın.
