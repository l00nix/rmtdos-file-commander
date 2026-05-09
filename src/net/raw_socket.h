/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RMTDOS_FILE_COMMANDER_NET_RAW_SOCKET_H
#define RMTDOS_FILE_COMMANDER_NET_RAW_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include "common/protocol.h"

struct RawSocket {
  int sock_fd;
  int if_index;
  uint16_t ethertype;
  uint32_t session_id;
  const char *if_name;
  uint8_t if_addr[ETH_ALEN];
};

uint32_t create_session_id(void);
int create_socket(struct RawSocket *result, const char *if_name,
                  uint16_t ethertype);
void close_socket(struct RawSocket *sock);
int send_packet(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                enum PKT_TYPE pkt_type, const void *payload,
                size_t payload_len);
int send_status_req(struct RawSocket *sock, const uint8_t *dest_mac_addr);
int send_session_start(struct RawSocket *sock, const uint8_t *dest_mac_addr);

#endif

