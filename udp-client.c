#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/netstack.h"
#include "net/routing/routing.h"
#include "random.h"
#include "sys/node-id.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Ürettiğimiz küçük imaj dizisi
#include "firmware_data.h"

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define SEND_INTERVAL (2 * CLOCK_SECOND)
#define BLOCK_SIZE 64

#define TARGET_FIRMWARE_SIZE 16384
static struct simple_udp_connection udp_conn;
static bool end_packet_sent = false;
static uint16_t current_block = 0;
static bool ack_received = true;
static bool transfer_completed = false;

typedef struct {
  uint16_t block_no;
  uint8_t data_len;
  uint16_t checksum;
  uint8_t payload[BLOCK_SIZE];
} __attribute__((packed)) firmware_packet_t;

static firmware_packet_t pkt;

uint16_t calculate_checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  uint8_t i;
  for (i = 0; i < len; i++)
    sum += data[i];
  return sum;
}

uint32_t calculate_full_crc(const uint8_t *data, uint32_t len) {
  uint32_t crc = 0;
  uint32_t i;
  for (i = 0; i < len; i++) {
    crc += data[i];
  }
  return crc;
}

PROCESS(udp_client_process, "UDP client (Firmware Sender)");
AUTOSTART_PROCESSES(&udp_client_process);

static void udp_rx_callback(struct simple_udp_connection *c,
                            const uip_ipaddr_t *sender_addr,
                            uint16_t sender_port,
                            const uip_ipaddr_t *receiver_addr,
                            uint16_t receiver_port, const uint8_t *data,
                            uint16_t datalen) {
  if (datalen == sizeof(uint16_t)) {
    uint16_t ack_block;
    memcpy(&ack_block, data, sizeof(uint16_t));
    if (ack_block == current_block) {
      current_block++;
      ack_received = true;
    }
  }
}

PROCESS_THREAD(udp_client_process, ev, data) {
  static struct etimer periodic_timer;
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT,
                      udp_rx_callback);

  etimer_set(&periodic_timer,
             (5 * CLOCK_SECOND) + (random_rand() % SEND_INTERVAL));

  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if (NETSTACK_ROUTING.node_is_reachable() &&
        NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      if (node_id == 2 && !transfer_completed) {

        uint32_t offset = (uint32_t)current_block * BLOCK_SIZE;

        // Hocanın istediği hedefe vardık mı?
        if (ack_received && offset >= TARGET_FIRMWARE_SIZE) {
          if (!end_packet_sent) {
            uint32_t final_crc =
                calculate_full_crc(firmware_data, TARGET_FIRMWARE_SIZE);
            firmware_packet_t end_pkt;
            memset(&end_pkt, 0, sizeof(end_pkt));
            end_pkt.block_no = 0xFFFF;
            memcpy(end_pkt.payload, &final_crc, sizeof(uint32_t));
            simple_udp_sendto(&udp_conn, &end_pkt, sizeof(firmware_packet_t),
                              &dest_ipaddr);
            LOG_INFO("Son CRC32 gonderildi: %lu\n", (unsigned long)final_crc);
            end_packet_sent = true;
          }
          transfer_completed = true;
          continue;
        }

        pkt.block_no = current_block;
        uint32_t remaining = TARGET_FIRMWARE_SIZE - offset;
        pkt.data_len = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;

        for (uint8_t i = 0; i < pkt.data_len; i++) {
          pkt.payload[i] = firmware_data[offset + i];
        }

        pkt.checksum = calculate_checksum(pkt.payload, pkt.data_len);

        if (ack_received) {
          LOG_INFO("Blok %d gonderiliyor... (%d bayt)\n", pkt.block_no,
                   pkt.data_len);
        }

        simple_udp_sendto(&udp_conn, &pkt, sizeof(firmware_packet_t),
                          &dest_ipaddr);
        ack_received = false;
        etimer_set(&periodic_timer, SEND_INTERVAL);
      }
    } else {
      etimer_set(&periodic_timer, SEND_INTERVAL);
    }
  }

  PROCESS_END();
}