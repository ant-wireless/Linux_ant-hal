/*
 * ANT Stack
 *
 * Copyright 2009 Dynastream Innovations
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/******************************************************************************\
*
*   FILE NAME:      ant_hciuntils.h
*
*   BRIEF:
*		This file defines the utility functions for an HCI implementation
*
*
\******************************************************************************/

#ifndef __ANT_HCIUTILS_H
#define __ANT_HCIUTILS_H

#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#include "ant_types.h"
#include "ant_native.h"
#include "ant_log.h"
// -------------------------------------------------

#define HCI_COMMAND_HEADER_SIZE 3
typedef struct {
   ANT_U8                     opcode[2];           // 0xFDD1 for ANT
   ANT_U8                     plen;
} __attribute__ ((packed)) hci_command_header_t;

#define HCI_VENDOR_HEADER_SIZE 2
typedef struct {
   ANT_U8                  hcilen[2];
} __attribute__ ((packed)) hci_vendor_header_t;

#define HCI_COMMAND_OVERHEAD_SIZE ( HCI_COMMAND_HEADER_SIZE + \
                                    HCI_VENDOR_HEADER_SIZE + 1)
typedef struct {
   ANT_U8                     packet_type;         // 0x01 for HCI_COMMAND_PKT
   hci_command_header_t       cmd_header;
   hci_vendor_header_t        vendor_header;
} __attribute__ ((packed)) hci_vendor_cmd_packet_t;

#define HCI_EVENT_HEADER_SIZE 2
typedef struct {
   ANT_U8                     event_c;             // 0xFF for Vendor Specific,
                                                   // 0x0E for EVNT_CMD_COMPLETE
   ANT_U8                     plen;                // data/parameter length
} __attribute__ ((packed)) hci_event_header_t;

#define HCI_EVENT_OVERHEAD_SIZE (HCI_EVENT_HEADER_SIZE + 5)
typedef struct {
   ANT_U8                     packet_type;         // HCI_EVENT_PKT
   hci_event_header_t         header;
   union {
      struct {
         ANT_U8               vendor_c[2];         // 0x0500 for ANT
         ANT_U8               hcilen[2];
      } vendor;
      struct {
         ANT_U8               num_token;
         ANT_U8               opcode[2];           // 0xFDD1 for ANT
         ANT_U8               resp;
      } cmd_cmplt;
   };
   ANT_U8                     data[ANT_NATIVE_MAX_PARMS_LEN];  // Should be 255
} __attribute__ ((packed)) hci_event_packet_t;

typedef struct {
   union {
      hci_event_packet_t      hci_event_packet;
      ANT_U8                  raw_packet[sizeof(hci_event_packet_t)];
   };
} rx_data_t;  // Use this is for vendor specific and command complete events


// -------------------------------------------------

static inline int create_hci_sock() {
   struct sockaddr_hci sa;
   int sk = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
   if (sk < 0)
   {
      ANT_ERROR("Failed to create bluetooth hci socket: %s", strerror(errno));
   }
   else
   {
      memset(&sa, 0, sizeof(sa));
      // sockaddr_hci changed from kernel 2.6.37 to 2.6.38 adding member hci_channel
      // In order to be backwards compatible set it to 0 if it exists (HCI_CHANNEL_RAW)
      sa.hci_family = AF_BLUETOOTH;
      sa.hci_dev = 0;
      if (bind(sk, (struct sockaddr *) &sa, sizeof(sa)) < 0)
      {
         ANT_ERROR("Failed to bind socket %#x to hci0: %s", sk, strerror(errno));
         close(sk);
         sk = -1;
      }
   }
   return sk;
}

#endif /* __ANT_HCIUTILS_H */


