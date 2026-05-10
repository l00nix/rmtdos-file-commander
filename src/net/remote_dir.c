/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "net/remote_dir.h"

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define DIR_LIST_PAGE_ENTRIES 16
#define DIR_LIST_RETRIES 5
#define DIR_TIMEOUT_SEC 2
#define DOS_ATTR_DIRECTORY 0x10

static uint32_t make_request_id(struct RawSocket *sock) {
  return sock->session_id ^ 0x44495221;
}

static int recv_dir_page(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                         uint32_t request_id, uint16_t start_index,
                         struct RemoteDirEntry *entries, int max_entries,
                         uint16_t *count) {
  uint8_t buf[ETH_FRAME_LEN];

  for (;;) {
    fd_set fds;
    struct timeval tv;
    ssize_t received;
    const struct ether_header *eh;
    const struct ProtocolHeader *ph;
    const struct DirListData *data;
    const struct DirListEntry *wire_entries;
    uint16_t payload_len;
    uint16_t wire_count;
    uint16_t i;

    FD_ZERO(&fds);
    FD_SET(sock->sock_fd, &fds);
    tv.tv_sec = DIR_TIMEOUT_SEC;
    tv.tv_usec = 0;

    if (select(sock->sock_fd + 1, &fds, NULL, NULL, &tv) <= 0) {
      return -1;
    }

    received = recv(sock->sock_fd, buf, sizeof(buf), 0);
    if (received < (ssize_t)(sizeof(struct ether_header) +
                             sizeof(struct ProtocolHeader) +
                             sizeof(struct DirListData))) {
      continue;
    }

    eh = (const struct ether_header *)buf;
    ph = (const struct ProtocolHeader *)(eh + 1);
    data = (const struct DirListData *)(ph + 1);
    payload_len = ntohs(ph->payload_len);
    wire_count = ntohs(data->entry_count);

    if (memcmp(eh->ether_shost, dest_mac_addr, ETH_ALEN) ||
        memcmp(eh->ether_dhost, sock->if_addr, ETH_ALEN) ||
        ntohl(ph->signature) != PACKET_SIGNATURE ||
        ntohl(ph->session_id) != sock->session_id ||
        ntohs(ph->pkt_type) != V1_DIR_LIST_DATA ||
        payload_len < sizeof(*data) ||
        ntohl(data->request_id) != request_id ||
        ntohs(data->start_index) != start_index) {
      continue;
    }

    if (ntohs(data->status) != FILE_ACK_OK ||
        wire_count > DIR_LIST_PAGE_ENTRIES ||
        wire_count > max_entries ||
        payload_len < sizeof(*data) + wire_count * sizeof(*wire_entries)) {
      return -1;
    }

    wire_entries = (const struct DirListEntry *)(data + 1);
    for (i = 0; i < wire_count; ++i) {
      memset(&entries[i], 0, sizeof(entries[i]));
      snprintf(entries[i].name, sizeof(entries[i].name), "%s",
               wire_entries[i].name);
      entries[i].attributes = wire_entries[i].attributes;
      entries[i].size = ntohl(wire_entries[i].size);
      entries[i].dos_date = ntohs(wire_entries[i].dos_date);
      entries[i].dos_time = ntohs(wire_entries[i].dos_time);
      entries[i].is_dir = !!(entries[i].attributes & DOS_ATTR_DIRECTORY);
    }

    *count = wire_count;
    return 0;
  }
}

int remote_dir_fetch(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                     const char *path, struct RemoteDirEntry *entries,
                     int max_entries, int *entry_count) {
  uint32_t request_id = make_request_id(sock);
  int total = 0;
  int attempt;
  struct DirListBegin begin;
  struct DirListEnd end;

  memset(&begin, 0, sizeof(begin));
  begin.request_id = htonl(request_id);
  snprintf(begin.path, sizeof(begin.path), "%s", path);

  if (send_packet(sock, dest_mac_addr, V1_DIR_LIST_BEGIN, &begin,
                  sizeof(begin)) < 0) {
    return -1;
  }

  while (total < max_entries) {
    struct DirListDataReq req;
    uint16_t page_count = 0;
    int ok = 0;

    req.request_id = htonl(request_id);
    req.start_index = htons((uint16_t)total);
    req.max_entries = htons(DIR_LIST_PAGE_ENTRIES);

    for (attempt = 0; attempt < DIR_LIST_RETRIES; ++attempt) {
      if (send_packet(sock, dest_mac_addr, V1_DIR_LIST_DATA_REQ, &req,
                      sizeof(req)) < 0) {
        return -1;
      }

      if (!recv_dir_page(sock, dest_mac_addr, request_id, (uint16_t)total,
                         entries + total, max_entries - total, &page_count)) {
        ok = 1;
        break;
      }
    }

    if (!ok) {
      return -1;
    }

    total += page_count;
    if (page_count < DIR_LIST_PAGE_ENTRIES) {
      break;
    }
  }

  memset(&end, 0, sizeof(end));
  end.request_id = htonl(request_id);
  send_packet(sock, dest_mac_addr, V1_DIR_LIST_END, &end, sizeof(end));

  *entry_count = total;
  return 0;
}

