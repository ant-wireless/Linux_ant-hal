/*
 * ANT Stack
 *
 * Copyright 2011 Dynastream Innovations
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*******************************************************************************\
*
*   FILE NAME:     ant_rx_chardev.c
*
*   BRIEF:
*      This file provides the rx function which will loop reading ANT messages
*        until told to exit.
*
*
\*******************************************************************************/

#include <errno.h>
#include <poll.h>
#include <pthread.h>

#include "ant_rx_chardev.h"
#include "ant_native_chardev.h"

#include "ant_native.h"
#include "ant_types.h"
#include "ant_log.h"
#undef LOG_TAG
#define LOG_TAG "antradio_rx"

#define ANT_POLL_TIMEOUT         ((int)30000)

int readChannelMsg(ant_channel_info_t *pstChnlInfo)
{
   int iRet = -1;
   ANT_U8 aucRxBuffer[ANT_HCI_MAX_MSG_SIZE];
   int iRxLenRead;
   int iMutexResult;
   ANT_FUNC_START();

   while (((iRxLenRead = read(pstChnlInfo->iFd, aucRxBuffer, sizeof(aucRxBuffer))) < 0)
                   && errno == EAGAIN)
      ;

   if (iRxLenRead < 0) {
      if (errno == ENODEV) {
         ANT_ERROR("%s not enabled, exiting rx thread",
               pstChnlInfo->pcDevicePath);
         goto out;
      } else if (errno == ENXIO) {
         ANT_ERROR("%s there is no physical ANT device connected",
               pstChnlInfo->pcDevicePath);
         goto out;
      } else {
         ANT_ERROR("%s read thread exiting, unhandled error: %s",
               pstChnlInfo->pcDevicePath, strerror(errno));
         goto out;
      }
   } else {
      ANT_SERIAL(aucRxBuffer, iRxLenRead, 'R');
      if (aucRxBuffer[CHIP_C_HCI_DATA_OFFSET + ANT_MSG_ID_OFFSET] == ANT_MESG_FLOW_CONTROL) {
         ANT_DEBUG_V("getting stFlowControlLock in %s", __FUNCTION__);
         iMutexResult = pthread_mutex_lock(pstChnlInfo->pstFlowControlLock);
         if (iMutexResult) {
            ANT_ERROR("failed to lock flow control mutex during response: %s", strerror(iMutexResult));
            goto out;
         }
         ANT_DEBUG_V("got stFlowControlLock in %s", __FUNCTION__);

         pstChnlInfo->ucFlowControlResp = aucRxBuffer[CHIP_C_HCI_DATA_OFFSET + ANT_MSG_DATA_OFFSET];

         ANT_DEBUG_V("releasing stFlowControlLock in %s", __FUNCTION__);
         pthread_mutex_unlock(pstChnlInfo->pstFlowControlLock);
         ANT_DEBUG_V("released stFlowControlLock in %s", __FUNCTION__);
         pthread_cond_signal(pstChnlInfo->pstFlowControlCond);
      } else {
         if (pstChnlInfo->fnRxCallback != NULL) {
            pstChnlInfo->fnRxCallback(aucRxBuffer[CHIP_C_HCI_SIZE_OFFSET], &aucRxBuffer[CHIP_C_HCI_DATA_OFFSET]);
         } else {
            ANT_WARN("%s rx callback is null", pstChnlInfo->pcDevicePath);
         }
      }
      iRet = 0;
   }
out:
   ANT_FUNC_END();
   return iRet;
}

void *fnRxThread(void *ant_rx_thread_info)
{
   int iMutexLockResult;
   int iPollRet;
   ant_rx_thread_info_t *stRxThreadInfo;
   struct pollfd astPollFd[NUM_ANT_CHANNELS];
   enum ant_channel_type eChannel;
   ANT_FUNC_START();

   stRxThreadInfo = (ant_rx_thread_info_t *)ant_rx_thread_info;
   for (eChannel = 0; eChannel < NUM_ANT_CHANNELS; eChannel++) {
      astPollFd[eChannel].fd = stRxThreadInfo->astChannels[eChannel].iFd;
      astPollFd[eChannel].events = POLLIN | POLLRDNORM;
   }

   while (stRxThreadInfo->ucRunThread) {
      iPollRet = poll(astPollFd, NUM_ANT_CHANNELS, ANT_POLL_TIMEOUT);
      if (!iPollRet) {
         ANT_DEBUG_V("poll timed out, checking exit cond");
      } else if (iPollRet < 0) {
         ANT_ERROR("read thread exiting, unhandled error: %s", strerror(errno));
      } else {
         for (eChannel = 0; eChannel < NUM_ANT_CHANNELS; eChannel++) {
            if (astPollFd[eChannel].revents & (POLLERR | POLLPRI | POLLRDHUP)) {
               ANT_ERROR("poll error from %s. exiting rx thread",
                            stRxThreadInfo->astChannels[eChannel].pcDevicePath);
               /* Chip was reset or other error, only way to recover is to
                * close and open ANT chardev */
               stRxThreadInfo->ucChipResetting = 1;
               if (g_fnStateCallback)
                  g_fnStateCallback(RADIO_STATUS_RESETTING);
               goto reset;

            } else if (astPollFd[eChannel].revents & (POLLIN | POLLRDNORM)) {
               ANT_DEBUG_D("data on %s. reading it",
                            stRxThreadInfo->astChannels[eChannel].pcDevicePath);
               if (readChannelMsg(&stRxThreadInfo->astChannels[eChannel]) < 0)
                  goto out;
            } else if (astPollFd[eChannel].revents) {
               ANT_DEBUG_W("unhandled poll result %#x from %s",
                            astPollFd[eChannel].revents,
                            stRxThreadInfo->astChannels[eChannel].pcDevicePath);
            }
         }
      }
   }
out:
   stRxThreadInfo->ucRunThread = 0;
   /* Try to get stEnabledStatusLock.
    * if you get it then noone is enabling or disabling
    * if you can't get it assume something made you exit */
   ANT_DEBUG_V("try getting stEnabledStatusLock in %s", __FUNCTION__);
   iMutexLockResult = pthread_mutex_trylock(stRxThreadInfo->pstEnabledStatusLock);
   if (!iMutexLockResult) {
      ANT_DEBUG_V("got stEnabledStatusLock in %s", __FUNCTION__);
      ANT_WARN("rx thread has unexpectedly crashed, cleaning up");
      stRxThreadInfo->stRxThread = 0; /* spoof our handle as closed so we don't
                                       * try to join ourselves in disable */

      if (g_fnStateCallback)
         g_fnStateCallback(RADIO_STATUS_DISABLING);

      ant_do_disable();

      if (g_fnStateCallback)
         g_fnStateCallback(ant_radio_enabled_status());

      ANT_DEBUG_V("releasing stEnabledStatusLock in %s", __FUNCTION__);
      pthread_mutex_unlock(stRxThreadInfo->pstEnabledStatusLock);
      ANT_DEBUG_V("released stEnabledStatusLock in %s", __FUNCTION__);
   } else if (iMutexLockResult != EBUSY) {
      ANT_ERROR("rx thread closing code, trylock on state lock failed: %s",
            strerror(iMutexLockResult));
   } else {
      ANT_DEBUG_V("stEnabledStatusLock busy");
   }
   ANT_FUNC_END();
#ifdef ANDROID
   return NULL;
#endif

reset:
   stRxThreadInfo->ucRunThread = 0;
   ANT_DEBUG_V("getting stEnabledStatusLock in %s", __FUNCTION__);
   iMutexLockResult = pthread_mutex_lock(stRxThreadInfo->pstEnabledStatusLock);
   if (iMutexLockResult < 0) {
      ANT_ERROR("chip was reset, getting state mutex failed: %s",
            strerror(iMutexLockResult));
      stRxThreadInfo->stRxThread = 0;
   } else {
      ANT_DEBUG_V("got stEnabledStatusLock in %s", __FUNCTION__);
      stRxThreadInfo->stRxThread = 0; /* spoof our handle as closed so we don't
                                       * try to join ourselves in disable */
      ant_do_disable();

      if (ant_do_enable()) { /* failed */
         if (g_fnStateCallback)
            g_fnStateCallback(RADIO_STATUS_DISABLED);
      } else { /* success */
         if (g_fnStateCallback)
            g_fnStateCallback(RADIO_STATUS_RESET);
      }
      ANT_DEBUG_V("releasing stEnabledStatusLock in %s", __FUNCTION__);
      pthread_mutex_unlock(stRxThreadInfo->pstEnabledStatusLock);
      ANT_DEBUG_V("released stEnabledStatusLock in %s", __FUNCTION__);
   }
   stRxThreadInfo->ucChipResetting = 0;
   ANT_FUNC_END();
#ifdef ANDROID
   return NULL;
#endif
}

