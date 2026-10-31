# Veri ve eğitim

[English](../en/training.md)

Model, bir bilgisayarın işlemcisinde PyTorch ile eğitiliyor; hiçbir aşamada GPU yok. Geliştirmenin yapıldığı Apple Silicon Mac'te eğitim yaklaşık iki dakika sürüyor.

## Veri seti

`data/gpucuk_tr.txt` ve `data/gpucuk_en.txt` elle yazılmış soruları ve cevapları içeriyor:

```
> GPU ver
Önce kitapları bitir.
GPU benim. Sen kitapları oku.
...
```

- Önce bir `> soru` satırı, sonra her satıra bir cevap. Boş satır grubu bitiriyor, `#` yorum başlatıyor.
- Her dilde 9 soru ve 88 cevap. İngilizce cevaplar çeviri değil, aynı tonda ayrı espriler.
- İki dosyada sorular aynı sırada; cihazda dil değişince soru aynı kalıyor.
- Ton: kendini dev bir GPU sanan minik bir cihaz. Kibirli ama sevimli.
- Cevaplar 64 karakteri geçmemeli (bundan uzun kalıp cevapları atılıyor). Soru ve cevap birlikte 96 token'lık bağlama sığmalı; sığmayan olursa `train.py` hata verip duruyor.

Dil etiketi yok. Tek model iki dili birden öğreniyor; cevabın dilini sorunun dili belirliyor.

## Kalıplar

176 cevap az: yalnızca onlarla eğitilen model cevapları kelimesi kelimesine ezberden okuyor. `data/templates_tr.txt` ve `data/templates_en.txt` aynı esprileri değiştirilebilir parçalarla tarif ediyor:

```
@missing: CUDA | fan | FPU | ekran kartı | soğutucu | tensor çekirdeği
@plenty: kitap | karizma | sabır | özgüven | stil | hayal gücü

> GPU ver
Bende {missing} yok ama {plenty} çok.
```

- `@ad: a | b | c` bir değişkeni ve alabileceği değerleri tanımlıyor. Değişkenler dosyadaki bütün sorular için ortak.
- Bir `> soru` satırının altındaki satırlar kalıp; `{ad}` değişkenin her değeriyle dolduruluyor.
- `train.py` bütün kombinasyonları karıştırıyor (seed 1337), aynı değeri iki kez kullananları ve fazla uzun olanları atlıyor, kalıp başına en fazla 40 tane tutuyor. Elle yazılmış dosyada zaten olan cevaplar bir daha eklenmiyor.

Böylece 529 Türkçe ve 536 İngilizce cevap daha çıkıyor. Neden kalıp kullanıldığı ve neyi değiştirdiği [memorization.md](memorization.md) içinde ölçüldü.

## Model ne görüyor?

Her eğitim örneği tek bir dizi: `BOS`, `> `, soru, satır sonu, cevap ve kapanış satır sonu. Kayıp (loss) yalnızca cevap ve onun satır sonu için hesaplanıyor; soru yalnızca bağlam. Cihazda `llm.c` soruyu tam olarak aynı biçimde veriyor ve model satır sonu yazana kadar örnekliyor.

Eğitim kümesi: elle yazılmış her cevap iki kez, artı bütün kalıp cevapları; toplam 1.417 örnek.

## Eğitim ayarları

| Ayar | Değer |
|---|---|
| Adım | 3.000; her adımda rastgele seçilen 32 örnek |
| Optimizer | AdamW, en yüksek öğrenme oranı 3e-3, betas (0,9, 0,95), weight decay 0,01 |
| Öğrenme oranı | 150 adım doğrusal warmup, sonra kosinüs eğrisiyle en yüksek değerin %10'una iniş |
| Düzenlileştirme | Dropout 0,1, gradyan kırpma 1,0 |
| Seed | Her şey için 1337 (PyTorch, Python, kalıpların açılması) |
| Son kayıp | 0,128 (bütün eğitim örnekleri üzerinde) |

## Çalıştırmak

```sh
.venv/bin/python tools/train.py
```

Şunları yazıyor:

- `firmware/src/llm/model_data.h`: model boyutları (`LLM_*` tanımları), sözlük, sorular ve bütün ağırlıklar bir `const float` dizi olarak (yaklaşık 2,5 MB C kaynağı, derlenince 554 KB).
- `runs/gpucuk.pt`: değerlendirme araçları için PyTorch checkpoint'i.

Ayrıca her 200 adımda kaybı ve her soru için en olası cevabı (`[t=0]`) ile iki örneklenmiş cevabı yazdırıyor.

Diğer seçenekler: `--steps`, `--dropout`, `--no-templates`, `--original-repeat` (elle yazılmış cevapların kaç kez tekrarlanacağı; varsayılan 2), `--dim` ve `--layers`. C kodu bütün boyutları `model_data.h`'den okuduğu için farklı boyutta bir model kod değişikliği gerektirmiyor; bağlam 96'da sabit, RAM kullanımı ise `--dim` ve `--layers` ile büyüyor (bkz. [model.md](model.md)).

## Yeni bir modeli kontrol etmek

```sh
tools/host/build.sh                       # llm.c'yi bilgisayar için derle
tools/host/llm_host -t 0 -n 1             # eğitim logundaki "[t=0]" satırlarıyla aynı olmalı
tools/host/llm_host -t 1.0 -p 0.9 -s 42 -n 1 2
.venv/bin/python tools/memorization_experiment.py --eval runs/gpucuk.pt
```

Üçüncü satır 3. soruyu ("GPU ver" / "Give me a GPU") cihazın normal modda seed 42 ile verdiği şekilde cevaplıyor. Yayımlanan modelde:

```
141696 parameters, temp 1.00, top-p 0.90
[tr] > GPU ver
  [s=42] Önce kitapları bitir. Sonra belki H100.   (39 chars, 2.21 ms)
[en] > Give me a GPU
  [s=42] You're already holding me. Turn the page first.   (47 chars, 2.83 ms)
```

Süreler bilgisayara göre değişir. Geliştirme Mac'inde Türkçe cevap yaklaşık 2 milisaniye sürdü; e-kitap okuyucu aynı cevap için 12,7 saniye harcıyor. Bütün sorular için sondaki `2`'yi silin, başka seed'ler için `-s` ve `-n`'i değiştirin.

Son satır, cihazın ayarlarıyla her soru ve dil için 40 cevap örnekliyor ve onları elle yazılmış, kalıp, yeni ve bozuk diye ayırıyor (bkz. [memorization.md](memorization.md)).

## Yeniden üretilebilirlik

Bu repodaki `model_data.h`, cihazda çalışan modelin kendisi. Geliştirmenin yapıldığı makinede, `requirements.txt`'teki paket sürümleriyle (torch 2.14.1) `tools/train.py` onu bit bit aynı üretiyor. Başka bir torch sürümü ya da başka bir işlemci biraz farklı ağırlıklar verebilir.

## Karakterler ve font

Sözlük veriden çıkarıldığı için veriye eklenen yeni karakterler kendiliğinden yeni token oluyor. Cihazın fontunda ise yalnızca yazdırılabilir ASCII, Türkçe harfler ve `·`, `…`, `—`, `–`, `’`, `“`, `”`, `°`, `×` sembolleri var; geri kalan her şey `?` olarak çiziliyor. Başka karakterler için onları `tools/make_font.py` içindeki `CHARSET`'e ekleyip `tools/make_fonts.sh`'ı çalıştırın.
