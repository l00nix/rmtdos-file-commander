/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/protocol.h"
#include "ui/commander.h"

static void usage(FILE *out, const char *argv0) {
  fprintf(out, "Usage: %s -i IFACE [-e ETHERTYPE]\n", argv0);
  fprintf(out, "\n");
  fprintf(out, "Options:\n");
  fprintf(out, "  -i IFACE      Linux network interface, e.g. enp2s0\n");
  fprintf(out, "  -e ETHERTYPE  Hex EtherType, default %04x\n",
          RMTDOS_DEFAULT_ETHERTYPE);
  fprintf(out, "  -h            Show this help\n");
}

static int parse_ethertype(const char *text, uint16_t *out) {
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 16);
  if (errno || !end || *end || value > 0xffff) {
    return -1;
  }

  *out = (uint16_t)value;
  return 0;
}

int main(int argc, char **argv) {
  struct CommanderConfig config = {
      .if_name = NULL,
      .ethertype = RMTDOS_DEFAULT_ETHERTYPE,
  };
  int opt;

  while ((opt = getopt(argc, argv, "hi:e:")) != -1) {
    switch (opt) {
      case 'i':
        config.if_name = optarg;
        break;
      case 'e':
        if (parse_ethertype(optarg, &config.ethertype)) {
          fprintf(stderr, "invalid EtherType: %s\n", optarg);
          return 2;
        }
        break;
      case 'h':
        usage(stdout, argv[0]);
        return 0;
      default:
        usage(stderr, argv[0]);
        return 2;
    }
  }

  if (!config.if_name) {
    usage(stderr, argv[0]);
    return 2;
  }

  return commander_run(&config);
}

