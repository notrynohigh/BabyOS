/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef _TUSB_H_
#define _TUSB_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// INCLUDE
//--------------------------------------------------------------------+
#include "tusb_common.h"
#include "osal.h"
#include "tusb_fifo.h"

#include "hid.h"

//------------- TypeC -------------//
#if CFG_TUC_ENABLED
  #include "usbc.h"
#endif

//------------- HOST -------------//
#if CFG_TUH_ENABLED
  #include "usbh.h"

  #if CFG_TUH_HID
    #include "hid_host.h"
  #endif

  #if CFG_TUH_MSC
    #include "msc_host.h"
  #endif

  #if CFG_TUH_CDC
    #include "cdc_host.h"
  #endif

  #if CFG_TUH_VENDOR
    #include "vendor_host.h"
  #endif
#else
  #ifndef tuh_int_handler
  #define tuh_int_handler(...)
  #endif
#endif

//------------- DEVICE -------------//
#if CFG_TUD_ENABLED
  #include "usbd.h"

  #if CFG_TUD_HID
    #include "hid_device.h"
  #endif

  #if CFG_TUD_CDC
    #include "cdc_device.h"
  #endif

  #if CFG_TUD_MSC
    #include "msc_device.h"
  #endif

  #if CFG_TUD_AUDIO
    #include "audio_device.h"
  #endif

  #if CFG_TUD_VIDEO
    #include "video_device.h"
  #endif

  #if CFG_TUD_MIDI
    #include "midi_device.h"
  #endif

  #if CFG_TUD_VENDOR
    #include "vendor_device.h"
  #endif

  #if CFG_TUD_USBTMC
    #include "usbtmc_device.h"
  #endif

  #if CFG_TUD_DFU_RUNTIME
    #include "dfu_rt_device.h"
  #endif

  #if CFG_TUD_DFU
    #include "dfu_device.h"
  #endif

  #if CFG_TUD_ECM_RNDIS || CFG_TUD_NCM
    #include "net_device.h"
  #endif

  #if CFG_TUD_BTH
    #include "bth_device.h"
  #endif
#else
  #ifndef tud_int_handler
  #define tud_int_handler(...)
  #endif
#endif


//--------------------------------------------------------------------+
// APPLICATION API
//--------------------------------------------------------------------+

// Initialize device/host stack
// Note: when using with RTOS, this should be called after scheduler/kernel is started.
// Otherwise it could cause kernel issue since USB IRQ handler does use RTOS queue API.
bool tusb_init(void);

// Check if stack is initialized
bool tusb_inited(void);

// TODO
// bool tusb_teardown(void);

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_H_ */
