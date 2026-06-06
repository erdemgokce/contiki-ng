## 25. OTA Sistem Mimarisi, Protokol Yöntemleri ve Veri Bütünlüğü

Projede uygulanan kablosuz bellenim güncelleme (OTA) senaryosu, düşük güç tüketen IoT ağlarının kısıtlarına göre optimize edilmiştir. Aşağıda uygulanan yöntemler, paket analizi ve alınan hata tolerans (robustness) önlemleri detaylandırılmıştır.

### 🎥 Proje Video Sunumu
Sistemin Cooja üzerindeki canlı çalışması, teorik altyapısı ve kod anlatımı aşağıdaki bağlantıda sunulmuştur:
YOUTUBE LİNKİ: https://youtu.be/Hb6TJ7GucB4


### A. Uygulanan İletişim Yöntemi (Stop-and-Wait ARQ)
Sistemde onay mekanizmalı **Stop-and-Wait ARQ (Otomatik Tekrar İsteği)** yöntemi kullanılmıştır. 
* Gönderici düğüm (Client), bir firmware bloğunu yollar ve beklemeye geçer.
* Alıcı düğüm (Server), bloğu doğrular, diske (CFS) yazar ve o blok numarası için bir ACK (Onay) paketi geri döner.
* Eğer ACK kaybolur veya paket bozuk giderse, sunucu yanıt vermez; gönderici "Time-Out" (Zaman Aşımı) yaşayarak aynı paketi tekrar iletir.

### B. Paket Yapısı ve Uzunluk Analizi
Büyük bir firmware imajı doğrudan tek parça iletilemeyeceği için `BLOCK_SIZE` sabiti ile 64 baytlık parçalara (chunking) bölünmüştür. C dili seviyesinde `__attribute__((packed))` kullanılarak paket içindeki boşluklar (padding) engellenmiştir.

* **Paket Formatı:** 2 Bayt (Sıra No) + 1 Bayt (Uzunluk) + 2 Bayt (Checksum) + 64 Bayt (Payload) = **Toplam 69 Bayt**.
* **Neden 64 Bayt?** IEEE 802.15.4 radyo standardında maksimum çerçeve boyutu 127 bayttır. IPv6 ve UDP başlıkları çıkarıldığında, MAC katmanında parçalanmaya (fragmentation) maruz kalmadan en güvenli iletilebilecek payload boyutu 64 bayt olarak belirlenmiştir.

### C. Alınan Güvenlik Önlemleri ve Checksum Teorisi
Havadan iletişim sırasındaki gürültü ve veri bozulmalarına karşı üç farklı önlem alınmıştır:

1. **Bütünlük Kontrolü (16-bit Arithmetic Checksum):** CPU frekansı düşük cihazlarda batarya ömrünü korumak adına ağır kriptografik özetler yerine 16-bitlik aritmetik sağlama toplamı kullanılmıştır. Gelen her bayt üst üste toplanarak 16-bitlik bir şifre üretir ve bozuk paketler anında reddedilir.
2. **Sıra Numarası (Sequence) Koruması:** Gelen paketin sıra numarası sunucudaki beklenen numara ile kıyaslanır. Kayıp veya kopya paketler bu sayede engellenir.
3. **Tam İmaj Doğrulaması (End-of-File CRC32):** Dosya aktarımı bittiğinde istemci `0xFFFF` numaralı özel bir paket gönderir. Bu paketin içinde tüm dosyanın 32-bit CRC özeti vardır. Sunucu diske yazdığı dosyayı baştan aşağı okuyup bu CRC ile eşleştirir, böylece nihai imajın tam anlamıyla kusursuz olduğu kanıtlanır.
EOF
