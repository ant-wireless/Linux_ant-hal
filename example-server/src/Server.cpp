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

#define LOG_TAG "Ant Hidl Example"

#include "Ant.h"

#include "log/log.h"
#include <hidl/HidlTransportSupport.h>

using com::dsi::ant::V1_0::example::Ant;
using namespace android;
using namespace android::hardware;

/* Handle 1 data + 1 command + power control. */
static const uint32_t THREAD_POOL_SIZE = 3;

int main(void) {
    /*
     * This is fairly standard daemon code based on
     * defaultPassthroughServiceImplementation()
     */
    ALOGI("Starting example ANT HIDL daemon");
    configureRpcThreadpool(THREAD_POOL_SIZE, true);

    Ant ant;
    status_t status = ant.registerAsService();
    if (status != OK) {
        ALOGE("Unable to register service: %d", status);
        return -1;
    }

    joinRpcThreadpool();
}

