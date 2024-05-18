/**
 *!
 * \file        b_mod_gui.c
 * \version     v0.0.1
 * \date        2020/03/02
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO GUI SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_gui.h"
#if (defined(_GUI_ENABLE) && (_GUI_ENABLE == 1))
#include "core/inc/b_core.h"
#include "drivers/inc/b_driver.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup GUI
 * \{
 */

/**
 * \defgroup GUI_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t  is_pressed;
} bGuiTouchData_t;
/**
 * \}
 */

/**
 * \defgroup GUI_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup GUI_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup GUI_Private_Variables
 * \{
 */

static bGUIInstance_t *pGUIHead    = NULL;
static bGUIInstance_t *pGUICurrent = NULL;

#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
#if defined(GUI_FONT_XBF)
UG_FONT         bGUI_XBF_Font;
static uint32_t bGUI_XBF_FontDevice = 0;
#endif
#endif
/**
 * \}
 */

/**
 * \defgroup GUI_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup GUI_Private_Functions
 * \{
 */

static bGUIInstance_t *_GUIId2Instance(uint32_t dev_no)
{
    bGUIInstance_t *p = pGUIHead;
    while (p != NULL)
    {
        if (p->lcd_dev_no == dev_no)
        {
            return p;
        }
        p = p->pnext;
    }
    return NULL;
}

static void _LCD_SetColorPixel(int16_t x, int16_t y, bGUIColor_t c)
{
    int      fd    = -1;
    int      tmp_y = x;
    uint32_t off;
    if (pGUICurrent == NULL)
    {
        return;
    }
    if (pGUICurrent->lcd_disp_dir == LCD_DISP_H)
    {
        x = pGUICurrent->lcd_x_size - 1 - y;
        y = tmp_y;
    }
    off = y;
    if (x >= pGUICurrent->lcd_x_size || y >= pGUICurrent->lcd_y_size)
    {
        return;
    }
    fd = bOpen(pGUICurrent->lcd_dev_no, BCORE_FLAG_W);
    if (fd < 0)
    {
        return;
    }
    bLseek(fd, off * pGUICurrent->lcd_x_size + x);
    bWrite(fd, (uint8_t *)&c, sizeof(bGUIColor_t));
    bClose(fd);
}

static void _LCD_FillRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, bGUIColor_t c)
{
    int            fd     = -1;
    int            retval = 0;
    bLcdRectInfo_t info;
    if (pGUICurrent == NULL)
    {
        return;
    }
    fd = bOpen(pGUICurrent->lcd_dev_no, BCORE_FLAG_W);
    if (fd < 0)
    {
        return;
    }
    info.x1    = (x1 > x2) ? x2 : x1;
    info.x2    = (x1 > x2) ? x1 : x2;
    info.y1    = (y1 > y2) ? y2 : y1;
    info.y2    = (y1 > y2) ? y1 : y2;
    info.color = c;
    retval     = bCtl(fd, bCMD_FILL_RECT, &info);
    bClose(fd);
    if (retval >= 0)
    {
        return;
    }
    for (; info.x1 <= info.x2; info.x1++)
    {
        for (; info.y1 <= info.y2; info.y1++)
        {
            _LCD_SetColorPixel(info.x1, info.y1, c);
        }
    }
}

#if defined(GUI_FONT_XBF)
static int _bGUI_ReadXBF(uint32_t off, uint8_t *pbuf, uint16_t len)
{
    int fd = -1;
    fd     = bOpen(bGUI_XBF_FontDevice, BCORE_FLAG_RW);
    if (fd < 0)
    {
        b_log_e("open err\r\n");
        return -1;
    }
    bLseek(fd, off);
    bRead(fd, pbuf, len);
    bClose(fd);
    return 0;
}
#endif

static int _bGUI_TouchRead(bGuiTouchData_t *pdata)
{
    int           fd = -1;
    uint16_t      tmp;
    bTouchAdVal_t ad_val;
    if (pGUICurrent == NULL || pGUICurrent->touch_dev_no == 0 || pdata == NULL)
    {
        return -1;
    }
    fd = bOpen(pGUICurrent->touch_dev_no, BCORE_FLAG_R);
    if (fd < 0)
    {
        return -1;
    }
    if (pGUICurrent->touch_type == TOUCH_TYPE_RES)
    {
        bRead(fd, (uint8_t *)&ad_val, sizeof(bTouchAdVal_t));
    }
    bClose(fd);

    pdata->x          = 0;
    pdata->y          = 0;
    pdata->is_pressed = 0;

    if (pGUICurrent->touch_type == TOUCH_TYPE_RES)
    {
        if (ad_val.x_ad < pGUICurrent->touch_ad_x[0] || ad_val.x_ad >= pGUICurrent->touch_ad_x[1] ||
            ad_val.y_ad < pGUICurrent->touch_ad_y[0] || ad_val.y_ad >= pGUICurrent->touch_ad_y[1])
        {
        }
        else
        {
            ad_val.x_ad = (ad_val.x_ad - pGUICurrent->touch_ad_x[0]) * pGUICurrent->lcd_x_size /
                          (pGUICurrent->touch_ad_x[1] - pGUICurrent->touch_ad_x[0]);
            ad_val.y_ad = (ad_val.y_ad - pGUICurrent->touch_ad_y[0]) * pGUICurrent->lcd_y_size /
                          (pGUICurrent->touch_ad_y[1] - pGUICurrent->touch_ad_y[0]);
            if (pGUICurrent->lcd_disp_dir == LCD_DISP_H)
            {
                tmp         = ad_val.x_ad;
                ad_val.x_ad = ad_val.y_ad;
                ad_val.y_ad = pGUICurrent->lcd_x_size - 1 - tmp;
            }
            pdata->x          = ad_val.x_ad;
            pdata->y          = ad_val.y_ad;
            pdata->is_pressed = 1;
        }
    }
    return 0;
}

static void _bGUI_Core()
{
    static uint32_t tick = 0;
    bGuiTouchData_t tdata;
    if (bHalGetSysTick() - tick > MS2TICKS(10))
    {
        tick = bHalGetSysTick();
#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
        if (0 == _bGUI_TouchRead(&tdata))
        {
            UG_TouchUpdate(tdata.x, tdata.y,
                           (tdata.is_pressed) ? TOUCH_STATE_PRESSED : TOUCH_STATE_RELEASED);
        }
#endif
    }
#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
    UG_Update();
#endif
#if (defined(_USE_LVGL) && (_USE_LVGL == 1))
    lv_task_handler();
#endif
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bGUI_Core);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif

#if (defined(_USE_LVGL) && (_USE_LVGL == 1))
#if LV_USE_LOG
void _lv_log_print_g_cb(lv_log_level_t level, const char *buf)
{
    b_log("%s\r\n", buf);
}
#endif

static void _lv_disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    bGUIDrawBmp(area->x1, area->y1, (area->x2 - area->x1) + 1, (area->y2 - area->y1) + 1, px_map);
    lv_display_flush_ready(disp_drv);
}

static void _lv_port_disp_init()
{
    lv_display_t *disp = lv_display_create(pGUICurrent->lcd_x_size, pGUICurrent->lcd_y_size);
    lv_display_set_flush_cb(disp, _lv_disp_flush);

#if LVGL_DISP_BUF_NUM == 1
    lv_color_t *pbuf = NULL;
    pbuf             = bCalloc(pGUICurrent->lcd_x_size * 10, sizeof(lv_color_t));
    B_ASSERT(pbuf != NULL);
    lv_display_set_buffers(disp, pbuf, NULL, (pGUICurrent->lcd_x_size * 10 * sizeof(lv_color_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
#if LVGL_DISP_BUF_NUM == 2
    lv_color_t *pbuf_1 = NULL;
    lv_color_t *pbuf_2 = NULL;
    pbuf_1             = bCalloc(pGUICurrent->lcd_x_size * 10, sizeof(lv_color_t));
    pbuf_2             = bCalloc(pGUICurrent->lcd_x_size * 10, sizeof(lv_color_t));
    B_ASSERT((pbuf_1 != NULL && pbuf_2 != NULL));
    lv_display_set_buffers(disp, pbuf_1, pbuf_2, pGUICurrent->lcd_x_size * 10 * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
}

static void _lv_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    bGuiTouchData_t tdata;
    if (_bGUI_TouchRead(&tdata) == 0)
    {
        data->point.x = tdata.x;
        data->point.y = tdata.y;
        data->state   = (tdata.is_pressed) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    }
}

static void _lv_port_indev_init()
{
    lv_indev_t *indev_touch = lv_indev_create();
    lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touch, _lv_indev_read_cb);
}

#endif

/**
 * \}
 */

/**
 * \addtogroup GUI_Exported_Functions
 * \{
 */

int bGUIRegist(bGUIInstance_t *pInstance)
{
    if (pInstance == NULL)
    {
        return -1;
    }

    if ((pInstance->touch_type != TOUCH_TYPE_RES && pInstance->touch_type != TOUCH_TYPE_CAP) &&
        pInstance->touch_dev_no != 0)
    {
        return -2;
    }

    int fd = -1;
    fd     = bOpen(pInstance->lcd_dev_no, BCORE_FLAG_RW);
    if (fd != -1)
    {
        bLcdSize_t lsize;
        lsize.width  = pInstance->lcd_x_size;
        lsize.length = pInstance->lcd_y_size;
        bCtl(fd, bCMD_SET_SIZE, &lsize);
        bClose(fd);
    }

    if (pGUIHead == NULL)
    {
        pGUIHead        = pInstance;
        pGUIHead->pnext = NULL;
    }
    else
    {
        pInstance->pnext = pGUIHead->pnext;
        pGUIHead->pnext  = pInstance;
    }
    pGUICurrent             = pInstance;
    pInstance->lcd_disp_dir = LCD_DISP_V;
#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
    pInstance->gui_handle.char_h_space = 0;
    pInstance->gui_handle.char_v_space = 0;
    UG_Init(&pInstance->gui_handle, _LCD_SetColorPixel, pInstance->lcd_x_size,
            pInstance->lcd_y_size);
    UG_SelectGUI(&pInstance->gui_handle);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_DriverRegister(DRIVER_FILL_FRAME, _LCD_FillRect);

#if defined(GUI_FONT_6X8)
    UG_FontSelect(&FONT_6X8);
#elif defined(GUI_FONT_8X12)
    UG_FontSelect(&FONT_8X12);
#elif defined(GUI_FONT_12X16)
    UG_FontSelect(&FONT_12X16);
#elif defined(GUI_FONT_XBF)
    uint8_t xbf_info[18];
    UG_DriverRegister(DRIVER_READXBF, _bGUI_ReadXBF);
    if (0 > _bGUI_ReadXBF(XBF_FILE_ADDR, xbf_info, 18))
    {
        b_log_e("read err\r\n");
        return -1;
    }
    bGUI_XBF_Font.char_height = xbf_info[4];
    bGUI_XBF_Font.char_width  = xbf_info[4] / 2;
    bGUI_XBF_Font.start_char  = 0x00;
    bGUI_XBF_Font.end_char    = 0xff;
    bGUI_XBF_Font.font_type   = FONT_TYPE_1BPP;
    bGUI_XBF_Font.p           = NULL;
    bGUI_XBF_Font.widths      = NULL;
    UG_FontSelect(&bGUI_XBF_Font);
#endif
#endif

#if (defined(_USE_ARM_2D) && (_USE_ARM_2D == 1))
    arm_2d_init();
#endif

#if (defined(_USE_LVGL) && (_USE_LVGL == 1))
#if LV_USE_LOG
    lv_log_register_print_cb(_lv_log_print_g_cb);
#endif /* LV_USE_LOG */
    lv_init();
    _lv_port_disp_init();
    _lv_port_indev_init();
    lv_tick_set_cb(bHalGetSysTick);
#endif
    return 0;
}

int bGUISelect(uint32_t lcd_dev_no)
{
    bGUIInstance_t *p = _GUIId2Instance(lcd_dev_no);
    if (p == NULL)
    {
        return -1;
    }
    pGUICurrent = p;
#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
    UG_SelectGUI(&p->gui_handle);
#endif
    return 0;
}

int bGUITouchRange(uint32_t lcd_dev_no, uint16_t x_ad_min, uint16_t x_ad_max, uint16_t y_ad_min,
                   uint16_t y_ad_max)
{

    bGUIInstance_t *p = _GUIId2Instance(lcd_dev_no);
    if (p == NULL)
    {
        return -1;
    }
    if (p->touch_type == TOUCH_TYPE_RES)
    {
        p->touch_ad_x[0] = x_ad_min;
        p->touch_ad_x[1] = x_ad_max;
        p->touch_ad_y[0] = y_ad_min;
        p->touch_ad_y[1] = y_ad_max;
    }
    else
    {
        return -1;
    }
    return 0;
}

int bGUIDispDir(uint32_t lcd_dev_no, uint8_t dir)
{
    bGUIInstance_t *p = _GUIId2Instance(lcd_dev_no);
    if (p == NULL || (dir != LCD_DISP_H && dir != LCD_DISP_V))
    {
        return -1;
    }
    p->lcd_disp_dir = dir;
    return 0;
}

int bGUISetFontDevice(uint32_t dev_no)
{
#if defined(GUI_FONT_XBF)
    bGUI_XBF_FontDevice = dev_no;
#else
    (void)dev_no;
#endif
    return 0;
}

#if (defined(_USE_UGUI) && (_USE_UGUI == 1))
int bGUIGetHandle(uint32_t lcd_dev_no, UG_GUI **p_gui_handle)
{
    bGUIInstance_t *p = _GUIId2Instance(lcd_dev_no);
    if (p == NULL || p_gui_handle == NULL)
    {
        return -1;
    }
    *p_gui_handle = &p->gui_handle;
    return 0;
}
#endif

int bGUIDrawBmp(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *bmp)
{
    int           fd     = -1;
    int           i      = 0;
    int           retval = 0;
    bLcdBmpInfo_t info;
    if (pGUICurrent == NULL || bmp == NULL || width == 0 || height == 0)
    {
        return -1;
    }
    fd = bOpen(pGUICurrent->lcd_dev_no, BCORE_FLAG_W);
    if (fd < 0)
    {
        return -1;
    }
    info.x1    = x;
    info.x2    = x + width - 1;
    info.y1    = y;
    info.y2    = y + height - 1;
    info.color = (uint8_t *)bmp;
    retval     = bCtl(fd, bCMD_FILL_BMP, &info);
    bClose(fd);
    if (retval >= 0)
    {
        return 0;
    }
    for (; info.x1 <= info.x2; info.x1++)
    {
        for (; info.y1 <= info.y2; info.y1++)
        {
            _LCD_SetColorPixel(info.x1, info.y1, ((uint16_t *)bmp)[i++]);
        }
    }
    return 0;
}

#if (defined(_USE_ARM_2D) && (_USE_ARM_2D == 1))
void Disp0_DrawBitmap(int16_t x, int16_t y, int16_t width, int16_t height, const uint8_t *bitmap)
{
    bGUIDrawBmp(x, y, width, height, bitmap);
}

int64_t arm_2d_helper_get_system_timestamp(void)
{
    return bHalGetSysTickPlus();
}

uint32_t arm_2d_helper_get_reference_clock_frequency(void)
{
    return TICK_FRQ_HZ;
}

#endif

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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
