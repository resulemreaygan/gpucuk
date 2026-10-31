# Model

[English](../en/model.md)

GPU'cuk, Llama ile aynı yapı taşlarından oluşan, sıfırdan eğitilmiş karakter düzeyinde bir transformer. 141.696 parametresi var.

## Neden karakter düzeyi?

- Her karakter bir token: tokenizer dosyası gerekmiyor, Türkçe harfler (ç, ğ, ı, İ, ö, ş, ü) sorunsuz çalışıyor.
- Sözlük, eğitim verisinde geçen bütün karakterler ve bir BOS token'ından oluşuyor: 83 token. Küçük sözlük, çıkış katmanını da küçük tutuyor.
- Bedeli: her karakter için modelin bir kez çalışması gerekiyor. Kısa bir soruya 40 karakterlik bir cevap yaklaşık 50 adım sürüyor.

## Mimari

```
token embedding (83 × 64)
3 × blok:
    RMSNorm → attention: 4 query head, 2 key/value head (grouped-query attention),
              head boyutu 16, RoPE (komşu çiftler, θ = 10000) → + residual
    RMSNorm → SwiGLU feed-forward, gizli boyut 172 → + residual
RMSNorm → çıkış katmanı (ağırlıkları token embedding ile ortak)
```

Bağlam 96 token: BOS, `> `, soru, satır sonu ve cevap. Boyutlar `tools/train.py` içindeki `Config`'te; dışa aktarılan başlık dosyası onları C koduna `LLM_*` tanımları olarak taşıyor.

## Parametre bütçesi

| Parça | Parametre |
|---|---|
| Token embedding (aynı zamanda çıkış katmanı) | 83 × 64 = 5.312 |
| Katman başına attention (wq, wk, wv, wo) | 64·64 + 2·(32·64) + 64·64 = 12.288 |
| Katman başına feed-forward (w1, w2, w3) | 3 · 64·172 = 33.024 |
| Katman başına iki norm | 2 · 64 = 128 |
| 3 katman | 3 · 45.440 = 136.320 |
| Son norm | 64 |
| **Toplam** | **141.696** |

## Cihazdaki bellek

- **Ağırlıklar:** 141.696 `float` = 566.784 bayt (554 KB). Firmware'e derlenmiş bir `const` dizi oldukları için flash'ta kalıyor ve flash önbelleği üzerinden okunuyorlar. RAM harcamıyorlar.
- **KV cache:** her katman ve konum için key ve value değerleri, 2 × 3 × 96 × 32 × 4 bayt = 73.728 bayt (72 KB). Query head'i kadar key/value head'i olsaydı bunun iki katı olurdu.
- **Geri kalan her şey** (aktivasyonlar, logit'ler, örnekleyicinin çalışma alanı): 4.000 bayt.

Toplam 77.728 bayt (76 KB) ve hepsi statik: inference kodu hiç `malloc` çağırmıyor. Firmware'in tamamı için linker 95.216 bayt statik RAM bildiriyor; cihaz da cevap verirken 146 KB boş heap gösteriyor.

## Inference kodu

`firmware/src/llm/llm.c` yaklaşık 300 satırlık düz C. Yapısı Andrej Karpathy'nin [llama2.c](https://github.com/karpathy/llama2.c) projesindeki `run.c`'yi örnek alıyor ama bu model için yeniden yazıldı: dosya okuma yok, heap yok, ağırlıklar doğrudan `model_data.h` içindeki diziden okunuyor. `tools/train.py` ağırlıkları tam olarak `llm.c`'nin okuduğu sırayla dışa aktarıyor.

Aynı dosya hem cihaz için hem bilgisayar için derleniyor (`tools/host/llm_host`). Böylece model donanım olmadan kolayca test ediliyor:

- Temperature 0'da C kodu 18 sorunun hepsinde (dil başına 9) PyTorch ile aynı cevabı veriyor.
- Örneklemeyle, aynı seed ve ayarlarda bilgisayar cihazla aynı cevabı veriyor: v0.4 cihaz logundaki 21 cevabın hepsi birebir aynı üretildi.

## Örnekleme

- Önce temperature, sonra top-p (nucleus sampling) uygulanıyor. BOS hiçbir zaman seçilmiyor. Cevap, model satır sonu ürettiğinde ya da bağlam dolduğunda bitiyor.
- Rastgele sayılar xorshift64\* ile üretiliyor, başlangıç durumu `(seed << 1) | 1`. Aynı seed, soru ve ayarlar aynı cevabı veriyor.
- Küçük bir kısayol: olasılığı `(1 − top_p) / (sözlük − 1)` değerinin altında kalan bir karakter nucleus'a asla giremez; bu yüzden yalnızca bu eşiğin üstündeki karakterler sıralanıyor.

## Hız

Model cihazda **saniyede 3,8 ile 4,0 token** işliyor. Bu sayıya modelden geçen bütün token'lar (soru ve cevap) giriyor, süreye ise yalnızca modelde geçen zaman; ekranı beklemek dahil değil.

Cevap süresi doğrusaldan biraz hızlı büyüyor, çünkü attention önceki bütün konumlara bakıyor. v0.4 seri logundaki 14 farklı ölçüm şu formülden en fazla 0,05 saniye sapıyor:

```
saniye ≈ 0,233 · n + 0,00043 · n²      (n = token)
```

Örneğin "mdisec ne dedi?" sorusu ve 49 karakterlik bir cevap 68 token ediyor: 17,8 saniye.

Süre nereye harcanıyor? 160 MHz, saniyede 160 milyon clock cycle demek; token başına 0,23 saniye de kabaca 37 milyon cycle. Bu sürede ağırlıklarla yaklaşık 142 bin çarpma-toplama yapılıyor, yani her birine yaklaşık 260 cycle düşüyor: kayan nokta hesabını yazılımla yapmanın bedeli. İlk denemeden önceki tahmin saniyede yaklaşık 3 token'dı; cihazdaki ilk ölçüm 3,9 çıktı.

Henüz yapılmadı: int8 ağırlıklar ve tam sayı aritmetiği. Ağırlıkları dörtte birine indirir ve çok daha hızlı olması beklenir, ama demo için gerekmedi.

## Sınırlar

142 bin parametre, elle yazılmış 176 cevap (yaklaşık 6.900 karakter) ve onların kalıp çeşitlemeleriyle eğitilince soruları anlayamıyor. Model 9 hazır sorusunu biliyor, firmware de yalnızca onları soruyor. Cevapların çoğu veri setindeki cümleler ya da onların parçalarının yeni kombinasyonları; kaçının gerçekten yeni olduğu ve bunun nasıl ölçüldüğü [memorization.md](memorization.md) içinde.
