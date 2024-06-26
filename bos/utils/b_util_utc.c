/**
 *!
 * \file        b_util_utc.c
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#include "utils/inc/b_util_utc.h"

#include "hal/inc/b_hal.h"

/**
 * \addtogroup B_UTILS
 * \{
 */

/**
 * \addtogroup UTC
 * \{
 */

/**
 * \defgroup UTC_Private_TypesDefinitions
 * \{
 */

#define YEAR_CHECK(yr) (((yr % 400) == 0x0) || (((yr % 4) == 0) && ((yr % 100) != 0)))

#define YEAR_DAYS(yr) (YEAR_CHECK(yr) ? 366 : 365)
#define BEGYEAR 1970  // UTC started at 00:00:00 January 1, 2000
#define DAY 86400UL   // 24 hours * 60 minutes * 60 seconds

#define IS_TIME_VALID(n)                                                             \
    ((n.month > 0 && n.month <= 12) && (n.day > 0 && n.day <= 31) && n.hour <= 23 && \
     n.minute <= 59 && n.second <= 59)

/**
 * \}
 */

/**
 * \defgroup UTC_Private_Variables
 * \{
 */
static uint64_t bBaseUTC  = 0;
static uint64_t bBaseTick = 0;
/**
 * \}
 */

/**
 * \defgroup UTC_Private_Functions
 * \{
 */
static uint8_t _bUTC_CalendarMonthDays(uint8_t lpyr, uint8_t month)
{
    uint8_t days = 31, i = 0;
    uint8_t table[7] = {1, 3, 5, 7, 8, 10, 12};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2)
    {
        days = (28 + lpyr);
    }
    else
    {
        for (i = 0; i < 7; i++)
        {
            if (month == table[i])
            {
                days = 31;
                break;
            }
        }
        if (i >= 7)
        {
            days = 30;
        }
    }
    return days;
}

/**
 * \}
 */

/**
 * \addtogroup UTC_Exported_Functions
 * \{
 */

void bUTC_SetTime(bUTC_t utc)
{
    bBaseUTC  = utc;
    bBaseTick = bHalGetSysTickPlus();
}

bUTC_t bUTC_GetTime()
{
    bUTC_t utc = bBaseUTC + (TICKS2MS(bHalGetSysTickPlus() - bBaseTick)) / 1000;
    return utc;
}

/**
 * \brief UTC to date struct \ref bUTC_DateTime_t
 */
void bUTC2Struct(bUTC_DateTime_t *tm, bUTC_t utc, double zone)
{
    utc = utc + zone * 60 * 60;

    uint32_t sec_in_day = utc % DAY;
    tm->second          = sec_in_day % 60;
    tm->minute          = (sec_in_day % 3600) / 60;
    tm->hour            = sec_in_day / 3600;

    uint32_t days = utc / DAY;

    tm->week = (((days % 7) + 3) % 7) + 1;

    tm->year = BEGYEAR;
    while (days >= YEAR_DAYS(tm->year))
    {
        days -= YEAR_DAYS(tm->year);
        tm->year++;
    }
    tm->month = 1;
    while (days >= _bUTC_CalendarMonthDays(YEAR_CHECK(tm->year), tm->month))
    {
        days -= _bUTC_CalendarMonthDays(YEAR_CHECK(tm->year), tm->month);
        tm->month++;
    }
    tm->day = days + 1;
}

/**
 * \brief Calculate UTC
 */
bUTC_t bStruct2UTC(bUTC_DateTime_t tm, double zone)
{
    bUTC_t seconds;
    if (!IS_TIME_VALID(tm))
    {
        return 0;
    }
    seconds = (((tm.hour * 60) + tm.minute) * 60) + tm.second;

    bUTC_t days  = tm.day - 1;
    int8_t month = tm.month;
    month--;
    while (month > 0)
    {
        days += _bUTC_CalendarMonthDays(YEAR_CHECK(tm.year), month);
        month--;
    }

    bUTC_t year = (tm.year > 0) ? (tm.year - 1) : 0;
    while (year >= BEGYEAR)
    {
        days += YEAR_DAYS(year);
        year--;
    }

    seconds += (days * DAY);
    seconds -= (bUTC_t)(zone * 60 * 60);

    return seconds;
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

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
