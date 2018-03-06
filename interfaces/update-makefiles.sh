#!/bin/bash

source $ANDROID_BUILD_TOP/system/tools/hidl/update-makefiles-helper.sh

do_makefiles_update \
  "com.dsi:external/ant-wireless/hidl/interfaces" \
  "android.hidl:system/libhidl/transport"

