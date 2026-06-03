#include "cfs/cfs.h" // Dosya sistemi (Coffee File System)
#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/netstack.h"
#include "net/routing/routing.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define BLOCK_SIZE 64

// HOCANIN İSTEDİĞİ KUTSAL BOYUT
#define TARGET_FIRMWARE_SIZE 16384
static struct simple_udp_connection udp_conn;

// Durum yönetimi değişkenleri
static int fd = -1;
static uint16_t expected_block = 0;
static uint32_t total_received_bytes = 0;
static bool transfer_completed = false;

// Paket Yapısı (Gönderici ile BİREBİR aynı olmalı)
typedef struct {
  uint16_t block_no;
  uint8_t data_len;
  uint16_t checksum;
  uint8_t payload[BLOCK_SIZE];
} __attribute__((packed)) firmware_packet_t;

// Parça Doğrulama (Checksum)
uint16_t calculate_checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  uint8_t i;
  for (i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}

/* İş Parçacığını (Process) Tanımlıyoruz */
PROCESS(udp_server_process, "UDP server (Firmware Receiver)");
AUTOSTART_PROCESSES(&udp_server_process);

/* Gelen UDP paketlerini yakalayan Callback Fonksiyonu */
static void udp_rx_callback(struct simple_udp_connection *c,
                            const uip_ipaddr_t *sender_addr,
                            uint16_t sender_port,
                            const uip_ipaddr_t *receiver_addr,
                            uint16_t receiver_port, const uint8_t *data,
                            uint16_t datalen) {
  if (datalen == sizeof(firmware_packet_t)) {
    firmware_packet_t pkt;
    uint16_t local_checksum;
    uint16_t ack_block;

    // Gelen ham byte verisini struct yapımıza kopyalıyoruz
    memcpy(&pkt, data, sizeof(firmware_packet_t));

    // Tüm imaj doğrulama — END paketi kontrolü
    if (pkt.block_no == 0xFFFF) {
      uint32_t received_crc;
      memcpy(&received_crc, pkt.payload, sizeof(uint32_t));

      uint32_t computed_crc = 0;
      uint8_t buf[64];
      int n;
      int verify_fd = cfs_open("firmware.bin", CFS_READ);
      if (verify_fd >= 0) {
        while ((n = cfs_read(verify_fd, buf, sizeof(buf))) > 0) {
          int j;
          for (j = 0; j < n; j++) {
            computed_crc += buf[j];
          }
        }
        cfs_close(verify_fd);
      }

      if (computed_crc == received_crc) {
        LOG_INFO("TUM IMAJ DOGRULAMASI BASARILI! CRC: %lu\n",
                 (unsigned long)computed_crc);
      } else {
        LOG_ERR("IMAJ DOGRULAMASI BASARISIZ! Gelen: %lu, Hesaplanan: %lu\n",
                (unsigned long)received_crc, (unsigned long)computed_crc);
      }
      return;
    }

    // 1. İş Parçacığı: Parça Doğrulama (Checksum Kontrolü)
    local_checksum = calculate_checksum(pkt.payload, pkt.data_len);
    if (local_checksum != pkt.checksum) {
      LOG_ERR("Blok %d bozuk geldi! Checksum hatasi.\n", pkt.block_no);
      return; // Hatalıysa ACK atma, gönderici time-out olup tekrar yollasın
    }

    // 2. İş Parçacığı: Sıralama Kontrolü
    if (pkt.block_no == expected_block) {
      if (fd >= 0 && !transfer_completed) {

        // Kalıcı depolama alanına (CFS disk) yazma
        int written = cfs_write(fd, pkt.payload, pkt.data_len);
        if (written != pkt.data_len) {
          LOG_ERR("Diske yazma hatasi gerceklesti!\n");
          return;
        }

        total_received_bytes += pkt.data_len;
        LOG_INFO("Blok %d diske yazildi. Toplam: %lu bayt\n", pkt.block_no,
                 total_received_bytes);

        // 3. İş Parçacığı: Tüm İmaj Doğrulama
        if (total_received_bytes >= TARGET_FIRMWARE_SIZE &&
            !transfer_completed) {
          transfer_completed = true;
          cfs_close(fd); // Dosyayı kapatıp kalıcı olarak sakla
          fd = -1;

          // Hocanın birebir yazdırılmasını istediği o mesaj
          LOG_INFO("Yüklenmeye hazır yeni firmware alımı tamamlandı.\n");
        }
      }
      // Beklenen bloğu bir artır
      expected_block++;

    } else if (pkt.block_no < expected_block) {
      // Eğer göndericinin ACK'si kaybolmuş ve aynı bloğu tekrar atmışsa
      LOG_WARN("Eski blok tekrar geldi: %d\n", pkt.block_no);
    } else {
      LOG_WARN("Beklenmeyen blok geldi! Beklenen: %d, Gelen: %d\n",
               expected_block, pkt.block_no);
      return;
    }

    // 4. İş Parçacığı: Durum Yönetimi (Göndericiye ACK Dönme)
    ack_block = pkt.block_no;
    simple_udp_sendto(&udp_conn, &ack_block, sizeof(uint16_t), sender_addr);
  }
}

/* Ana İş Parçacığı */
PROCESS_THREAD(udp_server_process, ev, data) {
  PROCESS_BEGIN();

  // Temiz bir başlangıç için eski diski sil
  cfs_remove("firmware.bin");

  // Coffee File System'i (Diski) yazma formatında aç
  fd = cfs_open("firmware.bin", CFS_WRITE);
  if (fd < 0) {
    LOG_ERR("HATA: Dosya sistemi (Coffee) acilamadi!\n");
    PROCESS_EXIT();
  }

  /* RPL Root olarak kendini ata (Ağı kur) */
  NETSTACK_ROUTING.root_start();

  /* UDP Bağlantısını Başlat */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT,
                      udp_rx_callback);

  LOG_INFO("Alici dugum (ID:1) hazir. Firmware bekleniyor...\n");

  // İşlem bitene kadar açık tut
  // Bu en sessiz bug. Contiki-NG'de simple_udp callback'i, tcpip_process içinde
  // çalışır ve senin process'ine event post etmez. Yani udp_server_process
  // sonunda bu satırda sonsuza kadar uyur, transfer_completed = true olsa bile
  // uyanmaz.
  while (1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}