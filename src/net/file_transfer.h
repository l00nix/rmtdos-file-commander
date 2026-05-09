/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RMTDOS_FILE_COMMANDER_NET_FILE_TRANSFER_H
#define RMTDOS_FILE_COMMANDER_NET_FILE_TRANSFER_H

#include <stdint.h>

#include "net/raw_socket.h"

int file_transfer_put(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                      const char *local_path, const char *remote_path);
int file_transfer_get(struct RawSocket *sock, const uint8_t *dest_mac_addr,
                      const char *remote_path, const char *local_path);

#endif

