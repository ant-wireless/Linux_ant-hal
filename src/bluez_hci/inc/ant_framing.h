/*
 * ANT Stack
 *
 * Copyright 2009 Dynastream Innovations
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
/******************************************************************************\
*
*   FILE NAME:      ANT_Framing.h
*
*   BRIEF:          
*		This file defines ANT specific HCI values used by the ANT chip.
*
*
\******************************************************************************/

#ifndef __ANT_HCIFRAMING_H
#define __ANT_HCIFRAMING_H

#ifdef BOARD_ANT_DEVICE_WILINK

/* Number to used by the VS Parameters Len field */
#define ANT_NATIVE_HCI_VS_PARMS_LEN_FIELD_LEN    (2)

#define HCI_CMD_ANT_MESSAGE_WRITE                ((ANT_U16)0x01D1)

#define HCI_CMD_OPCODE_ANT_LSB                   0xD1
#define HCI_CMD_OPCODE_ANT_MSB                   0xFD

#define HCI_VSOP_ANT_LSB                         0x00
#define HCI_VSOP_ANT_MSB                         0x05

#else

#error "Board ANT Device Type not recognised"

#endif

#endif /* __ANT_HCIFRAMING_H */
