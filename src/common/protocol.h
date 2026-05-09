/*
 * Portions derived from rmtdos-cga-web.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RMTDOS_FILE_COMMANDER_COMMON_PROTOCOL_H
#define RMTDOS_FILE_COMMANDER_COMMON_PROTOCOL_H

#include <stdint.h>
#include <net/ethernet.h>

#include "common/ethernet.h"

#define RMTDOS_FC_VERSION "rmtdos-file-commander v0.1.0"
#define RMTDOS_DEFAULT_ETHERTYPE 0x80ab

#define PACKET_SIGNATURE ((uint32_t)0x7b6e05b0)

enum PKT_TYPE {
  V1_NOOP = 0,
  V1_PING = 1,
  V1_PONG = 2,
  V1_STATUS_REQ = 3,
  V1_STATUS_RESP = 4,
  V1_SESSION_START = 5,
  V1_VGA_TEXT = 6,
  V1_INJECT_KEYSTROKE = 7,
  V1_CGA_GRAPHICS = 8,
  V1_FILE_PUT_BEGIN = 9,
  V1_FILE_PUT_DATA = 10,
  V1_FILE_PUT_END = 11,
  V1_FILE_ACK = 12,
  V1_FILE_GET_BEGIN = 13,
  V1_FILE_GET_DATA_REQ = 14,
  V1_FILE_GET_DATA = 15,
  V1_FILE_GET_END = 16,

  /* Planned next TSR extensions. */
  V1_DIR_LIST_BEGIN = 17,
  V1_DIR_LIST_DATA_REQ = 18,
  V1_DIR_LIST_DATA = 19,
  V1_DIR_LIST_END = 20,
  V1_CHDIR = 21,
};

#pragma pack(push, 1)

struct ProtocolHeader {
  uint32_t signature;
  uint32_t session_id;
  uint16_t payload_len;
  uint16_t pkt_type;
};

#define COMBINED_HEADER_LEN                                                    \
  (sizeof(struct ether_header) + sizeof(struct ProtocolHeader))
#define MAX_PAYLOAD_LENGTH (ETH_FRAME_LEN - COMBINED_HEADER_LEN)

struct StatusResponse {
  uint8_t video_mode;
  uint8_t active_page;
  uint8_t text_rows;
  uint8_t text_cols;
  uint8_t cursor_row;
  uint8_t cursor_col;
};

struct VideoText {
  uint8_t text_rows;
  uint8_t text_cols;
  uint8_t cursor_row;
  uint8_t cursor_col;
  uint16_t offset;
  uint16_t count;
};

#define FILE_TRANSFER_NAME_BYTES 64
#define FILE_TRANSFER_CHUNK_BYTES 512

enum FILE_ACK_COMMAND {
  FILE_ACK_BEGIN = 1,
  FILE_ACK_DATA = 2,
  FILE_ACK_END = 3,
};

enum FILE_ACK_STATUS {
  FILE_ACK_OK = 0,
  FILE_ACK_BUSY = 1,
  FILE_ACK_ERROR = 2,
};

struct FilePutBegin {
  uint32_t transfer_id;
  uint32_t size;
  char filename[FILE_TRANSFER_NAME_BYTES];
};

struct FilePutData {
  uint32_t transfer_id;
  uint32_t offset;
  uint16_t count;
};

struct FilePutEnd {
  uint32_t transfer_id;
  uint32_t size;
};

struct FileGetBegin {
  uint32_t transfer_id;
  char filename[FILE_TRANSFER_NAME_BYTES];
};

struct FileGetDataReq {
  uint32_t transfer_id;
  uint32_t offset;
  uint16_t count;
};

struct FileGetData {
  uint32_t transfer_id;
  uint32_t offset;
  uint16_t count;
  uint16_t status;
};

struct FileGetEnd {
  uint32_t transfer_id;
};

struct FileAck {
  uint32_t transfer_id;
  uint16_t command;
  uint16_t status;
  uint32_t offset;
};

#define RMTDOS_PATH_BYTES 128
#define RMTDOS_DIR_ENTRY_NAME_BYTES 64

enum RMTDOS_DIR_ENTRY_FLAGS {
  RMTDOS_DIR_ENTRY_DIRECTORY = 0x10,
};

struct DirListBegin {
  uint32_t request_id;
  char path[RMTDOS_PATH_BYTES];
};

struct DirListDataReq {
  uint32_t request_id;
  uint16_t start_index;
  uint16_t max_entries;
};

struct DirListEntry {
  uint8_t attributes;
  uint8_t reserved;
  uint32_t size;
  uint16_t dos_date;
  uint16_t dos_time;
  char name[RMTDOS_DIR_ENTRY_NAME_BYTES];
};

struct DirListData {
  uint32_t request_id;
  uint16_t start_index;
  uint16_t entry_count;
  uint16_t status;
};

struct DirListEnd {
  uint32_t request_id;
};

#pragma pack(pop)

#endif

