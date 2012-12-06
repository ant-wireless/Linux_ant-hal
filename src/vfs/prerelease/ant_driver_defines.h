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
*   FILE NAME:    ant_driver_defines.h
*
*   BRIEF:
*      This file defines ANT specific HCI values used by the ANT chip for a
*      sample TTY implementation.
*
*
\*******************************************************************************/

#ifndef __VFS_PRERELEASE_H
#define __VFS_PRERELEASE_H

#define ANT_CHIP_NAME                        "TTY"

#define ANT_COMMANDS_DEVICE_NAME             "/dev/smd5"
#define ANT_DATA_DEVICE_NAME                 "/dev/smd6"

// Hard reset not supported, don't define ANT_IOCTL_RESET

// -----------------------------------------
// |         Header       | Data |  Footer  |
// |----------------------|-----------------|
// |Optional| Data | Opt. | ...  | Optional |
// | Opcode | Size | Sync |      | Checksum |

#define ANT_HCI_OPCODE_SIZE                  0
#define ANT_HCI_SIZE_SIZE                    1

#define ANT_HCI_SYNC_SIZE                    0
#define ANT_HCI_CHECKSUM_SIZE                0

#define ANT_MESG_FLOW_CONTROL                ((ANT_U8)0xC9)

#define ANT_FLOW_GO                          ((ANT_U8)0x00)

// ---------------------- Not chip specific

#define ANT_HCI_HEADER_SIZE                  ((ANT_HCI_OPCODE_SIZE) + (ANT_HCI_SIZE_SIZE) + (ANT_HCI_SYNC_SIZE))

#define ANT_HCI_OPCODE_OFFSET                0
#define ANT_HCI_SIZE_OFFSET                  ((ANT_HCI_OPCODE_OFFSET) + (ANT_HCI_OPCODE_SIZE))
#define ANT_HCI_SYNC_OFFSET                  ((ANT_HCI_SIZE_OFFSET) + (ANT_HCI_SIZE_SIZE))
#define ANT_HCI_DATA_OFFSET                  (ANT_HCI_HEADER_SIZE)

#define ANT_FLOW_STOP                        ((ANT_U8)0x80)
#define ANT_FLOW_GO_WAIT_TIMEOUT_SEC         10

#endif /* ifndef __VFS_PRERELEASE_H */
