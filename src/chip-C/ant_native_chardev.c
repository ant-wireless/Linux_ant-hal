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
*   FILE NAME:     ant_native_chardev.c
*
*   BRIEF:
*      This file provides the character device implementation of ant_native.h
*
*
\*******************************************************************************/

#include <errno.h>
#include <fcntl.h> /* for open() */
#include <linux/ioctl.h>
#include <pthread.h>

#include "ant_native.h"
#include "ant_types.h"
#include "ant_log.h"
#include "ant_version.h"

#include "ant_native_chardev.h"
#include "ant_rx_chardev.h"

#define MESG_BROADCAST_DATA_ID               ((ANT_U8)0x4E)
#define MESG_ACKNOWLEDGED_DATA_ID            ((ANT_U8)0x4F)
#define MESG_BURST_DATA_ID                   ((ANT_U8)0x50)
#define MESG_EXT_BROADCAST_DATA_ID           ((ANT_U8)0x5D)
#define MESG_EXT_ACKNOWLEDGED_DATA_ID        ((ANT_U8)0x5E)
#define MESG_EXT_BURST_DATA_ID               ((ANT_U8)0x5F)

static ant_rx_thread_info_t stRxThreadInfo;
static pthread_mutex_t stEnabledStatusLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stFlowControlLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stFlowControlCond = PTHREAD_COND_INITIALIZER;
ANTNativeANTStateCb g_fnStateCallback;

static void ant_channel_init(ant_channel_info_t *pstChnlInfo, const char *pcCharDevName)
{
   pstChnlInfo->pcDevicePath = pcCharDevName;
   pstChnlInfo->iFd = -1;
   pstChnlInfo->fnRxCallback = NULL;
   pstChnlInfo->ucFlowControlResp = FLOW_GO;
   pstChnlInfo->pstFlowControlCond = &stFlowControlCond;
   pstChnlInfo->pstFlowControlLock = &stFlowControlLock;
}

ANTStatus ant_init(void)
{
   ANTStatus uiRet = ANT_STATUS_FAILED;
   ANT_FUNC_START();
   stRxThreadInfo.stRxThread = 0;
   stRxThreadInfo.ucRunThread = 0;
   stRxThreadInfo.ucChipResetting = 0;
   stRxThreadInfo.pstEnabledStatusLock = &stEnabledStatusLock;
   g_fnStateCallback = 0;
   ant_channel_init(&stRxThreadInfo.astChannels[COMMAND_CHANNEL], ANT_COMMANDS_DEVICE_NAME);
   ant_channel_init(&stRxThreadInfo.astChannels[DATA_CHANNEL], ANT_DATA_DEVICE_NAME);
   uiRet = ANT_STATUS_SUCCESS;
   ANT_FUNC_END();
   return uiRet;
}

ANTStatus ant_deinit(void)
{
   ANTStatus uiRet = ANT_STATUS_FAILED;
   ANT_FUNC_START();
   uiRet = ANT_STATUS_SUCCESS;
   ANT_FUNC_END();
   return uiRet;
}

ANTStatus set_ant_rx_callback(ANTNativeANTEventCb rx_callback_func)
{
   ANT_FUNC_START();
   stRxThreadInfo.astChannels[COMMAND_CHANNEL].fnRxCallback = rx_callback_func;
   stRxThreadInfo.astChannels[DATA_CHANNEL].fnRxCallback = rx_callback_func;
   ANT_FUNC_END();
   return ANT_STATUS_SUCCESS;
}

ANTStatus set_ant_state_callback(ANTNativeANTStateCb state_callback_func)
{
   ANT_FUNC_START();
   g_fnStateCallback = state_callback_func;
   ANT_FUNC_END();
   return ANT_STATUS_SUCCESS;
}

ANTStatus ant_tx_message(ANT_U8 ucLen, ANT_U8 *pucMesg)
{
   ANTStatus uiRet = ANT_STATUS_FAILED;
   int iMutexResult;
   int iResult;
   struct timespec stTimeout;
   int iCondWaitResult;
   ANT_U8 txBuffer[ANT_HCI_MAX_MSG_SIZE];
   ANT_FUNC_START();
   if (ant_radio_enabled_status() != RADIO_STATUS_ENABLED) {
      uiRet = ANT_STATUS_FAILED_BT_NOT_INITIALIZED;
      goto out;
   }
   txBuffer[CHIP_C_HCI_SIZE_OFFSET] = ucLen;
   memcpy(txBuffer + CHIP_C_HCI_HEADER_SIZE, pucMesg, ucLen);
   ANT_SERIAL(txBuffer, ucLen + CHIP_C_HCI_HEADER_SIZE, 'T');
   switch (txBuffer[CHIP_C_HCI_DATA_OFFSET + ANT_MSG_ID_OFFSET]) {
   case MESG_BROADCAST_DATA_ID:
   case MESG_ACKNOWLEDGED_DATA_ID:
   case MESG_BURST_DATA_ID:
   case MESG_EXT_BROADCAST_DATA_ID:
   case MESG_EXT_ACKNOWLEDGED_DATA_ID:
   case MESG_EXT_BURST_DATA_ID:
      ANT_DEBUG_V("getting stFlowControlLock in %s", __FUNCTION__);
      iMutexResult = pthread_mutex_lock(&stFlowControlLock);
      if (iMutexResult) {
         ANT_ERROR("failed to lock flow control mutex during tx: %s", strerror(iMutexResult));
         goto out;
      }
      ANT_DEBUG_V("got stFlowControlLock in %s", __FUNCTION__);

      stRxThreadInfo.astChannels[COMMAND_CHANNEL].ucFlowControlResp = FLOW_STOP;
      iResult = write(stRxThreadInfo.astChannels[DATA_CHANNEL].iFd, txBuffer, ucLen + CHIP_C_HCI_HEADER_SIZE);
      if (iResult < 0) {
         ANT_ERROR("failed to write data message to device: %s", strerror(errno));
      } else if (iResult != ucLen + CHIP_C_HCI_HEADER_SIZE) {
         ANT_ERROR("bytes written and message size dont match up");
      } else {
         stTimeout.tv_sec = time(0) + CHIP_C_FLOW_GO_WAIT_TIMEOUT_SEC;
         stTimeout.tv_nsec = 0;

         while (stRxThreadInfo.astChannels[COMMAND_CHANNEL].ucFlowControlResp != FLOW_GO) {
            iCondWaitResult = pthread_cond_timedwait(&stFlowControlCond, &stFlowControlLock, &stTimeout);
            if (iCondWaitResult) {
               ANT_ERROR("failed to wait for flow control response: %s", strerror(iCondWaitResult));
               if (iCondWaitResult == ETIMEDOUT)
                  uiRet = ANT_STATUS_HARDWARE_ERR;
               goto wait_error;
            }
         }
         uiRet = ANT_STATUS_SUCCESS;
      }
wait_error:
      ANT_DEBUG_V("releasing stFlowControlLock in %s", __FUNCTION__);
      pthread_mutex_unlock(&stFlowControlLock);
      ANT_DEBUG_V("released stFlowControlLock in %s", __FUNCTION__);
      break;
   default:
      iResult = write(stRxThreadInfo.astChannels[COMMAND_CHANNEL].iFd, txBuffer, ucLen + CHIP_C_HCI_HEADER_SIZE);
      if (iResult < 0) {
         ANT_ERROR("failed to write message to device: %s", strerror(errno));
      }  else if (iResult != ucLen + CHIP_C_HCI_HEADER_SIZE) {
         ANT_ERROR("bytes written and message size dont match up");
      } else {
         uiRet = ANT_STATUS_SUCCESS;
      }
   }
out:
   ANT_FUNC_END();
   return uiRet;
}

ANTStatus ant_radio_hard_reset(void)
{
   ANTStatus result_status = ANT_STATUS_NOT_SUPPORTED;
   ANT_FUNC_START();
   ANT_FUNC_END();
   return result_status;
}

static void ant_disable_channel(ant_channel_info_t *pstChnlInfo)
{
   ANT_FUNC_START();
   if (!pstChnlInfo) {
      ANT_ERROR("null channel info passed to channel disable function");
      goto out;
   }
   if (pstChnlInfo->iFd != -1) {
      if (close(pstChnlInfo->iFd) < 0) {
         ANT_ERROR("failed to close channel %s(%#x): %s", pstChnlInfo->pcDevicePath, pstChnlInfo->iFd, strerror(errno));
      }
      pstChnlInfo->iFd = -1; //TODO can this overwrite a still valid fd?
   } else {
      ANT_DEBUG_D("%s file is already closed", pstChnlInfo->pcDevicePath);
   }
out:
   ANT_FUNC_END();
}

static int ant_enable_channel(ant_channel_info_t *pstChnlInfo)
{
   int iRet = -1;
   ANT_FUNC_START();
   if (!pstChnlInfo) {
      ANT_ERROR("null channel info passed to channel enable function");
      errno = EINVAL;
      goto out;
   }
   if (pstChnlInfo->iFd == -1) {
      pstChnlInfo->iFd = open(pstChnlInfo->pcDevicePath, O_RDWR);
      if (pstChnlInfo->iFd < 0) {
         ANT_ERROR("failed to open dev %s: %s", pstChnlInfo->pcDevicePath, strerror(errno));
         goto out;
      }
   } else {
      ANT_DEBUG_D("%s is already enabled", pstChnlInfo->pcDevicePath);
   }
   iRet = 0;
out:
   ANT_FUNC_END();
   return iRet;
}

int ant_do_enable(void)
{
   int iRet = -1;
   enum ant_channel_type eChannel;
   ANT_FUNC_START();

   stRxThreadInfo.ucRunThread = 1;
   for (eChannel = 0; eChannel < NUM_ANT_CHANNELS; eChannel++) {
      if (ant_enable_channel(&stRxThreadInfo.astChannels[eChannel]) < 0) {
         ANT_ERROR("failed to enable channel %s: %s",
                         stRxThreadInfo.astChannels[eChannel].pcDevicePath,
                         strerror(errno));
         goto out;
      }
   }
   if (stRxThreadInfo.stRxThread == 0) {
      if (pthread_create(&stRxThreadInfo.stRxThread, NULL, fnRxThread, &stRxThreadInfo) < 0) {
         ANT_ERROR("failed to start rx thread: %s", strerror(errno));
         goto out;
      }
   } else {
      ANT_DEBUG_D("rx thread is already running");
   }
   if (!stRxThreadInfo.ucRunThread) {
      ANT_ERROR("rx thread crashed during init");
      goto out;
   }
   iRet = 0;
out:
   ANT_FUNC_END();
   return iRet;
}

void ant_do_disable(void)
{
   enum ant_channel_type eChannel;
   ANT_FUNC_START();
   stRxThreadInfo.ucRunThread = 0;
   for (eChannel = 0; eChannel < NUM_ANT_CHANNELS; eChannel++)
      ant_disable_channel(&stRxThreadInfo.astChannels[eChannel]);
   if (stRxThreadInfo.stRxThread != 0) {
      if (pthread_join(stRxThreadInfo.stRxThread, NULL) < 0) {
         ANT_ERROR("failed to join rx thread: %s", strerror(errno));
      }
      stRxThreadInfo.stRxThread = 0;
   } else {
      ANT_DEBUG_D("rx thread is not running");
   }
   ANT_FUNC_END();
}

ANTStatus ant_enable_radio(void)
{
   int iLockResult;
   ANTStatus uiRet = ANT_STATUS_FAILED;
   ANT_FUNC_START();
   ANT_DEBUG_V("getting stEnabledStatusLock in %s", __FUNCTION__);
   iLockResult = pthread_mutex_lock(&stEnabledStatusLock);
   if(iLockResult) {
      ANT_ERROR("enable failed to get state lock: %s", strerror(iLockResult));
      goto out;
   }
   ANT_DEBUG_V("got stEnabledStatusLock in %s", __FUNCTION__);

   if (g_fnStateCallback)
      g_fnStateCallback(RADIO_STATUS_ENABLING);

   if (ant_do_enable() < 0) {
      ANT_ERROR("ant enable failed: %s", strerror(errno));

      ant_do_disable();

      if (g_fnStateCallback)
         g_fnStateCallback(ant_radio_enabled_status());
   } else {
      if (g_fnStateCallback)
         g_fnStateCallback(RADIO_STATUS_ENABLED);
      uiRet = ANT_STATUS_SUCCESS;
   }

   ANT_DEBUG_V("releasing stEnabledStatusLock in %s", __FUNCTION__);
   pthread_mutex_unlock(&stEnabledStatusLock);
   ANT_DEBUG_V("released stEnabledStatusLock in %s", __FUNCTION__);
out:
   ANT_FUNC_END();
   return uiRet;
}

ANTStatus ant_disable_radio(void)
{
   int iLockResult;
   ANTStatus uiRet = ANT_STATUS_FAILED;
   ANT_FUNC_START();
   ANT_DEBUG_V("getting stEnabledStatusLock in %s", __FUNCTION__);
   iLockResult = pthread_mutex_lock(&stEnabledStatusLock);
   if(iLockResult) {
      ANT_ERROR("disable failed to get state lock: %s", strerror(iLockResult));
      goto out;
   }
   ANT_DEBUG_V("got stEnabledStatusLock in %s", __FUNCTION__);

   if (g_fnStateCallback)
      g_fnStateCallback(RADIO_STATUS_DISABLING);

   ant_do_disable();

   if (g_fnStateCallback)
      g_fnStateCallback(ant_radio_enabled_status());
   uiRet = ANT_STATUS_SUCCESS;

   ANT_DEBUG_V("releasing stEnabledStatusLock in %s", __FUNCTION__);
   pthread_mutex_unlock(&stEnabledStatusLock);
   ANT_DEBUG_V("released stEnabledStatusLock in %s", __FUNCTION__);
out:
   ANT_FUNC_END();
   return uiRet;
}

ANTRadioEnabledStatus ant_radio_enabled_status(void)
{
   enum ant_channel_type eChannel;
   int iOpenFiles = 0;
   int iOpenThread;
   ANTRadioEnabledStatus uiRet = RADIO_STATUS_UNKNOWN;
   ANT_FUNC_START();
   if (stRxThreadInfo.ucChipResetting) {
      uiRet = RADIO_STATUS_RESETTING;
      goto out;
   }
   for (eChannel = 0; eChannel < NUM_ANT_CHANNELS; eChannel++)
      if (stRxThreadInfo.astChannels[eChannel].iFd != -1)
         iOpenFiles++;
   iOpenThread = (stRxThreadInfo.stRxThread) ? 1 : 0;

   if (!stRxThreadInfo.ucRunThread) {
      if (iOpenFiles || iOpenThread) {
         uiRet = RADIO_STATUS_DISABLING;
      } else {
         uiRet = RADIO_STATUS_DISABLED;
      }
   } else {
      if ((iOpenFiles == NUM_ANT_CHANNELS) && iOpenThread) {
         uiRet = RADIO_STATUS_ENABLED;
      } else if (!iOpenFiles && iOpenThread) {
         uiRet = RADIO_STATUS_UNKNOWN;
      } else {
         uiRet = RADIO_STATUS_ENABLING;
      }
   }
out:
   ANT_DEBUG_D("get radio enabled status returned %d", uiRet);
   ANT_FUNC_END();
   return uiRet;
}

const char *ant_get_lib_version()
{
   return "libantradio.so: CHIP_C TTY Transport. Version "
      LIBANT_STACK_MAJOR"."LIBANT_STACK_MINOR"."LIBANT_STACK_INCRE;
}

