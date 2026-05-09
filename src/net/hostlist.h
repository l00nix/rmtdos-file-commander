/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RMTDOS_FILE_COMMANDER_NET_HOSTLIST_H
#define RMTDOS_FILE_COMMANDER_NET_HOSTLIST_H

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#include "common/protocol.h"

#define RMTDOS_MAX_HOSTS 16
#define MAC_ADDR_FMT_LEN 18

struct RemoteHost {
  int index;
  uint8_t if_addr[ETH_ALEN];
  struct timeval tv_last_resp;
  struct timeval tv_last_session_start;
  struct StatusResponse status;
};

void hostlist_create(void);
void hostlist_destroy(void);
struct RemoteHost *hostlist_find_by_mac(const uint8_t *if_addr);
struct RemoteHost *hostlist_find_by_index(int index);
struct RemoteHost *hostlist_iter(int *iter);
void hostlist_register(const uint8_t *packet, size_t length);
const char *fmt_mac_addr(char *buf, size_t len, const uint8_t *mac);

#endif

