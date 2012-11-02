/*
 * ANT Stack
 *
 * Copyright 2011 Dynastream Innovations
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
/*******************************************************************************\
*
*   FILE NAME:      ant_rx_chardev.h
*
*   BRIEF:
*       This file defines public members in ant_rx_chardev.c
*
*
\*******************************************************************************/

#ifndef __ANT_RX_NATIVE_H
#define __ANT_RX_NATIVE_H

#include "ant_native.h"

/* This struct defines the info passed to an rx thread */
typedef struct {
   /* Device path */
   const char *pcDevicePath;
   /* File descriptor to read from */
   int iFd;
   /* Callback to call with ANT packet */
   ANTNativeANTEventCb fnRxCallback;
   /* Flow control response if channel supports it */
   ANT_U8 ucFlowControlResp;
   /* Handle to flow control condition */
   pthread_cond_t *pstFlowControlCond;
   /* Handle to flow control mutex */
   pthread_mutex_t *pstFlowControlLock;
} ant_channel_info_t;

enum ant_channel_type {
   DATA_CHANNEL,
   COMMAND_CHANNEL,
   NUM_ANT_CHANNELS
};

typedef struct {
   /* Thread handle */
   pthread_t stRxThread;
   /* Exit condition */
   ANT_U8 ucRunThread;
   /* Set state as resetting override */
   ANT_U8 ucChipResetting;
   /* Handle to state change lock for crash cleanup */
   pthread_mutex_t *pstEnabledStatusLock;
   /* ANT channels */
   ant_channel_info_t astChannels[NUM_ANT_CHANNELS];
} ant_rx_thread_info_t;

/* This is the rx thread function. It loops reading ANT packets until told to
 * exit */
void *fnRxThread(void *ant_rx_thread_info);

#endif /* ifndef __ANT_RX_NATIVE_H */

