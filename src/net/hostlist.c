/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "net/hostlist.h"

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct RemoteHost *g_remote_hosts[RMTDOS_MAX_HOSTS] = {NULL};

static struct RemoteHost *host_create(void) {
  struct RemoteHost *rh = malloc(sizeof(*rh));
  if (rh) {
    memset(rh, 0, sizeof(*rh));
  }
  return rh;
}

void hostlist_create(void) {}

void hostlist_destroy(void) {
  int i;
  for (i = 0; i < RMTDOS_MAX_HOSTS; ++i) {
    free(g_remote_hosts[i]);
    g_remote_hosts[i] = NULL;
  }
}

struct RemoteHost *hostlist_find_by_mac(const uint8_t *if_addr) {
  int i;
  for (i = 0; i < RMTDOS_MAX_HOSTS; ++i) {
    if (g_remote_hosts[i] &&
        !memcmp(g_remote_hosts[i]->if_addr, if_addr, ETH_ALEN)) {
      return g_remote_hosts[i];
    }
  }
  return NULL;
}

struct RemoteHost *hostlist_find_by_index(int index) {
  if (index >= 0 && index < RMTDOS_MAX_HOSTS) {
    return g_remote_hosts[index];
  }
  return NULL;
}

static struct RemoteHost *hostlist_allocate(void) {
  int i;
  for (i = 0; i < RMTDOS_MAX_HOSTS; ++i) {
    if (!g_remote_hosts[i]) {
      g_remote_hosts[i] = host_create();
      if (g_remote_hosts[i]) {
        g_remote_hosts[i]->index = i;
      }
      return g_remote_hosts[i];
    }
  }
  return NULL;
}

struct RemoteHost *hostlist_iter(int *iter) {
  while (*iter < RMTDOS_MAX_HOSTS) {
    if (g_remote_hosts[*iter]) {
      struct RemoteHost *rh = g_remote_hosts[*iter];
      ++(*iter);
      return rh;
    }
    ++(*iter);
  }

  *iter = -1;
  return NULL;
}

void hostlist_register(const uint8_t *packet, size_t length) {
  const struct ether_header *eh;
  const struct ProtocolHeader *ph;
  struct RemoteHost *rh;

  if (length < sizeof(*eh) + sizeof(*ph)) {
    return;
  }

  eh = (const struct ether_header *)packet;
  ph = (const struct ProtocolHeader *)(eh + 1);

  rh = hostlist_find_by_mac(eh->ether_shost);
  if (!rh) {
    rh = hostlist_allocate();
    if (!rh) {
      return;
    }
    memcpy(rh->if_addr, eh->ether_shost, ETH_ALEN);
  }

  gettimeofday(&rh->tv_last_resp, NULL);

  if (ntohs(ph->pkt_type) == V1_STATUS_RESP &&
      ntohs(ph->payload_len) >= sizeof(struct StatusResponse)) {
    const struct StatusResponse *s = (const struct StatusResponse *)(ph + 1);
    rh->status = *s;
  }
}

const char *fmt_mac_addr(char *buf, size_t len, const uint8_t *mac) {
  snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

