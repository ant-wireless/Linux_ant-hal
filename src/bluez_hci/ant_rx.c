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
*   FILE NAME:      ant_rx.c
*
*   BRIEF:
*      This file Implements the receive thread for an HCI implementation
*      using Vendor Specific messages.
*
*
\******************************************************************************/


#define _GNU_SOURCE /* needed for PTHREAD_MUTEX_RECURSIVE */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include "antradio_power.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "ant_rx.h"
#include "ant_hciutils.h"
#include "ant_types.h"
#include "ant_framing.h"
#include "ant_log.h"
#undef LOG_TAG
#define LOG_TAG "antradio_rx"

static char EVT_PKT_VENDOR_FILTER[] = {0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                                       0x00,0x00,0x00,0x80,0x00,0x05,0x00,0x00};

/* Global Options */
ANTHCIRxParams RxParams = {
   .pfRxCallback = NULL,
   .pfStateCallback = NULL,
   .thread = 0
};

extern pthread_mutex_t enableLock;
extern ANTRadioEnabledStatus get_and_set_radio_status(void);
/*
 * This thread opens a Bluez HCI socket and waits for ANT messages.
 */
void *ANTHCIRxThread(void *pvHCIDevice)
{
   int ret = ANT_STATUS_SUCCESS;
   int rxSocket;
   int len;
   unsigned char buf[HCI_MAX_EVENT_SIZE];
   int result;
   ANT_FUNC_START();

   (void)pvHCIDevice; //unused waring

   ANT_DEBUG_D("Entering ANTHCIRxThread");

   rxSocket = create_hci_sock();
   if (rxSocket < 0)
   {
      ANT_DEBUG_E("can't open HCI socket in rx thread: %s", strerror(errno));

      ret = ANT_STATUS_FAILED;
      goto out;
   }

   if (setsockopt(rxSocket, SOL_HCI, HCI_FILTER, &EVT_PKT_VENDOR_FILTER,
                                             sizeof(EVT_PKT_VENDOR_FILTER)) < 0)
   {
      ANT_ERROR("failed to set socket options: %s", strerror(errno));

      ret = ANT_STATUS_FAILED;
      goto close;
   }

   /* continue running as long as not terminated */
   while (get_and_set_radio_status() == RADIO_STATUS_ENABLED)
   {
      struct pollfd p;
      int n;

      p.fd = rxSocket;
      p.events = POLLIN;

      ANT_DEBUG_V("    RX: Polling HCI for data...");

      /* poll socket, wait for ANT messages */
      while ((n = poll(&p, 1, 2500)) == -1)
      {
         if (errno == EAGAIN || errno == EINTR)
            continue;

         ANT_ERROR("failed to poll socket: %s", strerror(errno));

         ret = ANT_STATUS_FAILED;
         goto close;
      }

      /* we timeout once in a while */
      /* this let's us the chance to check if we were terminated */
      if (0 == n)
      {
         ANT_DEBUG_V("    RX: Timeout");
         continue;
      }

      ANT_DEBUG_D("New HCI data available, reading...");

      /* read newly arrived data */
      /* TBD: rethink assumption about single arrival */
      while ((len = read(rxSocket, buf, sizeof(buf))) < 0)
      {
         if (errno == EAGAIN || errno == EINTR)
            continue;

         ANT_ERROR("failed to read socket: %s", strerror(errno));

         ret = ANT_STATUS_FAILED;
         goto close;
      }

    // 0 = packet type eg. HCI_EVENT_PKT
    // FOR EVENT:
    //   1 = event code  eg. EVT_VENDOR, EVT_CMD_COMPLETE
    //   2 = Parameter total length
    //   3... parameters
    //  FOR CC
    //      3   = Num HCI Command packets allowed to be sent
    //      4+5 = ANT Opcode
    //      6   = Result ??
    //   FOR VS
    //     3+4 = ANT Opcode
    //     5   = length
    //     6 ? MSB of length ?
    //     7... ant message

      if (len >= 7)
      {
          ANT_DEBUG_V("HCI Data: [%02X][%02X][%02X][%02X][%02X][%02X][%02X]...",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
      }
      else
      {
         ANT_ERROR("Failed to read full header off socket. len = %d", len);
         continue;
      }

      if (len != (buf[2] + 3))
      {
         ANT_WARN("HCI packet length(%d) and bytes read(%d) dont match", buf[2] + 3, len);
      }

      if(HCI_EVENT_PKT == buf[0])
      {
         ANT_DEBUG_D("Received Event Packet");

         if (EVT_VENDOR == buf[1])
         {
            if ((HCI_VSOP_ANT_LSB == buf[3]) && (HCI_VSOP_ANT_MSB == buf[4]))
            {
               ANT_DEBUG_D("Received ANT VS Message");

               if (len < 9)
               {
                  ANT_ERROR("Malformed ANT header");
                  ret = ANT_STATUS_FAILED;
                  goto close;
               }

               ANT_DEBUG_V("ANT Mesg: ANTMesgSize:%d ANTMesgID:0x%02X ...",
                                                               buf[7], buf[8]);

               ANT_SERIAL(&(buf[7]), buf[5], 'R');

               if(RxParams.pfRxCallback != NULL)
               {
                  RxParams.pfRxCallback(buf[5], &(buf[7]));
               }
               else
               {
                  ANT_ERROR("Can't send rx message - no callback registered");
               }

               continue;
            }
            else
            {
               ANT_DEBUG_W("Vendor Specific message for another vendor. "
                                                         "Should filter out");
            }
         }
         else
         {
            ANT_DEBUG_V("Other Event Packet, Ignoring");
         }
      }
      else
      {
         ANT_DEBUG_V("Non-Event Packet, Ignoring");
      }
   }

close:
   result = pthread_mutex_trylock(&enableLock);
   ANT_DEBUG_D("rx thread close: trylock enableLock returned %d", result);

   if (result == 0)
   {
      ANT_DEBUG_W("rx thread socket has unexpectedly crashed");
      if (RxParams.pfStateCallback)
         RxParams.pfStateCallback(RADIO_STATUS_DISABLING);
      ant_disable();
      get_and_set_radio_status();
      RxParams.thread = 0;
      pthread_mutex_unlock(&enableLock);
   }
   else if (result == EBUSY)
   {
      ANT_DEBUG_V("rx thread socket was closed");
   }
   else
   {
      ANT_ERROR("rx thread close: trylock failed: %s", strerror(result));
   }

   if (-1 == close(rxSocket))
   {
      ANT_ERROR("failed to close hci device (socket handle=%#x): %s", rxSocket, strerror(errno));
   }
   else
   {
      ANT_DEBUG_D("closed hci device (socket handle=%#x)", rxSocket);
   }

out:
   ANT_FUNC_END();

   pthread_exit((void *)ret);

#if defined(ANDROID)
   return 0;
#endif
}

