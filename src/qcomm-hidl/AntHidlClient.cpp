/*
 *Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are
 *met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *
 *THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 *WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 *ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 *BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <iostream>       // std::cin, std::cout
#include <queue>          // std::queue
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable
#include <cstdlib>
#include <thread>

#include <hwbinder/ProcessState.h>

#include <com/qualcomm/qti/ant/1.0/IAntHci.h>
#include <com/qualcomm/qti/ant/1.0/IAntHciCallbacks.h>
#include <com/qualcomm/qti/ant/1.0/types.h>

#include <utils/Log.h>


#include <hidl/Status.h>
#include <hwbinder/ProcessState.h>
#include "ant_types.h"
#include "AntHidlClient.h"

using com::qualcomm::qti::ant::V1_0::IAntHci;
using com::qualcomm::qti::ant::V1_0::IAntHciCallbacks;
using com::qualcomm::qti::ant::V1_0::AntPacket;
using com::qualcomm::qti::ant::V1_0::Status;

using ::android::hardware::hidl_vec;


using android::hardware::ProcessState;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;
android::sp<IAntHci> anthci;
typedef std::unique_lock<std::mutex> Lock;



struct ant_hci_t {
public:
   std::condition_variable rx_cond;
   std::condition_variable on_cond;
   std::mutex on_mtx;
   std::mutex rx_mtx;
   ant_power_state_t state;
   bool rx_processing;
};

static struct ant_hci_t ant_hci;

Return<void> initialization_complete(bool is_hci_initialize)
{
   ALOGI("%s: is_hci_initialize: %d", __func__, is_hci_initialize);

   Lock lk(ant_hci.on_mtx);
   if (is_hci_initialize)
   {
      ant_hci.state = ANT_RADIO_ENABLED;
      ALOGI("%s: is_hci_initialize: %d", __func__, is_hci_initialize);
   }
   else
   {
      ALOGE("%s:Ant hci init failed :%d", __func__, is_hci_initialize);
      ant_hci.state = ANT_RADIO_DISABLED;
   }
   ant_hci.on_cond.notify_all();
   ALOGI("%s: exit", __func__);
   return Void();
}


class AntHciCallbacks : public IAntHciCallbacks {
public:
   AntHciCallbacks() {};
   virtual ~AntHciCallbacks() = default;

   Return<void> initializationComplete(Status status)
   {
      ALOGI("%s", __func__);
      if(status == Status::SUCCESS)
      {
         initialization_complete(true);
      } else {
         initialization_complete(false);
      }
      ALOGI("%s: exit", __func__);
      return Void();
   }

   Return<void> antControlReceived(const hidl_vec<uint8_t>& event)
   {
      ALOGI("%s: Start ", __func__);
      Lock lk(ant_hci.rx_mtx);

      // Make sure we don't overwrite a message still processing.
      while(ant_hci.rx_processing)
      {
         ant_hci.rx_cond.wait(lk);
      }
      ant_hci.rx_processing = true;

      memcpy(&aucRxBuffer[0][0], event.data(), event.size());
      iRxBufferLength[0] = event.size();
      ant_hci.rx_cond.notify_all();
      ALOGI("%s:  End", __func__);
      return Void();
   }

   Return<void> antDataReceived(const hidl_vec<uint8_t>& event)
   {
      ALOGI("%s: ", __func__);
      Lock lk(ant_hci.rx_mtx);

      // Make sure we don't overwrite a message still processing.
      while(ant_hci.rx_processing)
      {
         ant_hci.rx_cond.wait(lk);
      }
      ant_hci.rx_processing = true;

      memcpy(&aucRxBuffer[0][0], event.data(), event.size());
      iRxBufferLength[0] = event.size();
      ant_hci.rx_cond.notify_all();
      ALOGI("%s: exit", __func__);
      return Void();
   }
};

bool hci_initialize()
{
   ALOGI("%s", __func__);

   anthci = IAntHci::getService();

   if(anthci != nullptr)
   {
      ant_hci.state = ANT_RADIO_ENABLING;
      ant_hci.rx_processing = false;
      android::sp<IAntHciCallbacks> callbacks = new AntHciCallbacks();
      anthci->initialize(callbacks);
      ALOGI("%s: exit", __func__);
      return true;
   } else {
      return false;
   }
}

void hci_close() {
   ALOGV("%s", __func__);

   if(anthci != nullptr)
   {
      anthci->close();
   }
   ant_rx_clear();
   anthci =nullptr;
   ant_hci.state = ANT_RADIO_DISABLED;
   ALOGI("%s: exit", __func__);
}

ANT_UINT ant_get_status()
{
   return ant_hci.state;
}

ANTStatus ant_tx_write(ANT_U8 *pucTxMessage,ANT_U8 ucMessageLength)
{
   AntPacket data;
   ANT_U8  packet_type;
   ALOGI("%s: start", __func__);
   packet_type = *pucTxMessage;
   ALOGV("%s: proto type  :%d", __func__, packet_type);
   if (anthci != nullptr)
   {
      data.setToExternal(pucTxMessage+1, ucMessageLength-1);
      if (packet_type == ANT_DATA_TYPE_PACKET)
      {
         anthci->sendAntData(data);
      } else {
         anthci->sendAntControl(data);
      }
   } else {
      ALOGE("%s: antHci is NULL", __func__);
      return -1;
   }
   ALOGI("%s: exit", __func__);
   return ucMessageLength;
}

ANTStatus ant_rx_check()
{
   ALOGI("%s: start", __func__);
   Lock lk(ant_hci.rx_mtx);
   ant_hci.rx_cond.wait(lk);
   ALOGI("%s: exit", __func__);
   if (ant_hci.rx_processing)
   {
      return ANT_STATUS_SUCCESS;
   }
   else
   {
      return ANT_STATUS_NO_VALUE_AVAILABLE;
   }
}

void ant_rx_clear()
{
   ALOGI("%s: start", __func__);
   Lock lk(ant_hci.rx_mtx);
   ant_hci.rx_processing = false;
   ant_hci.rx_cond.notify_all();
   ALOGI("%s: exit", __func__);
}

void ant_interface_init()
{
   ALOGI("%s: start", __func__);
   bool status;
   status = hci_initialize();
   if (status)
   {
      ALOGV("%s waiting for iniialization complete hci state: %d ", __func__, ant_hci.state);
      Lock lk(ant_hci.on_mtx);
      while(ant_hci.state == ANT_RADIO_ENABLING)
      {
         ant_hci.on_cond.wait(lk);
         ALOGV(LOG_TAG,"%s:after on_cond wait  ",__func__);
      }
   } else {
      ALOGE(LOG_TAG,"%s:Failed ",__func__);
      ant_hci.state = ANT_RADIO_DISABLED;
   }
   ALOGI(LOG_TAG,"%s:exit ",__func__);
}
