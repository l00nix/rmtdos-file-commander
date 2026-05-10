/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RMTDOS_FILE_COMMANDER_NET_REMOTE_DIR_H
#define RMTDOS_FILE_COMMANDER_NET_REMOTE_DIR_H

#include <stdint.h>

#include "common/protocol.h"
#include "net/raw_socket.h"

#define REMOTE_DIR_MAX_ENTRIES 256

struct RemoteDirEntry {
  char name[RMTDOS_DIR_ENTRY_NAME_BYTES];
  uint8_t attributes;
  uint32_t size;
  uint16_t dos_date;
  uint16_t dos_time;
  int is_dir;
};

int remote_dir_fetch(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                     const char *path, struct RemoteDirEntry *entries,
                     int max_entries, int *entry_count);

#endif

