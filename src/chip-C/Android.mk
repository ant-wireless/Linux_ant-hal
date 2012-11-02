#
# Copyright (C) 2011 Dynastream Innovations
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifeq ($(BOARD_ANT_WIRELESS_DEVICE),"chip-C")

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -c -W -Wall -O2

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../common/inc \
    $(LOCAL_PATH)/inc \

LOCAL_SRC_FILES:= \
    ../../JAntNative.cpp \
    ../common/ant_utils.c \
    ant_native_chardev.c \
    ant_rx_chardev.c \

#JNI
LOCAL_C_INCLUDE += $(JNI_H_INCLUDE)

LOCAL_SHARED_LIBRARIES += \
    libnativehelper

# logging
LOCAL_SHARED_LIBRARIES += \
    libcutils

LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := libantradio

include $(BUILD_SHARED_LIBRARY)

endif # BOARD_ANT_WIRELESS_DEVICE = "chip-C"
