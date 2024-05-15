/**
 *!
 * \file        b_mod_usb.c
 * \version     v0.0.1
 * \date        2020/05/16
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_usb.h"

#if (defined(_USB_ENABLE) && (_USB_ENABLE == 1))
#include "b_section.h"
#include "hal/inc/b_hal_it.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup USB
 * \{
 */

/**
 * \defgroup USB_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    union
    {
        uint8_t  *pDescrip;
        uint8_t **pDescripTable;
    } _descript;
    uint8_t num;
} bDescriptor_t;

enum
{
    B_STRID_LANGID = 0,
    B_STRID_MANUFACTURER,
    B_STRID_PRODUCT,
    B_STRID_SERIAL,
};

/**
 * \}
 */

/**
 * \defgroup USB_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup USB_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup USB_Private_Variables
 * \{
 */

bDescriptor_t bDevicDescrip = {
    ._descript.pDescrip = NULL,
    .num                = 0,
};
bDescriptor_t bConfigDescrip = {
    ._descript.pDescripTable = NULL,
    .num                     = 0,
};
bDescriptor_t bHidReportDescrip = {
    ._descript.pDescripTable = NULL,
    .num                     = 0,
};

bDescriptor_t bStringDescrip = {
    ._descript.pDescrip = NULL,
    .num                = 0,
};

static char const *bDefaultStringDescrip[] = {
    (const char[]){0x09, 0x04, 0x0},  // 0: is supported language is English (0x0409)
    USB_MANUFACTURER,                 // 1: Manufacturer
    USB_PRODUCT,                      // 2: Product
    USB_SERIAL_NUMBER,                // 3: Serials will use unique ID if possible
};

static uint16_t bStringBuf[32 + 1];

/**
 * \}
 */

/**
 * \defgroup USB_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup USB_Private_Functions
 * \{
 */

static void _bUSBCore()
{
#if CFG_TUD_ENABLED
    tud_task();
#endif
#if CFG_TUH_ENABLED
    tuh_task();
#endif
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bUSBCore);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif

static void _USBItHandler(bHalItNumber_t it, uint8_t index, bHalItParam_t *param, void *user_data)
{
    tud_int_handler(index);
}

/**
 * \}
 */

/**
 * \addtogroup USB_Exported_Functions
 * \{
 */

int bUSBInit(uint8_t *dev_descrip, uint8_t **config_descrip, uint8_t config_num,
             uint8_t **string_descrip, uint8_t string_num)
{
    if (dev_descrip == NULL || config_descrip == NULL || config_num == 0)
    {
        return -1;
    }
    bDevicDescrip._descript.pDescrip       = dev_descrip;
    bDevicDescrip.num                      = 1;
    bConfigDescrip._descript.pDescripTable = config_descrip;
    bConfigDescrip.num                     = config_num;
    bStringDescrip._descript.pDescripTable = string_descrip;
    bStringDescrip.num                     = string_num;
    bHAL_IT_REGISTER(usb_it, B_HAL_IT_USB, 0, _USBItHandler, NULL);
    tud_init(BOARD_TUD_RHPORT);
    return 0;
}

int bUSBHidSetReportDescrip(uint8_t **report_descrip, uint8_t report_num)
{
    if (report_descrip == NULL)
    {
        return -1;
    }
    bHidReportDescrip._descript.pDescripTable = report_descrip;
    bHidReportDescrip.num                     = report_num;
    return 0;
}

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)bDevicDescrip._descript.pDescrip;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    if (index >= bConfigDescrip.num)
    {
        b_log_e("%d %d\r\n", index, bConfigDescrip.num);
        return NULL;
    }
    return (uint8_t const *)bConfigDescrip._descript.pDescripTable[index];
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf)
{
    if (itf >= bHidReportDescrip.num)
    {
        b_log_e("%d %d\r\n", itf, bHidReportDescrip.num);
        return NULL;
    }
    return (uint8_t const *)bHidReportDescrip._descript.pDescripTable[itf];
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    int       chr_count;
    uint8_t **pstring    = bStringDescrip._descript.pDescripTable;
    int       string_num = bStringDescrip.num;
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors
    if (pstring == NULL)
    {
        pstring    = (uint8_t **)&bDefaultStringDescrip[0];
        string_num = sizeof(bDefaultStringDescrip) / sizeof(bDefaultStringDescrip[0]);
    }
    if (!(index < string_num))
    {
        return NULL;
    }
    const char *str     = (const char *)pstring[index];
    chr_count           = strlen(str);
    int const max_count = sizeof(bStringBuf) / sizeof(bStringBuf[0]) - 1;  // -1 for string type
    if (chr_count > max_count)
    {
        chr_count = max_count;
    }
    // Convert ASCII string into UTF-16
    for (int i = 0; i < chr_count; i++)
    {
        bStringBuf[1 + i] = str[i];
    }
    // first byte is length (including header), second byte is string type
    bStringBuf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return bStringBuf;
}

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */
#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
