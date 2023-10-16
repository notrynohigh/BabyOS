/**
 *!
 * \file        mcu_stm32f10x_flash.c
 * \version     v0.0.1
 * \date        2021/06/13
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2021 Bean
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SGPIOL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include <string.h>

#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if defined(AT32F413xB)
#pragma anon_unions       // 在使用匿名联合的地方添加这个指令
#define UNUSED(x) (void)x /* to avoid gcc/g++ warnings */

#define __IO volatile /*!< Defines 'read / write' permissions */

#define FLASH_BASE ((uint32_t)0x08000000)
#define FLASH_BASE_ADDR (0x8000000UL)
#define FLASH_SPIM_START_ADDR ((uint32_t)0x08400000) /*!< flash start address of spim */

#if defined(AT32F413xC)
#define SECTOR_SIZE 2048 /* this parameter depends on the specific model of the chip */
#else
#define SECTOR_SIZE 1024 /* this parameter depends on the specific model of the chip */
#endif
#define FLASH_PAGE_SIZE SECTOR_SIZE

#define FLASH_MAX_SIZE (128 * 1024)

#define FLASH_UNLOCK_KEY1 ((uint32_t)0x45670123) /*!< flash operation unlock order key1 */
#define FLASH_UNLOCK_KEY2 ((uint32_t)0xCDEF89AB) /*!< flash operation unlock order key2 */
#define FAP_RELIEVE_KEY ((uint16_t)0x00A5)       /*!< flash fap relieve key val */
#define SLIB_UNLOCK_KEY ((uint32_t)0xA35F6D24)   /*!< flash slib operation unlock order key */

#define FLASH_ERR_INT ((uint32_t)0x00000001)      /*!< flash bank1 error interrupt */
#define FLASH_ODF_INT ((uint32_t)0x00000002)      /*!< flash bank1 operate done interrupt */
#define FLASH_SPIM_ERR_INT ((uint32_t)0x00000010) /*!< flash spim error interrupt */
#define FLASH_SPIM_ODF_INT ((uint32_t)0x00000020) /*!< flash spim operate done interrupt */

#define FLASH_OBF_FLAG ((uint32_t)0x00000001)          /*!< flash bank1 operate busy flag */
#define FLASH_ODF_FLAG ((uint32_t)0x00000020)          /*!< flash bank1 operate done flag */
#define FLASH_PRGMERR_FLAG ((uint32_t)0x00000004)      /*!< flash bank1 program error flag */
#define FLASH_EPPERR_FLAG ((uint32_t)0x00000010)       /*!< flash bank1 erase/program protection error flag */
#define FLASH_SPIM_OBF_FLAG ((uint32_t)0x20000001)     /*!< flash spim operate busy flag */
#define FLASH_SPIM_ODF_FLAG ((uint32_t)0x20000020)     /*!< flash spim operate done flag */
#define FLASH_SPIM_PRGMERR_FLAG ((uint32_t)0x20000004) /*!< flash spim program error flag */
#define FLASH_SPIM_EPPERR_FLAG ((uint32_t)0x20000010)  /*!< flash spim erase/program protection error flag */
#define FLASH_USDERR_FLAG ((uint32_t)0x40000001)       /*!< flash user system data error flag */

#define FLASH_SLIB_START_SECTOR ((uint32_t)0x000007FF)      /*!< flash slib start sector */
#define FLASH_SLIB_DATA_START_SECTOR ((uint32_t)0x003FF800) /*!< flash slib d-bus area start sector */
#define FLASH_SLIB_END_SECTOR ((uint32_t)0xFFC00000)        /*!< flash slib end sector */

#define ERASE_TIMEOUT ((uint32_t)0x40000000)            /*!< internal flash erase operation timeout */
#define PROGRAMMING_TIMEOUT ((uint32_t)0x00100000)      /*!< internal flash program operation timeout */
#define SPIM_ERASE_TIMEOUT ((uint32_t)0xFFFFFFFF)       /*!< spim erase operation timeout */
#define SPIM_PROGRAMMING_TIMEOUT ((uint32_t)0x00100000) /*!< spim program operation timeout */
#define OPERATION_TIMEOUT ((uint32_t)0x10000000)        /*!< flash common operation timeout */

/**
 * @brief flag status
 */
typedef enum
{
    RESET = 0,
    SET = !RESET
} flag_status;

/**
 * @brief confirm state
 */
typedef enum
{
    FALSE = 0,
    TRUE = !FALSE
} confirm_state;

/**
 * @brief error status
 */
typedef enum
{
    ERROR = 0,
    SUCCESS = !ERROR
} error_status;

/**
 * @brief  flash status type
 */
typedef enum
{
    FLASH_OPERATE_BUSY = 0x00,   /*!< flash status is operate busy */
    FLASH_PROGRAM_ERROR = 0x01,  /*!< flash status is program error */
    FLASH_EPP_ERROR = 0x02,      /*!< flash status is epp error */
    FLASH_OPERATE_DONE = 0x03,   /*!< flash status is operate done */
    FLASH_OPERATE_TIMEOUT = 0x04 /*!< flash status is operate timeout */
} flash_status_type;

/**
 * @brief  flash spim model type
 */
typedef enum
{
    FLASH_SPIM_MODEL1 = 0x01, /*!< spim model 1 */
    FLASH_SPIM_MODEL2 = 0x02, /*!< spim model 2 */
} flash_spim_model_type;

/**
 * @brief type define flash register all
 */
typedef struct
{
    /**
     * @brief flash psr register, offset:0x00
     */
    union
    {
        __IO uint32_t psr;
        struct
        {
            __IO uint32_t reserved1 : 32; /* [31:0] */
        } psr_bit;
    };

    /**
     * @brief flash unlock register, offset:0x04
     */
    union
    {
        __IO uint32_t unlock;
        struct
        {
            __IO uint32_t ukval : 32; /* [31:0] */
        } unlock_bit;
    };

    /**
     * @brief flash usd unlock register, offset:0x08
     */
    union
    {
        __IO uint32_t usd_unlock;
        struct
        {
            __IO uint32_t usd_ukval : 32; /* [31:0] */
        } usd_unlock_bit;
    };

    /**
     * @brief flash sts register, offset:0x0C
     */
    union
    {
        __IO uint32_t sts;
        struct
        {
            __IO uint32_t obf : 1;        /* [0] */
            __IO uint32_t reserved1 : 1;  /* [1] */
            __IO uint32_t prgmerr : 1;    /* [2] */
            __IO uint32_t reserved2 : 1;  /* [3] */
            __IO uint32_t epperr : 1;     /* [4] */
            __IO uint32_t odf : 1;        /* [5] */
            __IO uint32_t reserved3 : 26; /* [31:6] */
        } sts_bit;
    };

    /**
     * @brief flash ctrl register, offset:0x10
     */
    union
    {
        __IO uint32_t ctrl;
        struct
        {
            __IO uint32_t fprgm : 1;      /* [0] */
            __IO uint32_t secers : 1;     /* [1] */
            __IO uint32_t bankers : 1;    /* [2] */
            __IO uint32_t reserved1 : 1;  /* [3] */
            __IO uint32_t usdprgm : 1;    /* [4] */
            __IO uint32_t usders : 1;     /* [5] */
            __IO uint32_t erstr : 1;      /* [6] */
            __IO uint32_t oplk : 1;       /* [7] */
            __IO uint32_t reserved2 : 1;  /* [8] */
            __IO uint32_t usdulks : 1;    /* [9] */
            __IO uint32_t errie : 1;      /* [10] */
            __IO uint32_t reserved3 : 1;  /* [11] */
            __IO uint32_t odfie : 1;      /* [12] */
            __IO uint32_t reserved4 : 19; /* [31:13] */
        } ctrl_bit;
    };

    /**
     * @brief flash addr register, offset:0x14
     */
    union
    {
        __IO uint32_t addr;
        struct
        {
            __IO uint32_t fa : 32; /* [31:0] */
        } addr_bit;
    };

    /**
     * @brief flash reserved1 register, offset:0x18
     */
    __IO uint32_t reserved1;

    /**
     * @brief flash usd register, offset:0x1C
     */
    union
    {
        __IO uint32_t usd;
        struct
        {
            __IO uint32_t usderr : 1;     /* [0] */
            __IO uint32_t fap : 1;        /* [1] */
            __IO uint32_t wdt_ato_en : 1; /* [2] */
            __IO uint32_t depslp_rst : 1; /* [3] */
            __IO uint32_t stdby_rst : 1;  /* [4] */
            __IO uint32_t reserved1 : 5;  /* [9:5] */
            __IO uint32_t user_d0 : 8;    /* [17:10] */
            __IO uint32_t user_d1 : 8;    /* [25:18] */
            __IO uint32_t reserved2 : 6;  /* [31:26] */
        } usd_bit;
    };

    /**
     * @brief flash epps register, offset:0x20
     */
    union
    {
        __IO uint32_t epps;
        struct
        {
            __IO uint32_t epps : 32; /* [31:0] */
        } epps_bit;
    };

    /**
     * @brief flash reserved2 register, offset:0x80~0x24
     */
    __IO uint32_t reserved2[24];

    /**
     * @brief flash unlock3 register, offset:0x84
     */
    union
    {
        __IO uint32_t unlock3;
        struct
        {
            __IO uint32_t ukval : 32; /* [31:0] */
        } unlock3_bit;
    };

    /**
     * @brief flash select register, offset:0x88
     */
    union
    {
        __IO uint32_t select;
        struct
        {
            __IO uint32_t select : 32; /* [31:0] */
        } select_bit;
    };

    /**
     * @brief flash sts3 register, offset:0x8C
     */
    union
    {
        __IO uint32_t sts3;
        struct
        {
            __IO uint32_t obf : 1;        /* [0] */
            __IO uint32_t reserved1 : 1;  /* [1] */
            __IO uint32_t prgmerr : 1;    /* [2] */
            __IO uint32_t reserved2 : 1;  /* [3] */
            __IO uint32_t epperr : 1;     /* [4] */
            __IO uint32_t odf : 1;        /* [5] */
            __IO uint32_t reserved3 : 26; /* [31:6] */
        } sts3_bit;
    };

    /**
     * @brief flash ctrl3 register, offset:0x90
     */
    union
    {
        __IO uint32_t ctrl3;
        struct
        {
            __IO uint32_t fprgm : 1;      /* [0] */
            __IO uint32_t secers : 1;     /* [1] */
            __IO uint32_t chpers : 1;     /* [2] */
            __IO uint32_t reserved1 : 3;  /* [5:3] */
            __IO uint32_t erstr : 1;      /* [6] */
            __IO uint32_t oplk : 1;       /* [7] */
            __IO uint32_t reserved2 : 2;  /* [9:8] */
            __IO uint32_t errie : 1;      /* [10] */
            __IO uint32_t reserved3 : 1;  /* [11] */
            __IO uint32_t odfie : 1;      /* [12] */
            __IO uint32_t reserved4 : 19; /* [31:13] */
        } ctrl3_bit;
    };

    /**
     * @brief flash addr3 register, offset:0x94
     */
    union
    {
        __IO uint32_t addr3;
        struct
        {
            __IO uint32_t fa : 32; /* [31:0] */
        } addr3_bit;
    };

    /**
     * @brief flash da register, offset:0x98
     */
    union
    {
        __IO uint32_t da;
        struct
        {
            __IO uint32_t fda : 32; /* [31:0] */
        } da_bit;
    };

    /**
     * @brief flash reserved5 register, offset:0xC8~0x9C
     */
    __IO uint32_t reserved5[12];

    /**
     * @brief flash slib_sts0 register, offset:0xCC
     */
    union
    {
        __IO uint32_t slib_sts0;
        struct
        {
            __IO uint32_t reserved1 : 3;  /* [2:0] */
            __IO uint32_t slib_enf : 1;   /* [3] */
            __IO uint32_t reserved2 : 28; /* [31:4] */
        } slib_sts0_bit;
    };

    /**
     * @brief flash slib_sts1 register, offset:0xD0
     */
    union
    {
        __IO uint32_t slib_sts1;
        struct
        {
            __IO uint32_t slib_ss : 11;     /* [10:0] */
            __IO uint32_t slib_dat_ss : 11; /* [21:11] */
            __IO uint32_t slib_es : 10;     /* [31:22] */
        } slib_sts1_bit;
    };

    /**
     * @brief flash slib_pwd_clr register, offset:0xD4
     */
    union
    {
        __IO uint32_t slib_pwd_clr;
        struct
        {
            __IO uint32_t slib_pclr_val : 32; /* [31:0] */
        } slib_pwd_clr_bit;
    };

    /**
     * @brief flash slib_misc_sts register, offset:0xD8
     */
    union
    {
        __IO uint32_t slib_misc_sts;
        struct
        {
            __IO uint32_t slib_pwd_err : 1; /* [0] */
            __IO uint32_t slib_pwd_ok : 1;  /* [1] */
            __IO uint32_t slib_ulkf : 1;    /* [2] */
            __IO uint32_t reserved1 : 13;   /* [15:3] */
            __IO uint32_t slib_rcnt : 9;    /* [24:16] */
            __IO uint32_t reserved2 : 7;    /* [31:25] */
        } slib_misc_sts_bit;
    };

    /**
     * @brief flash slib_set_pwd register, offset:0xDC
     */
    union
    {
        __IO uint32_t slib_set_pwd;
        struct
        {
            __IO uint32_t slib_pset_val : 32; /* [31:0] */
        } slib_set_pwd_bit;
    };

    /**
     * @brief flash slib_set_range register, offset:0xE0
     */
    union
    {
        __IO uint32_t slib_set_range;
        struct
        {
            __IO uint32_t slib_ss_set : 11;  /* [10:0] */
            __IO uint32_t slib_dss_set : 11; /* [21:11] */
            __IO uint32_t slib_es_set : 10;  /* [31:22] */
        } slib_set_range_bit;
    };

    /**
     * @brief flash reserved6 register, offset:0xEC~0xE4
     */
    __IO uint32_t reserved6[3];

    /**
     * @brief flash slib_unlock register, offset:0xF0
     */
    union
    {
        __IO uint32_t slib_unlock;
        struct
        {
            __IO uint32_t slib_ukval : 32; /* [31:0] */
        } slib_unlock_bit;
    };

    /**
     * @brief flash crc_ctrl register, offset:0xF4
     */
    union
    {
        __IO uint32_t crc_ctrl;
        struct
        {
            __IO uint32_t crc_ss : 12;   /* [11:0] */
            __IO uint32_t crc_sn : 12;   /* [23:12] */
            __IO uint32_t reserved1 : 7; /* [30:24] */
            __IO uint32_t crc_strt : 1;  /* [31] */
        } crc_ctrl_bit;
    };

    /**
     * @brief flash crc_chkr register, offset:0xF8
     */
    union
    {
        __IO uint32_t crc_chkr;
        struct
        {
            __IO uint32_t crc_chkr : 32; /* [31:0] */
        } crc_chkr_bit;
    };

} McuFlashReg_t;

/**
 * @brief user system data
 */
typedef struct
{
    __IO uint16_t fap;
    __IO uint16_t ssb;
    __IO uint16_t data0;
    __IO uint16_t data1;
    __IO uint16_t epp0;
    __IO uint16_t epp1;
    __IO uint16_t epp2;
    __IO uint16_t epp3;
    __IO uint16_t eopb0;
    __IO uint16_t reserved;
    __IO uint16_t data2;
    __IO uint16_t data3;
    __IO uint16_t data4;
    __IO uint16_t data5;
    __IO uint16_t data6;
    __IO uint16_t data7;
    __IO uint16_t ext_flash_key[8];
} usd_type;

#define PERIPH_BASE ((uint32_t)0x40000000)

#define AHBPERIPH_BASE (PERIPH_BASE + 0x20000)

#define FLASH_REG_BASE (AHBPERIPH_BASE + 0x2000)
#define USD_BASE ((uint32_t)0x1FFFF800)

#define FLASH ((McuFlashReg_t *)FLASH_REG_BASE)
#define USD ((usd_type *)USD_BASE)

uint16_t flash_buf[SECTOR_SIZE / 2];

// static flag_status flash_flag_get(uint32_t flash_flag);
static void flash_flag_clear(uint32_t flash_flag);
static flash_status_type flash_operation_status_get(void);
static flash_status_type flash_operation_wait_for(uint32_t time_out);
static flash_status_type flash_spim_operation_wait_for(uint32_t time_out);
static void flash_unlock(void);
// static void flash_spim_unlock(void);
static void flash_lock(void);
// static void flash_spim_lock(void);
static flash_status_type flash_sector_erase(uint32_t raddr);
// static flash_status_type flash_internal_all_erase(void);
// static flash_status_type flash_spim_all_erase(void);
// static flash_status_type flash_user_system_data_erase(void);
// static flash_status_type flash_word_program(uint32_t address, uint32_t data);
static flash_status_type flash_halfword_program(uint32_t address, uint16_t data);
// static flash_status_type flash_byte_program(uint32_t address, uint8_t data);
// static flash_status_type flash_user_system_data_program(uint32_t address, uint8_t data);
// static flash_status_type flash_epp_set(uint32_t *sector_bits);
// static void flash_epp_status_get(uint32_t *sector_bits);
// static flash_status_type flash_fap_enable(confirm_state new_state);
// static flag_status flash_fap_status_get(void);
// static flash_status_type flash_ssb_set(uint8_t usd_ssb);
// static uint8_t flash_ssb_status_get(void);
// static void flash_interrupt_enable(uint32_t flash_int, confirm_state new_state);
// static void flash_spim_model_select(flash_spim_model_type mode);
// static void flash_spim_encryption_range_set(uint32_t decode_address);
static void flash_spim_dummy_read(void);
// static flash_status_type flash_spim_mass_program(uint32_t address, uint8_t *buf, uint32_t cnt);
// static flash_status_type flash_slib_enable(uint32_t pwd, uint16_t start_sector, uint16_t data_start_sector, uint16_t end_sector);
// static error_status flash_slib_disable(uint32_t pwd);
// static uint32_t flash_slib_remaining_count_get(void);
// static flag_status flash_slib_state_get(void);
// static uint16_t flash_slib_start_sector_get(void);
// static uint16_t flash_slib_datastart_sector_get(void);
// static uint16_t flash_slib_end_sector_get(void);

#if 1
/** @defgroup FLASH_private_functions
 * @{
 */

/**
 * @brief  check whether the specified flash flag is set or not.
 * @param  flash_flag: specifies the flash flag to check.
 *         this parameter can be one of flash flag status:
 *         - FLASH_OBF_FLAG
 *         - FLASH_ODF_FLAG
 *         - FLASH_PRGMERR_FLAG
 *         - FLASH_EPPERR_FLAG
 *         - FLASH_SPIM_OBF_FLAG
 *         - FLASH_SPIM_ODF_FLAG
 *         - FLASH_SPIM_PRGMERR_FLAG
 *         - FLASH_SPIM_EPPERR_FLAG
 *         - FLASH_USDERR_FLAG
 * @retval the new state of flash_flag (SET or RESET).
 */
// static flag_status flash_flag_get(uint32_t flash_flag)
// {
//     flag_status status = RESET;
//     uint32_t flag_position;
//     flag_position = flash_flag & 0x70000000;
//     flash_flag &= 0x8FFFFFFF;
//     switch (flag_position)
//     {
//     case 0x00000000:
//         if (FLASH->sts & flash_flag)
//             status = SET;
//         break;
//     case 0x20000000:
//         if (FLASH->sts3 & flash_flag)
//             status = SET;
//         break;
//     case 0x40000000:
//         if (FLASH->usd & flash_flag)
//             status = SET;
//         break;
//     default:
//         break;
//     }
//     /* return the new state of flash_flag (SET or RESET) */
//     return status;
// }

/**
 * @brief  clear the flash flag.
 * @param  flash_flag: specifies the flash flags to clear.
 *         this parameter can be any combination of the following values:
 *         - FLASH_ODF_FLAG
 *         - FLASH_PRGMERR_FLAG
 *         - FLASH_EPPERR_FLAG
 *         - FLASH_SPIM_ODF_FLAG
 *         - FLASH_SPIM_PRGMERR_FLAG
 *         - FLASH_SPIM_EPPERR_FLAG
 * @retval none
 */
static void flash_flag_clear(uint32_t flash_flag)
{
    uint32_t flag_position;
    flag_position = flash_flag & 0x70000000;
    flash_flag &= 0x8FFFFFFF;
    switch (flag_position)
    {
    case 0x00000000:
        FLASH->sts = flash_flag;
        break;
    case 0x20000000:
        FLASH->sts3 = flash_flag;
        break;
    default:
        break;
    }
}

/**
 * @brief  return the flash operation status.
 * @param  none
 * @retval status: the returned value can be: FLASH_OPERATE_BUSY,
 *         FLASH_PROGRAM_ERROR, FLASH_EPP_ERROR or FLASH_OPERATE_DONE.
 */
static flash_status_type flash_operation_status_get(void)
{
    flash_status_type flash_status = FLASH_OPERATE_DONE;
    if (FLASH->sts_bit.obf != RESET)
    {
        flash_status = FLASH_OPERATE_BUSY;
    }
    else if (FLASH->sts_bit.prgmerr != RESET)
    {
        flash_status = FLASH_PROGRAM_ERROR;
    }
    else if (FLASH->sts_bit.epperr != RESET)
    {
        flash_status = FLASH_EPP_ERROR;
    }
    else
    {
        flash_status = FLASH_OPERATE_DONE;
    }
    /* return the flash status */
    return flash_status;
}

/**
 * @brief  return the flash spim operation status.
 * @param  none
 * @retval status: the returned value can be: FLASH_OPERATE_BUSY,
 *         FLASH_PROGRAM_ERROR, FLASH_EPP_ERROR or FLASH_OPERATE_DONE.
 */
static flash_status_type flash_spim_operation_status_get(void)
{
    flash_status_type flash_status = FLASH_OPERATE_DONE;
    if (FLASH->sts3_bit.obf != RESET)
    {
        flash_status = FLASH_OPERATE_BUSY;
    }
    else if (FLASH->sts3_bit.prgmerr != RESET)
    {
        flash_status = FLASH_PROGRAM_ERROR;
    }
    else if (FLASH->sts3_bit.epperr != RESET)
    {
        flash_status = FLASH_EPP_ERROR;
    }
    else
    {
        flash_status = FLASH_OPERATE_DONE;
    }
    /* return the flash status */
    return flash_status;
}

/**
 * @brief  wait for flash operation complete or timeout.
 * @param  time_out: flash operation timeout
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
static flash_status_type flash_operation_wait_for(uint32_t time_out)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* check for the flash status */
    status = flash_operation_status_get();

    while ((status == FLASH_OPERATE_BUSY) && (time_out != 0x00))
    {
        status = flash_operation_status_get();
        time_out--;
    }
    if (time_out == 0x00)
    {
        status = FLASH_OPERATE_TIMEOUT;
    }
    /* return the status */
    return status;
}

/**
 * @brief  wait for flash spim operation complete or timeout.
 * @param  time_out: flash operation timeout
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
static flash_status_type flash_spim_operation_wait_for(uint32_t time_out)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* check for the flash status */
    status = flash_spim_operation_status_get();

    while ((status == FLASH_OPERATE_BUSY) && (time_out != 0x00))
    {
        status = flash_spim_operation_status_get();
        time_out--;
    }
    if (time_out == 0x00)
    {
        status = FLASH_OPERATE_TIMEOUT;
    }
    /* return the operation status */
    return status;
}

/**
 * @brief  unlock the flash controller.
 * @param  none
 * @retval none
 */
static void flash_unlock(void)
{
    FLASH->unlock = FLASH_UNLOCK_KEY1;
    FLASH->unlock = FLASH_UNLOCK_KEY2;
}

/**
 * @brief  unlock the flash spim controller.
 * @param  none
 * @retval none
 */
// static void flash_spim_unlock(void)
// {
//     FLASH->unlock3 = FLASH_UNLOCK_KEY1;
//     FLASH->unlock3 = FLASH_UNLOCK_KEY2;
// }

/**
 * @brief  lock the flash controller.
 * @param  none
 * @retval none
 */
static void flash_lock(void)
{
    FLASH->ctrl_bit.oplk = TRUE;
}

/**
 * @brief  lock the flash spim controller.
 * @param  none
 * @retval none
 */
// static void flash_spim_lock(void)
// {
//     FLASH->ctrl3_bit.oplk = TRUE;
// }

/**
 * @brief  erase a specified flash sector.
 * @param  raddr: the sector address to be erased.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
static flash_status_type flash_sector_erase(uint32_t raddr)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* spim : external flash */
    if (raddr >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.secers = TRUE;
        FLASH->addr3 = raddr;
        FLASH->ctrl3_bit.erstr = TRUE;

        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl3_bit.secers = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
    }
    else
    {
        FLASH->ctrl_bit.secers = TRUE;
        FLASH->addr = raddr;
        FLASH->ctrl_bit.erstr = TRUE;

        /* wait for operation to be completed */
        status = flash_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl_bit.secers = FALSE;
    }
    /* return the erase status */
    return status;
}

/**
 * @brief  erase flash all internal sectors.
 * @param  none
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_internal_all_erase(void)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     FLASH->ctrl_bit.bankers = TRUE;
//     FLASH->ctrl_bit.erstr = TRUE;

//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(ERASE_TIMEOUT);

//     /* disable the bankers bit */
//     FLASH->ctrl_bit.bankers = FALSE;

//     /* return the erase status */
//     return status;
// }

/**
 * @brief  erase flash spim sectors.
 * @param  none
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_spim_all_erase(void)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     FLASH->ctrl3_bit.chpers = TRUE;
//     FLASH->ctrl3_bit.erstr = TRUE;

//     /* wait for operation to be completed */
//     status = flash_spim_operation_wait_for(SPIM_ERASE_TIMEOUT);

//     /* disable the chpers bit */
//     FLASH->ctrl3_bit.chpers = FALSE;

//     /* dummy read */
//     flash_spim_dummy_read();

//     /* return the erase status */
//     return status;
// }

/**
 * @brief  erase the flash user system data.
 * @note   this functions erases all user system data except the fap byte.
 * @param  none
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_user_system_data_erase(void)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;
//     uint16_t fap_val = FAP_RELIEVE_KEY;
//     /* get the flash access protection status */
//     if (flash_fap_status_get() != RESET)
//     {
//         fap_val = 0x0000;
//     }

//     /* unlock the user system data */
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY1;
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY2;
//     while (FLASH->ctrl_bit.usdulks == RESET)
//         ;

//     /* erase the user system data */
//     FLASH->ctrl_bit.usders = TRUE;
//     FLASH->ctrl_bit.erstr = TRUE;

//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(ERASE_TIMEOUT);

//     /* disable the usders bit */
//     FLASH->ctrl_bit.usders = FALSE;

//     if ((status == FLASH_OPERATE_DONE) && (fap_val == FAP_RELIEVE_KEY))
//     {
//         /* enable the user system data programming operation */
//         FLASH->ctrl_bit.usdprgm = TRUE;

//         /* restore the last flash access protection value */
//         USD->fap = (uint16_t)fap_val;

//         /* wait for operation to be completed */
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//         /*disable the usdprgm bit */
//         FLASH->ctrl_bit.usdprgm = FALSE;
//     }

//     /* return the erase status */
//     return status;
// }

/**
 * @brief  program a word at a specified address.
 * @param  address: specifies the address to be programmed, word alignment is recommended.
 * @param  data: specifies the data to be programmed.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_word_program(uint32_t address, uint32_t data)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;
//     /* spim : external flash */
//     if (address >= FLASH_SPIM_START_ADDR)
//     {
//         FLASH->ctrl3_bit.fprgm = TRUE;
//         *(__IO uint32_t *)address = data;
//         /* wait for operation to be completed */
//         status = flash_spim_operation_wait_for(SPIM_PROGRAMMING_TIMEOUT);

//         /* disable the fprgm bit */
//         FLASH->ctrl3_bit.fprgm = FALSE;

//         /* dummy read */
//         flash_spim_dummy_read();
//     }
//     else
//     {
//         FLASH->ctrl_bit.fprgm = TRUE;
//         *(__IO uint32_t *)address = data;
//         /* wait for operation to be completed */
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//         /* disable the fprgm bit */
//         FLASH->ctrl_bit.fprgm = FALSE;
//     }
//     /* return the program status */
//     return status;
// }

/**
 * @brief  program a halfword at a specified address.
 * @param  address: specifies the address to be programmed, halfword alignment is recommended.
 * @param  data: specifies the data to be programmed.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
static flash_status_type flash_halfword_program(uint32_t address, uint16_t data)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* spim : external flash */
    if (address >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.fprgm = TRUE;
        *(__IO uint16_t *)address = data;
        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_PROGRAMMING_TIMEOUT);

        /* disable the fprgm bit */
        FLASH->ctrl3_bit.fprgm = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
    }
    else
    {
        FLASH->ctrl_bit.fprgm = TRUE;
        *(__IO uint16_t *)address = data;
        /* wait for operation to be completed */
        status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

        /* disable the fprgm bit */
        FLASH->ctrl_bit.fprgm = FALSE;
    }
    /* return the program status */
    return status;
}

/**
 * @brief  program a byte at a specified address.
 * @note   this function cannot be used to program spim.
 * @param  address: specifies the address to be programmed.
 * @param  data: specifies the data to be programmed.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_byte_program(uint32_t address, uint8_t data)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     FLASH->ctrl_bit.fprgm = TRUE;
//     *(__IO uint8_t *)address = data;
//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//     /* disable the fprgm bit */
//     FLASH->ctrl_bit.fprgm = FALSE;

//     /* return the program status */
//     return status;
// }

/**
 * @brief  program a halfword at a specified user system data address.
 * @param  address: specifies the address to be programmed.
 * @param  data: specifies the data to be programmed.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_user_system_data_program(uint32_t address, uint8_t data)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     if (address == USD_BASE)
//     {
//         if (data != 0xA5)
//             return FLASH_OPERATE_DONE;
//     }

//     /* unlock the user system data */
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY1;
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY2;
//     while (FLASH->ctrl_bit.usdulks == RESET)
//         ;

//     /* enable the user system data programming operation */
//     FLASH->ctrl_bit.usdprgm = TRUE;
//     *(__IO uint16_t *)address = data;

//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//     /* disable the usdprgm bit */
//     FLASH->ctrl_bit.usdprgm = FALSE;

//     /* return the user system data program status */
//     return status;
// }

/**
 * @brief  config erase/program protection for the desired sectors.
 * @param  sector_bits:
 *         the pointer of the address of the sectors to be erase/program protected.
 *         general every bit is used to protect the 4KB bytes, and the last one bit
 *         is used to protect the rest.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_epp_set(uint32_t *sector_bits)
// {
//     uint16_t epp_data[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
//     flash_status_type status = FLASH_OPERATE_DONE;
//     sector_bits[0] = (uint32_t)(~sector_bits[0]);
//     epp_data[0] = (uint16_t)((sector_bits[0] >> 0) & 0xFF);
//     epp_data[1] = (uint16_t)((sector_bits[0] >> 8) & 0xFF);
//     epp_data[2] = (uint16_t)((sector_bits[0] >> 16) & 0xFF);
//     epp_data[3] = (uint16_t)((sector_bits[0] >> 24) & 0xFF);

//     /* unlock the user system data */
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY1;
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY2;
//     while (FLASH->ctrl_bit.usdulks == RESET)
//         ;

//     FLASH->ctrl_bit.usdprgm = TRUE;
//     USD->epp0 = epp_data[0];
//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//     if (status == FLASH_OPERATE_DONE)
//     {
//         USD->epp1 = epp_data[1];
//         /* wait for operation to be completed */
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);
//     }
//     if (status == FLASH_OPERATE_DONE)
//     {
//         USD->epp2 = epp_data[2];
//         /* wait for operation to be completed */
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);
//     }
//     if (status == FLASH_OPERATE_DONE)
//     {
//         USD->epp3 = epp_data[3];
//         /* wait for operation to be completed */
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);
//     }
//     /* disable the usdprgm bit */
//     FLASH->ctrl_bit.usdprgm = FALSE;

//     /* return the erase/program protection operation status */
//     return status;
// }

/**
 * @brief  return the flash erase/program protection status.
 * @param  sector_bits: pointer to get the epps register.
 * @retval none
 */
// static void flash_epp_status_get(uint32_t *sector_bits)
// {
//     /* return the flash erase/program protection register value */
//     sector_bits[0] = (uint32_t)(FLASH->epps);
// }

/**
 * @brief  enable or disable the flash access protection.
 * @note   if the user has already programmed the other user system data before calling
 *         this function, must re-program them since this function erase all user system data.
 * @param  new_state: new state of the flash access protection.
 *         this parameter can be: TRUE or FALSE.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_fap_enable(confirm_state new_state)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     /* unlock the user system data */
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY1;
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY2;
//     while (FLASH->ctrl_bit.usdulks == RESET)
//         ;

//     FLASH->ctrl_bit.usders = TRUE;
//     FLASH->ctrl_bit.erstr = TRUE;
//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(ERASE_TIMEOUT);

//     /* disable the usders bit */
//     FLASH->ctrl_bit.usders = FALSE;

//     if (status == FLASH_OPERATE_DONE)
//     {
//         if (new_state == FALSE)
//         {
//             /* enable the user system data programming operation */
//             FLASH->ctrl_bit.usdprgm = TRUE;
//             USD->fap = FAP_RELIEVE_KEY;

//             /* Wait for operation to be completed */
//             status = flash_operation_wait_for(ERASE_TIMEOUT);

//             /* disable the usdprgm bit */
//             FLASH->ctrl_bit.usdprgm = FALSE;
//         }
//     }

//     /* return the flash access protection operation status */
//     return status;
// }

/**
 * @brief  check the flash access protection status.
 * @param  none
 * @retval flash access protection status(SET or RESET)
 */
// static flag_status flash_fap_status_get(void)
// {
//     return (flag_status)FLASH->usd_bit.fap;
// }

/**
 * @brief  program the flash system setting byte in usd: wdt_ato_en / depslp_rst / stdby_rst.
 * @param  usd_ssb: the system setting byte
 * @note   this parameter usd_ssb must contain a combination of all the following 3 types of data
 *         type 1: wdt_ato_en, select the wdt auto start
 *         this data can be one of the following values:
 *         - USD_WDT_ATO_DISABLE: disable wdt auto start
 *         - USD_WDT_ATO_ENABLE: enable wdt auto start
 *         type 2: depslp_rst, reset event when entering deepsleep mode.
 *         this data can be one of the following values:
 *         - USD_DEPSLP_NO_RST: no reset generated when entering in deepsleep
 *         - USD_DEPSLP_RST: reset generated when entering in deepsleep
 *         type 3: stdby_rst, reset event when entering standby mode.
 *         this data can be one of the following values:
 *         - USD_STDBY_NO_RST: no reset generated when entering in standby
 *         - USD_STDBY_RST: reset generated when entering in standby
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_ssb_set(uint8_t usd_ssb)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;

//     /* unlock the user system data */
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY1;
//     FLASH->usd_unlock = FLASH_UNLOCK_KEY2;
//     while (FLASH->ctrl_bit.usdulks == RESET)
//         ;

//     /* enable the user system data programming operation */
//     FLASH->ctrl_bit.usdprgm = TRUE;

//     USD->ssb = usd_ssb;
//     /* wait for operation to be completed */
//     status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);

//     /* disable the usdprgm bit */
//     FLASH->ctrl_bit.usdprgm = FALSE;

//     /* return the user system data program status */
//     return status;
// }

/**
 * @brief  return the flash system setting byte status.
 * @param  none
 * @retval values from flash_usd register: wdt_ato_en(bit0), depslp_rst(bit1) and stdby_rst(bit2).
 */
// static uint8_t flash_ssb_status_get(void)
// {
//     /* return the system setting byte status */
//     return (uint8_t)(FLASH->usd >> 2);
// }

/**
 * @brief  enable or disable the specified flash interrupts.
 * @param  flash_int: specifies the flash interrupt sources to be enabled or disabled.
 *         this parameter can be any combination of the following values:
 *         - FLASH_ERR_INT
 *         - FLASH_ODF_INT
 *         - FLASH_SPIM_ERR_INT
 *         - FLASH_SPIM_ODF_INT
 * @param  new_state: new state of the specified flash interrupts.
 *         this parameter can be: TRUE or FALSE.
 * @retval none
 */
// static void flash_interrupt_enable(uint32_t flash_int, confirm_state new_state)
// {
//     if (flash_int & FLASH_ERR_INT)
//         FLASH->ctrl_bit.errie = new_state;
//     if (flash_int & FLASH_ODF_INT)
//         FLASH->ctrl_bit.odfie = new_state;
//     if (flash_int & FLASH_SPIM_ERR_INT)
//         FLASH->ctrl3_bit.errie = new_state;
//     if (flash_int & FLASH_SPIM_ODF_INT)
//         FLASH->ctrl3_bit.odfie = new_state;
// }

/**
 * @brief  select spim supports extended spi flash chip model.
 * @param  mode: the extended spi flash model
 * @retval none
 */
// static void flash_spim_model_select(flash_spim_model_type mode)
// {
//     FLASH->select = mode;

//     /* dummy read */
//     flash_spim_dummy_read();
// }

/**
 * @brief  set the range of encryption in spim flash.
 *         when the address is larger than this value, the writing data will be
 *         directly written to spim without encryption.
 * @param  decode_address: the end address of encrypted data in spim
 * @retval none
 */
// static void flash_spim_encryption_range_set(uint32_t decode_address)
// {
//     FLASH->da = decode_address;
// }

/**
 * @brief  operate the flash spim dummy read.
 * @param  none
 * @retval none
 */
static void flash_spim_dummy_read(void)
{
    UNUSED(*(__IO uint32_t *)FLASH_SPIM_START_ADDR);
    UNUSED(*(__IO uint32_t *)(FLASH_SPIM_START_ADDR + 0x1000));
    UNUSED(*(__IO uint32_t *)(FLASH_SPIM_START_ADDR + 0x2000));
}

/**
 * @brief  mass program for flash spim.
 * @param  address: specifies the start address to be programmed, word or halfword alignment is recommended.
 * @param  buf: specifies the pointer of data to be programmed.
 * @param  cnt: specifies the data counter to be programmed.
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_spim_mass_program(uint32_t address, uint8_t *buf, uint32_t cnt)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;
//     uint32_t index, temp_offset;
//     if (address >= FLASH_SPIM_START_ADDR)
//     {
//         temp_offset = cnt % 4;
//         if ((temp_offset != 0) && (temp_offset != 2))
//             return status;

//         FLASH->ctrl3_bit.fprgm = TRUE;
//         for (index = 0; index < cnt / 4; index++)
//         {
//             *(__IO uint32_t *)(address + index * 4) = *(uint32_t *)(buf + index * 4);
//             /* wait for operation to be completed */
//             status = flash_spim_operation_wait_for(SPIM_PROGRAMMING_TIMEOUT);
//             if (status != FLASH_OPERATE_DONE)
//                 return status;
//         }
//         if (temp_offset == 2)
//         {
//             *(__IO uint16_t *)(address + index * 4) = *(uint16_t *)(buf + index * 4);
//             /* wait for operation to be completed */
//             status = flash_spim_operation_wait_for(SPIM_PROGRAMMING_TIMEOUT);
//         }
//         /* disable the fprgm bit */
//         FLASH->ctrl3_bit.fprgm = FALSE;

//         /* dummy read */
//         flash_spim_dummy_read();
//     }

//     /* return the program status */
//     return status;
// }

/**
 * @brief  enable security library function.
 * @param  pwd: slib password
 *         start_sector: security library start sector
 *         data_start_sector: security library d-bus area start sector
 *         end_sector: security library end sector
 * @retval status: the returned value can be: FLASH_PROGRAM_ERROR,
 *         FLASH_EPP_ERROR, FLASH_OPERATE_DONE or FLASH_OPERATE_TIMEOUT.
 */
// static flash_status_type flash_slib_enable(uint32_t pwd, uint16_t start_sector, uint16_t data_start_sector, uint16_t end_sector)
// {
//     uint32_t slib_range;
//     flash_status_type status = FLASH_OPERATE_DONE;

//     /*check range param limits*/
//     if ((start_sector >= data_start_sector) || ((data_start_sector > end_sector) && (data_start_sector != 0x7FF)) || (start_sector > end_sector))
//         return FLASH_PROGRAM_ERROR;

//     /* unlock slib cfg register */
//     FLASH->slib_unlock = SLIB_UNLOCK_KEY;
//     while (FLASH->slib_misc_sts_bit.slib_ulkf == RESET)
//         ;

//     slib_range = ((uint32_t)(data_start_sector << 11) & FLASH_SLIB_DATA_START_SECTOR) |
//                  ((uint32_t)(end_sector << 22) & FLASH_SLIB_END_SECTOR) |
//                  (start_sector & FLASH_SLIB_START_SECTOR);
//     /* configure slib, set pwd and range */
//     FLASH->slib_set_pwd = pwd;
//     status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);
//     if (status == FLASH_OPERATE_DONE)
//     {
//         FLASH->slib_set_range = slib_range;
//         status = flash_operation_wait_for(PROGRAMMING_TIMEOUT);
//     }

//     return status;
// }

/**
 * @brief  disable slib when slib enabled.
 * @param  pwd: slib password
 * @retval success or error
 */
// static error_status flash_slib_disable(uint32_t pwd)
// {
//     flash_status_type status = FLASH_OPERATE_DONE;
//     /* write password to disable slib */
//     FLASH->slib_pwd_clr = pwd;

//     status = flash_operation_wait_for(ERASE_TIMEOUT);
//     if (status == FLASH_OPERATE_DONE)
//     {
//         if (FLASH->slib_misc_sts_bit.slib_pwd_ok)
//             return SUCCESS;
//         else
//             return ERROR;
//     }
//     return ERROR;
// }

/**
 * @brief  get remaining count of slib(range: 256~0).
 * @param  none
 * @retval uint32_t
 */
// static uint32_t flash_slib_remaining_count_get(void)
// {
//     return (uint32_t)FLASH->slib_misc_sts_bit.slib_rcnt;
// }

/**
 * @brief  get the slib state.
 * @param  none
 * @retval SET or RESET
 */
// static flag_status flash_slib_state_get(void)
// {
//     if (FLASH->slib_sts0_bit.slib_enf)
//         return SET;
//     else
//         return RESET;
// }

/**
 * @brief  get the start sector of slib.
 * @param  none
 * @retval uint16_t
 */
// static uint16_t flash_slib_start_sector_get(void)
// {
//     return (uint16_t)FLASH->slib_sts1_bit.slib_ss;
// }

/**
 * @brief  get the data start sector of slib.
 * @param  none
 * @retval uint16_t
 */
// static uint16_t flash_slib_datastart_sector_get(void)
// {
//     return (uint16_t)FLASH->slib_sts1_bit.slib_dat_ss;
// }

/**
 * @brief  get the end sector of slib.
 * @param  none
 * @retval uint16_t
 */
// static uint16_t flash_slib_end_sector_get(void)
// {
//     return (uint16_t)FLASH->slib_sts1_bit.slib_es;
// }

/**
 * @brief  flash crc calibration in main block.
 * @param  start_sector: crc calibration start sector number
 *         sector_cnt: crc calibration sector count
 * @retval uint32: crc calibration result
 */
// static uint32_t flash_crc_calibrate(uint32_t start_sector, uint32_t sector_cnt)
// {
//     FLASH->crc_ctrl_bit.crc_ss = start_sector;
//     FLASH->crc_ctrl_bit.crc_sn = sector_cnt;
//     FLASH->crc_ctrl_bit.crc_strt = TRUE;
//     flash_operation_wait_for(OPERATION_TIMEOUT);
//     return FLASH->crc_chkr;
// }

/**
 * @brief  read data using halfword mode
 * @param  read_addr: the address of reading
 * @param  p_buffer: the buffer of reading data
 * @param  num_read: the number of reading data
 * @retval none
 */
void flash_read(uint32_t read_addr, uint16_t *p_buffer, uint16_t num_read)
{
    uint16_t i;
    for (i = 0; i < num_read; i++)
    {
        p_buffer[i] = *(uint16_t *)(read_addr);
        read_addr += 2;
    }
}

/**
 * @brief  write data using halfword mode without checking
 * @param  write_addr: the address of writing
 * @param  p_buffer: the buffer of writing data
 * @param  num_write: the number of writing data
 * @retval result
 */
error_status flash_write_nocheck(uint32_t write_addr, uint16_t *p_buffer, uint16_t num_write)
{
    uint16_t i;
    flash_status_type status = FLASH_OPERATE_DONE;
    for (i = 0; i < num_write; i++)
    {
        status = flash_halfword_program(write_addr, p_buffer[i]);
        if (status != FLASH_OPERATE_DONE)
            return ERROR;
        write_addr += 2;
    }
    return SUCCESS;
}
#endif
//----------------------------------------------------------------
int bMcuFlashInit()
{
    return 0;
}

int bMcuFlashUnlock()
{
    int retval      = 0;
    FLASH->unlock = FLASH_UNLOCK_KEY1;
    FLASH->unlock = FLASH_UNLOCK_KEY2;
    return retval;
}

int bMcuFlashLock()
{
    int retval = 0;
    FLASH->ctrl_bit.oplk = 1;
    return retval;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    int retval = 0;
    flash_status_type status = FLASH_OPERATE_DONE;

    raddr = FLASH_BASE_ADDR + raddr;

    if (raddr >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.secers = TRUE;
        FLASH->addr3 = raddr;
        FLASH->ctrl3_bit.erstr = TRUE;

        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl3_bit.secers = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
    }
    else
    {
        FLASH->ctrl_bit.secers = TRUE;
        FLASH->addr = raddr;
        FLASH->ctrl_bit.erstr = TRUE;

        /* wait for operation to be completed */
        status = flash_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl_bit.secers = FALSE;
    }

    if (status == FLASH_OPERATE_DONE)
    {
        retval = 0;
    }
    else
    {
        retval = -1;
    }

    return retval;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    uint32_t offset_addr;
    uint32_t sector_position;
    uint16_t sector_offset;
    uint16_t sector_remain;
    uint16_t i;
    flash_status_type status = FLASH_OPERATE_DONE;
    uint32_t write_addr = FLASH_BASE + raddr;
    uint16_t num_write = (len + 1) / 2;
    uint16_t *p_buffer = (uint16_t *)pbuf;

    flash_unlock();
    offset_addr = raddr;
    sector_position = offset_addr / SECTOR_SIZE;
    sector_offset = (offset_addr % SECTOR_SIZE) / 2;
    sector_remain = SECTOR_SIZE / 2 - sector_offset;
    if (num_write <= sector_remain)
        sector_remain = num_write;
    while (1)
    {
        flash_read(sector_position * SECTOR_SIZE + FLASH_BASE, flash_buf, SECTOR_SIZE / 2);
        for (i = 0; i < sector_remain; i++)
        {
            if (flash_buf[sector_offset + i] != 0xFFFF)
                break;
        }
        if (i < sector_remain)
        {
            /* wait for operation to be completed */
            status = flash_operation_wait_for(ERASE_TIMEOUT);

            if ((status == FLASH_PROGRAM_ERROR) || (status == FLASH_EPP_ERROR))
                flash_flag_clear(FLASH_PRGMERR_FLAG | FLASH_EPPERR_FLAG);
            else if (status == FLASH_OPERATE_TIMEOUT)
                return -1;
            status = flash_sector_erase(sector_position * SECTOR_SIZE + FLASH_BASE);
            if (status != FLASH_OPERATE_DONE)
                return -2;
            for (i = 0; i < sector_remain; i++)
            {
                flash_buf[i + sector_offset] = p_buffer[i];
            }
            if (flash_write_nocheck(sector_position * SECTOR_SIZE + FLASH_BASE, flash_buf, SECTOR_SIZE / 2) != SUCCESS)
                return -3;
        }
        else
        {
            if (flash_write_nocheck(write_addr, p_buffer, sector_remain) != SUCCESS)
                return -4;
        }
        if (num_write == sector_remain)
            break;
        else
        {
            sector_position++;
            sector_offset = 0;
            p_buffer += sector_remain;
            write_addr += (sector_remain * 2);
            num_write -= sector_remain;
            if (num_write > (SECTOR_SIZE / 2))
                sector_remain = SECTOR_SIZE / 2;
            else
                sector_remain = num_write;
        }
    }
    flash_lock();
    return len;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (pbuf == NULL || (raddr + FLASH_BASE_ADDR + len) > (FLASH_MAX_SIZE + FLASH_BASE_ADDR))
    {
        return -1;
    }
    raddr = FLASH_BASE_ADDR + raddr;
    memcpy(pbuf, (const uint8_t *)raddr, len);
    return len;
}

uint32_t bMcuFlashSectorSize()
{
    return FLASH_PAGE_SIZE;
}

uint32_t bMcuFlashChipSize()
{
    return FLASH_MAX_SIZE;
}

#endif

/************************ Copyright (c) 2021 Bean *****END OF FILE****/
