/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "net/raw_socket.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

uint32_t create_session_id(void) {
  uint32_t session_id = 0;
  if (getrandom(&session_id, sizeof(session_id), 0) != sizeof(session_id)) {
    session_id = (uint32_t)getpid();
  }
  return session_id;
}

int create_socket(struct RawSocket *result, const char *if_name,
                  uint16_t ethertype) {
  int r;
  struct ifreq ifr;

  memset(result, 0, sizeof(*result));
  result->sock_fd = -1;
  result->ethertype = ethertype;
  result->session_id = create_session_id();
  result->if_name = if_name;

  r = socket(AF_PACKET, SOCK_RAW, htons(ethertype));
  if (r < 0) {
    perror("socket");
    goto fail;
  }
  result->sock_fd = r;

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);

  if ((r = ioctl(result->sock_fd, SIOCGIFINDEX, &ifr)) < 0) {
    perror("SIOCGIFINDEX");
    goto fail;
  }
  result->if_index = ifr.ifr_ifindex;

  if ((r = ioctl(result->sock_fd, SIOCGIFHWADDR, &ifr)) < 0) {
    perror("SIOCGIFHWADDR");
    goto fail;
  }
  memcpy(result->if_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

  if ((r = setsockopt(result->sock_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name,
                      IFNAMSIZ - 1)) < 0) {
    perror("SO_BINDTODEVICE");
    goto fail;
  }

  return result->sock_fd;

fail:
  close_socket(result);
  return r;
}

void close_socket(struct RawSocket *sock) {
  if (sock->sock_fd >= 0) {
    close(sock->sock_fd);
    sock->sock_fd = -1;
  }
}

int send_packet(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                enum PKT_TYPE pkt_type, const void *payload,
                size_t payload_len) {
  static const uint8_t broadcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff,
                                                   0xff, 0xff, 0xff};
  const uint8_t *dest = dest_mac_addr ? dest_mac_addr : broadcast_addr;
  const size_t send_len =
      sizeof(struct ether_header) + sizeof(struct ProtocolHeader) + payload_len;
  uint8_t *buffer;
  struct ether_header *eh;
  struct ProtocolHeader *ph;
  struct sockaddr_ll sock_addr;
  int r;

  assert(sock);
  assert(payload_len <= MAX_PAYLOAD_LENGTH);

  buffer = malloc(send_len);
  if (!buffer) {
    return -1;
  }

  eh = (struct ether_header *)buffer;
  memcpy(eh->ether_shost, sock->if_addr, ETH_ALEN);
  memcpy(eh->ether_dhost, dest, ETH_ALEN);
  eh->ether_type = htons(sock->ethertype);

  ph = (struct ProtocolHeader *)(eh + 1);
  ph->signature = htonl(PACKET_SIGNATURE);
  ph->session_id = htonl(sock->session_id);
  ph->payload_len = htons(payload_len);
  ph->pkt_type = htons(pkt_type);

  if (payload) {
    memcpy(ph + 1, payload, payload_len);
  }

  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sll_ifindex = sock->if_index;
  sock_addr.sll_halen = ETH_ALEN;
  memcpy(sock_addr.sll_addr, dest, ETH_ALEN);

  r = sendto(sock->sock_fd, buffer, send_len, 0,
             (struct sockaddr *)&sock_addr, sizeof(sock_addr));
  free(buffer);

  if (r < 0) {
    perror("sendto");
  }

  return r;
}

int send_status_req(struct RawSocket *sock, const uint8_t *dest_mac_addr) {
  return send_packet(sock, dest_mac_addr, V1_STATUS_REQ, NULL, 0);
}

int send_session_start(struct RawSocket *sock, const uint8_t *dest_mac_addr) {
  return send_packet(sock, dest_mac_addr, V1_SESSION_START, NULL, 0);
}

