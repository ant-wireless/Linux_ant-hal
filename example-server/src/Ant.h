/*
 * ANT Android Host Stack
 *
 * Copyright 2018 Dynastream Innovations
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

#ifndef COM_DSI_ANT_V1_0_ANT_H
#define COM_DSI_ANT_V1_0_ANT_H

#include <mutex>
#include <thread>

#include <com/dsi/ant/1.0/IAnt.h>
#include <hidl/Status.h>

namespace com {
namespace dsi {
namespace ant {
namespace V1_0 {
namespace example {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct Ant : public IAnt {

public:

    // These need to be declared here because they are used in the inline versions
    // of sendData/CommandMessage.
    static const uint8_t command_channel_id = 0x0C;
    static const uint8_t data_channel_id = 0x0E;

    // Methods from IAnt follow.
    Return<void> getProperties(getProperties_cb _hidl_cb) override;
    Return<void> setCallbacks(const sp<IAntCallbacks>& callbacks) override;
    Return<void> translateStatus(Status status, translateStatus_cb _hidl_cb) override;
    Return<Status> enable() override;
    Return<Status> disable() override;

    Return<Status> sendDataMessage(const hidl_vec<uint8_t>& msg) override
    { return writeMsg(msg, data_channel_id); }
    Return<Status> sendCommandMessage(const hidl_vec<uint8_t>& msg) override
    { return writeMsg(msg, command_channel_id); }

    Ant();

private:

    sp<IAntCallbacks> callbacks;
    // File handle used for communicating with the ant chip.
    int transport_fd;
    // File handle used to signal the poll thread when shutting down.
    int shutdown_fd;
    std::thread poll_thread;
    // This is set to indicate that the poll thread should exit.
    bool stop_polling;
    // Coarse mutex for all internal state.
    std::mutex state_mtx;

    void pollThreadEntry();
    Status writeMsg(const hidl_vec<uint8_t> &msg, uint8_t channel_id);
};


}  // namespace example
}  // namespace V1_0
}  // namespace ant
}  // namespace dsi
}  // namespace com

#endif  // COM_DSI_ANT_V1_0_ANT_H
