# Ezber deneyi

[English](../en/memorization.md)

v0.3 çalışıyordu ama yalnızca ezberden okuyordu: normal modda örneklenen 720 cevabın hepsi veri setindeki bir cümlenin kelimesi kelimesine aynısıydı. Soru şuydu: 142 bin parametrelik bir model, **modelin kendisi değişmeden** yeni bir şey söyleyebilir mi? Mimari, C kodu, hız ve RAM aynı kalacak; yalnızca veri ve eğitim değişebilir.

## Neden ezberliyor?

Elle yazılmış 176 cevap yaklaşık 6.900 karakter; karşısında 141 bin parametre var. Model her cevabı ezberliyor. Aşağıdaki A varyantında ölçüldü: cevabın ilk karakterine ortalama %29 olasılık veriyor, yani seçim orada yapılıyor; geri kalan karakterlerin %97'sinde %99'dan fazla emin. Temperature'ı artırmak işe yaramıyor: model ezber yolundan çıkıyor, ama o cümlelerin dışında dil bilmediği için anlamsız şeyler üretiyor.

## Deney düzeneği

`tools/memorization_experiment.py`, `train.py`'nin modeli ve öğrenme oranı planıyla altı varyant eğitiyor (aksi belirtilmedikçe 3.000 adım, weight decay 0,01, seed 1337). Cihazdaki modele hiç dokunulmuyor.

| Varyant | Veri | Eğitim |
|---|---|---|
| A | yalnızca elle yazılmış cevaplar | dropout yok (v0.3 böyle eğitildi) |
| B | yalnızca elle yazılmış cevaplar | 800 adım (az eğitim) |
| C | yalnızca elle yazılmış cevaplar | dropout 0,2 |
| D | elle yazılmış + kalıplar | dropout yok |
| E | elle yazılmış + kalıplar | dropout 0,1 |
| F | elle yazılmış + kalıplar | dropout 0,2 |

Kalıplı varyantlar elle yazılmış cevapları iki kez, [kalıp](training.md#kalıplar) cevaplarının da %75'ini görüyor. Kalan %25 (132 Türkçe, 134 İngilizce) eğitimden saklanıyor: model için yeni, ama düzgün olduğu bilinen cümleler.

Her varyant her soruyu iki dilde 40 kez cevaplıyor; ayar başına 720 cevap. Örnekleme cihazdakiyle aynı şekilde çalışıyor (önce temperature, sonra top-p, BOS hiç seçilmiyor), ama rastgele sayılar PyTorch'tan geliyor; yani belli bir seed cihazdaki cevabı vermiyor. İki ayar: v0.3'ün normal modu (temperature 1,2, top-p 0,9) ve v0.3'ün overclock'u (1,6, top-p kapalı).

Her cevap dört gruptan birine giriyor:

- **ezber:** varyantın eğitimde gördüğü bir cevabın aynısı;
- **saklı:** eğitimden saklanan bir kalıp cevabı;
- **yeni:** hiçbir veri dosyasında yok, ama her kelimesi o dilin verisinde bir yerde geçiyor;
- **bozuk:** veride geçmeyen en az bir kelime var (yazım hatası, anlamsız kelime ya da öbür dil).

Kelime kontrolü kaba bir ölçüt. "Zero. But FPU of books." yeni sayılıyor; cihazın ürettiği "mdisec haklıydı. Şimdi saniyede birkaç karakter yazıyor." ise bozuk sayılıyor, çünkü veride "yazıyorum" var ama "yazıyor" yok.

"Farklı", aynı soruda 40 denemede ortalama kaç farklı cevap çıktığı.

## Sonuçlar

Normal (temperature 1,2, top-p 0,9):

| Varyant | ezber | saklı | yeni | bozuk | farklı | son kayıp |
|---|---|---|---|---|---|---|
| A: elle yazılmış | %100 | %0 | %0 | %0 | 9,5 | 0,059 |
| B: elle yazılmış, 800 adım | %96 | %0 | %0 | %4 | 10,3 | 0,068 |
| C: elle yazılmış, dropout 0,2 | %98 | %0 | %1 | %1 | 9,7 | 0,066 |
| D: kalıplar | %97 | %1 | %2 | %0 | 23,6 | 0,104 |
| **E: kalıplar, dropout 0,1** | %72 | **%3** | **%18** | %7 | 27,4 | 0,120 |
| F: kalıplar, dropout 0,2 | %33 | %4 | %27 | %36 | 35,3 | 0,168 |

Overclock (temperature 1,6, top-p kapalı):

| Varyant | ezber | saklı | yeni | bozuk | farklı |
|---|---|---|---|---|---|
| A: elle yazılmış | %66 | %0 | %0 | %34 | 22,2 |
| B: elle yazılmış, 800 adım | %9 | %0 | %1 | %90 | 39,2 |
| C: elle yazılmış, dropout 0,2 | %37 | %0 | %4 | %59 | 31,6 |
| D: kalıplar | %31 | %1 | %5 | %63 | 37,9 |
| E: kalıplar, dropout 0,1 | %21 | %1 | %13 | %66 | 38,6 |
| F: kalıplar, dropout 0,2 | %4 | %1 | %9 | %86 | 39,9 |

## Ne gösteriyor?

- **Yalnızca eğitimi gevşetmek bir işe yaramıyor.** Daha az eğitimle (B) ya da daha çok dropout'la (C) model yine ezberden okuyor; ezberden okumadığı şeylerin çoğu da bozuk. Sorun fazla eğitim değil, verinin azlığı.
- **Kalıplar tek başına çeşitliliği iki katından fazla artırıyor** (40 denemede 9,5 → 23,6 farklı cevap), ama model bu kez daha büyük veri setini ezberliyor: %97 ezber.
- **Kalıplar ve biraz dropout (E), gerçekten yeni cümle kuran ilk varyant:** %18 yeni ve hiç görmediği kalıp cevaplarından %3, buna karşılık %7 bozuk. Örneğin:
  - "A single-core, fanless RISC-V legend. The rest is talent."
  - "E-ink ekranlı, pilli bir süper bilgisayar. Gerisi yetenek."
  - "Hayatım değişti. Orada çıktım."
- **Daha çok dropout (F)** daha çok yeni cevap veriyor, ama bütün cevapların üçte biri bozuk.
- v0.3'ün overclock ayarı E için fazla sıcak: üçte iki bozuk.

Bunların hiçbiri cihaz tarafını değiştirmiyor: dropout yalnızca eğitimde var, model aynı boyutta ve aynı hızda.

## Cihazdaki model (v0.4)

E'nin ayarları `train.py`'nin varsayılanı oldu, iki farkla: hiçbir cevap saklanmıyor, ve örnekleme ayarları yeni modele göre düşürüldü. `--eval` ile, ayar başına 720 cevapla ölçüldü:

| Ayar | elle yazılmış | kalıp | yeni | bozuk | farklı |
|---|---|---|---|---|---|
| Normal (1,0, top-p 0,9) | %16 | %63 | %17 | %5 | 26,9 |
| Overclock (1,4, top-p 0,9) | %11 | %48 | %22 | %19 | 32,7 |

Burada "kalıp", modelin eğitimde gördüğü kalıp cevaplarından biri; "yeni" ise iki dosyada da olmayan bir cümle.

v0.4'ün cihazdaki seri logunda (yaklaşık yarım saatlik kullanım) 17 farklı cevabın 5'i yeniydi:

- "mdisec haklıydı. Şimdi o izliyor, ben üretiyorum." (seed 4813)
- "Adım GPU'cuk. Yani cebe sığan, RISC-V bir efsane." (seed 4238)
- "Hoş geldin! Bende fan yok ama kitap çok." (seed 812)
- "Selam! Bende FPU yok ama karizmam var." (seed 46)
- "Çok iyiydi. Teşekkürler latentshift.ai." (seed 515)

Bedeli: elle yazılmış espriler artık cevapların yalnızca %16'sı ve yaklaşık her yirmi cevaptan biri bozuk.

## Çalıştırmak

```sh
.venv/bin/python tools/memorization_experiment.py                  # altı varyantın hepsi: geliştirme Mac'inde 16 dakika
.venv/bin/python tools/memorization_experiment.py --only A,E       # bir kısmı
.venv/bin/python tools/memorization_experiment.py --eval runs/gpucuk.pt
```

Tam çalıştırma `runs/experiment/report.md` dosyasını (yukarıdaki tablolar ve her gruptan örnek cevaplar) ve her varyant için bir checkpoint yazıyor. `--eval` ise eğitilmiş bir model için cihaz ayarlarındaki tabloyu ve örnekleri yazdırıyor.
