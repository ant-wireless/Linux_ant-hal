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
*   FILE NAME:      ant_native_chardev.h
*
*   BRIEF:
*       This file defines constants for the char dev implementation
*
*
\*******************************************************************************/

#ifndef __ANT_NATIVE_CHARDEV_H
#define __ANT_NATIVE_CHARDEV_H

#define ANT_COMMANDS_DEVICE_NAME             "/dev/antradio_cmd"
#define ANT_DATA_DEVICE_NAME                 "/dev/antradio_data"

#define CHIP_B_HCI_SIZE_OFFSET               0
#define CHIP_B_HCI_DATA_OFFSET               1
#define CHIP_B_HCI_HEADER_SIZE               1

#define ANT_MESG_FLOW_CONTROL                ((ANT_U8)0xC9)
#define FLOW_GO                              ((ANT_U8)0x00)
#define FLOW_STOP                            ((ANT_U8)0x80)
#define CHIP_B_FLOW_GO_WAIT_TIMEOUT_SEC      10

int ant_do_enable(void);
void ant_do_disable(void);
extern ANTNativeANTStateCb g_fnStateCallback;

#endif /* ifndef __ANT_NATIVE_CHARDEV_H */

