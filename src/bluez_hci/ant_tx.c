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
*   FILE NAME:      ant_tx.c
*
*   BRIEF:
*      This file Implements the transmit functionality for an HCI implementation
*      using Vendor Specific messages.
*
*
\******************************************************************************/

#include <errno.h>
#include <poll.h>
#include <sys/uio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "ant_types.h"
#include "ant_hciutils.h"
#include "ant_framing.h"
#include "ant_utils.h"
#include "ant_log.h"
#undef LOG_TAG
#define LOG_TAG "antradio_tx"

static char EVT_PKT_CMD_COMPLETE_FILTER[] = {0x10,0x00,0x00,0x00,0x00,0x40,0x00,
                                 0x00,0x00,0x00,0x00,0x00,0xD1,0xFD,0x00,0x00};

int g_ant_cmd_socket = -1;

int ant_open_tx_transport(void)
{
   int socket = -1;
   ANT_FUNC_START();

   socket = create_hci_sock();
   
   if (socket < 0) 
   {
      ANT_DEBUG_E("failed to open HCI socket for tx: %s", strerror(errno));
   }
   else
   {
      g_ant_cmd_socket = socket;
      ANT_DEBUG_D("socket handle %#x", g_ant_cmd_socket);
      if (setsockopt(g_ant_cmd_socket, SOL_HCI, HCI_FILTER, 
         &EVT_PKT_CMD_COMPLETE_FILTER, sizeof(EVT_PKT_CMD_COMPLETE_FILTER)) < 0)
      {
         ANT_ERROR("failed to set socket options: %s", strerror(errno));
         close(socket);
         socket = -1;
      }
   }

   ANT_FUNC_END();
   return socket;
}

void ant_close_tx_transport(int socket)
{
   ANT_FUNC_START();

   if(0 < socket)
   {
      if (0 == close(socket)) 
      {
         ANT_DEBUG_D("closed hci device (socket handle=%#x)", socket);
      }
      else
      {
         ANT_ERROR("failed to close hci device (socket handle=%#x): %s", socket, strerror(errno));
      }
   }
   else
   {
      ANT_DEBUG_E("requested close on socket %#x. invalid param", socket);
   }

   ANT_FUNC_END();
}

/* 
Format of an HCI WRITE command to ANT chip:

HCI Header:
----------
- HCI Packet Type:                                                      1 byte

- HCI Opcode:                                                           2 bytes
                                                               (LSB, MSB - LE)
- HCI Parameters Total Len (total length of all subsequent fields):     1 byte

HCI Parameters:
--------------
- VS Parameters Len (total length of ANT Mesg inculding Len/ID)         2 bytes
                                                               (LSB, MSB - LE)
- ANT Mesg Len (N = number of bytes in ANT Mesg Data):                  1 byte
- ANT Mesg ID:                                                          1 byte
- ANT Mesg Data:                                                        N bytes 
*/

ANT_BOOL wait_for_message(int socket)
{
   struct pollfd p;
   int n;

   ANT_BOOL bReturn = ANT_FALSE;
   ANT_BOOL bRetry = ANT_FALSE;

   ANT_FUNC_START();

   p.fd = socket;
   p.events = POLLIN;

   do
   {
      bRetry = ANT_FALSE;
   
      ANT_DEBUG_V("    CC: Polling HCI for data...");
   
      /* poll socket, wait for ANT messages */
      n = poll(&p, 1, 2500);
      if (0 > n)
      {
         if (errno == EAGAIN || errno == EINTR)
         {
            ANT_DEBUG_W("    CC: error: %s", strerror(errno));
            bRetry = ANT_TRUE;
         }
         else
         {
            ANT_ERROR("failed to poll socket. error: %s", strerror(errno));
         }
      }

      /* timeout */
      else if (0 == n)
      {
         ANT_ERROR("SERIOUS: Timeouted getting Command Complete");
      }
      else if(0 < n)
      {
         // There is data to read.
         bReturn = ANT_TRUE;
      }

   } while(ANT_TRUE == bRetry);

   ANT_FUNC_END();
   
   return bReturn;
}

ANTStatus write_data(ANT_U8 ant_message[], int ant_message_len)
{
   ANTStatus ret = ANT_STATUS_FAILED;
   ANT_BOOL retry = ANT_FALSE;
   int bytes_written;
   struct iovec iov[2];
   hci_vendor_cmd_packet_t hci_header;
   ANT_U16 hci_opcode;

   ANT_FUNC_START();

   hci_opcode = cmd_opcode_pack(OGF_VENDOR_CMD, HCI_CMD_ANT_MESSAGE_WRITE);

   hci_header.packet_type = HCI_COMMAND_PKT;
   ANT_UTILS_StoreLE16(hci_header.cmd_header.opcode, hci_opcode);
   hci_header.cmd_header.plen = ((ant_message_len + HCI_VENDOR_HEADER_SIZE) & 0xFF);
   ANT_UTILS_StoreLE16(hci_header.vendor_header.hcilen, ant_message_len);

   iov[0].iov_base = &hci_header;
   iov[0].iov_len = sizeof(hci_header);
   iov[1].iov_base = ant_message;
   iov[1].iov_len = ant_message_len;

   do //while retry
   {
      retry = ANT_FALSE;

      if (g_ant_cmd_socket < 0)
      {
         ANT_DEBUG_E("bad socket handle %#x", g_ant_cmd_socket);
         return ANT_STATUS_INTERNAL_ERROR;
      }

      ANT_SERIAL(ant_message, ant_message_len, 'T');

      bytes_written = writev(g_ant_cmd_socket, iov, 2);

      ANT_DEBUG_D("writing to socket %#x returned %d", g_ant_cmd_socket, 
                                                               bytes_written);

//      bytes_written < 0                   = error (check errno)
//      bytes_written = 0                   = No data written
//      bytes_written < sizeof(hci_message) = not all data written
//      bytes_written = sizeof(hci_message) = all data written

      if(bytes_written < 0)
      {
         ANT_ERROR("write to HCI failed: %s", strerror(errno));

         if (errno == EAGAIN || errno == EINTR)
         {
            ANT_DEBUG_D("Retrying write to HCI");
            retry = ANT_TRUE;
         }
         else
         {
            ret = ANT_STATUS_FAILED;
         }
      }
      else if(bytes_written < ((int)sizeof(hci_header) + ant_message_len))
      {
         ANT_DEBUG_D("Only %d bytes written to HCI.", bytes_written);
         ret = ANT_STATUS_FAILED;
      }
      else
      {
         ANT_DEBUG_V("writev successful");
         ret = ANT_STATUS_SUCCESS;
      }
   } while(retry);

   ANT_FUNC_END();
 
   return ret;
}

// Returns:
//  ANT_STATUS_NO_VALUE_AVAILABLE          if not a CC packet
//  ANT_STATUS_FAILED                      if could not read socket or not a 
//                                                       valid length CC packet
//  ANT_STATUS_TRANSPORT_UNSPECIFIED_ERROR if CC indicates an unspecified error
//  ANT_STATUS_COMMAND_WRITE_FAILED        if CC indicates a failure
//  ANT_STATUS_SUCCESS                     if CC indicates message was received 
ANTStatus get_command_complete_result(int socket)
{
   ANTStatus status = ANT_STATUS_NO_VALUE_AVAILABLE;
   int len;
   ANTStatus ret = ANT_STATUS_SUCCESS;
   ANT_U8 ucResult = -1;
   ANT_U8 buf[ANT_NATIVE_MAX_PARMS_LEN];

   ANT_FUNC_START();
   ANT_DEBUG_V("reading off socket %#x", socket);

   /* read newly arrived data */
   while ((len = read(socket, buf, sizeof(buf))) < 0)
   {
      if (errno == EAGAIN || errno == EINTR)
         continue;

      ANT_ERROR("failed to read socket. error: %s", strerror(errno));

      ret = ANT_STATUS_FAILED;
      goto close;
   }

   ANT_SERIAL(buf, len, 'C');

   ANT_DEBUG_V("HCI Data: [%02X][%02X][%02X][%02X][%02X][%02X][%02X]...", 
              buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

   // 0 = packet type eg. HCI_EVENT_PKT
   // FOR EVENT:
   //   1 = event code  eg. EVT_VENDOR, EVT_CMD_COMPLETE
   //   2 = Parameter total length
   //   3... parameters
   //  FOR CC
   //      3   = Num HCI Vommand packets (allowed to be sent)
   //      4+5 = ANT Opcode
   //      6   = Result ??
   //   FOR VS
   //     3+4 = ANT Opcode
   //     5   = length
   //     6 ? MSB of length ?
   //     7... ant message

   if(HCI_EVENT_PKT == buf[0])
   {
      ANT_DEBUG_D("Received Event Packet");

      if(EVT_CMD_COMPLETE == buf[1])
      {
         if(len < HCI_EVENT_OVERHEAD_SIZE) 
         {
            status = ANT_STATUS_FAILED;
         }
         else
         {
            if((HCI_CMD_OPCODE_ANT_LSB == buf[4]) && 
                                         (HCI_CMD_OPCODE_ANT_MSB == buf[5]))
            {
               ucResult = buf[6];

               ANT_DEBUG_V("Received COMMAND COMPLETE");
            }
            else
            {
               ANT_DEBUG_V("Command complete has wrong opcode");
            }
         }

         /*
          * if got a status byte, pass it forward, otherwise pass a failure
          * status
          */
         if(status != ANT_STATUS_FAILED)
         {
            if(ucResult == 0)
            {
               ANT_DEBUG_D("Command Complete = SUCCESS");
               status = ANT_STATUS_SUCCESS;
            }
            else if(ucResult == HCI_UNSPECIFIED_ERROR)
            {
               ANT_DEBUG_D("Command Complete = UNSPECIFIED_ERROR");

               status = ANT_STATUS_TRANSPORT_UNSPECIFIED_ERROR;
            }
            else
            {
               status = ANT_STATUS_COMMAND_WRITE_FAILED;
               ANT_DEBUG_D("Command Complete = WRITE_FAILED");
            }
         }
      }
      else
      {
         ANT_DEBUG_W("Other Event Packet, Should filter out");
      }
   }

close:
   ANT_FUNC_END();
   return status;
}

