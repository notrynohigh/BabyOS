/**
 * \file        mcu_at32f403axx_flash.c
 * \version     v0.0.1
 * \date        2023-11-24
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */
/*Includes ----------------------------------------------*/
#include <string.h>

#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if defined(AT32F403Axx)
#pragma anon_unions       // 在使用匿名联合的地方添加这个指令
#define UNUSED(x) (void)x /* to avoid gcc/g++ warnings */
#define __IO volatile     /*!< Defines 'read / write' permissions */

/*************** AT32F403A_407_Firmware start ***************/
#define SECTOR_SIZE 2048 /* this parameter depends on the specific model of the chip */
#define FLASH_MAX_SIZE (1024 * 1024)

uint16_t flash_buf[SECTOR_SIZE / 2];

#define PERIPH_BASE ((uint32_t)0x40000000)
#define AHBPERIPH_BASE (PERIPH_BASE + 0x20000)
#define FLASH_REG_BASE (AHBPERIPH_BASE + 0x2000)
#define FLASH_UNLOCK_KEY1 ((uint32_t)0x45670123) /*!< flash operation unlock order key1 */
#define FLASH_UNLOCK_KEY2 ((uint32_t)0xCDEF89AB) /*!< flash operation unlock order key2 */
#define FLASH_BASE ((uint32_t)0x08000000)
#define FLASH_BANK1_START_ADDR ((uint32_t)0x08000000) /*!< flash start address of bank1 */
#define FLASH_BANK1_END_ADDR ((uint32_t)0x0807FFFF)   /*!< flash end address of bank1 */
#define FLASH_BANK2_START_ADDR ((uint32_t)0x08080000) /*!< flash start address of bank2 */
#define FLASH_BANK2_END_ADDR ((uint32_t)0x080FFFFF)   /*!< flash end address of bank2 */
#define FLASH_SPIM_START_ADDR ((uint32_t)0x08400000)  /*!< flash start address of spim */
#define ERASE_TIMEOUT ((uint32_t)0x40000000)          /*!< internal flash erase operation timeout */
#define PROGRAMMING_TIMEOUT                                                                     \
    ((uint32_t)0x00100000)                        /*!< internal flash program operation timeout \
                                                   */
#define SPIM_ERASE_TIMEOUT ((uint32_t)0xFFFFFFFF) /*!< spim erase operation timeout */
#define SPIM_PROGRAMMING_TIMEOUT ((uint32_t)0x00100000) /*!< spim program operation timeout */
#define OPERATION_TIMEOUT ((uint32_t)0x10000000)        /*!< flash common operation timeout */

#define FLASH_BANK1_OBF_FLAG ((uint32_t)0x00000001)     /*!< flash bank1 operate busy flag */
#define FLASH_BANK1_ODF_FLAG ((uint32_t)0x00000020)     /*!< flash bank1 operate done flag */
#define FLASH_BANK1_PRGMERR_FLAG ((uint32_t)0x00000004) /*!< flash bank1 program error flag */
#define FLASH_BANK1_EPPERR_FLAG \
    ((uint32_t)0x00000010) /*!< flash bank1 erase/program protection error flag */
#define FLASH_BANK2_OBF_FLAG ((uint32_t)0x10000001)     /*!< flash bank2 operate busy flag */
#define FLASH_BANK2_ODF_FLAG ((uint32_t)0x10000020)     /*!< flash bank2 operate done flag */
#define FLASH_BANK2_PRGMERR_FLAG ((uint32_t)0x10000004) /*!< flash bank2 program error flag */
#define FLASH_BANK2_EPPERR_FLAG \
    ((uint32_t)0x10000010) /*!< flash bank2 erase/program protection error flag */
#define FLASH_SPIM_OBF_FLAG ((uint32_t)0x20000001)     /*!< flash spim operate busy flag */
#define FLASH_SPIM_ODF_FLAG ((uint32_t)0x20000020)     /*!< flash spim operate done flag */
#define FLASH_SPIM_PRGMERR_FLAG ((uint32_t)0x20000004) /*!< flash spim program error flag */
#define FLASH_SPIM_EPPERR_FLAG \
    ((uint32_t)0x20000010) /*!< flash spim erase/program protection error flag */
#define FLASH_USDERR_FLAG ((uint32_t)0x40000001)    /*!< flash user system data error flag */
#define FLASH_OBF_FLAG FLASH_BANK1_OBF_FLAG         /*!< flash operate busy flag */
#define FLASH_ODF_FLAG FLASH_BANK1_ODF_FLAG         /*!< flash operate done flag */
#define FLASH_PRGMERR_FLAG FLASH_BANK1_PRGMERR_FLAG /*!< flash program error flag */
#define FLASH_EPPERR_FLAG                                                  \
    FLASH_BANK1_EPPERR_FLAG /*!< flash erase/program protection error flag \
                             */
typedef enum
{
    FALSE = 0,
    TRUE  = !FALSE
} confirm_state;

typedef enum
{
    RESET = 0,
    SET   = !RESET
} flag_status;

typedef enum
{
    ERROR   = 0,
    SUCCESS = !ERROR
} error_status;

typedef enum
{
    FLASH_OPERATE_BUSY    = 0x00, /*!< flash status is operate busy */
    FLASH_PROGRAM_ERROR   = 0x01, /*!< flash status is program error */
    FLASH_EPP_ERROR       = 0x02, /*!< flash status is epp error */
    FLASH_OPERATE_DONE    = 0x03, /*!< flash status is operate done */
    FLASH_OPERATE_TIMEOUT = 0x04  /*!< flash status is operate timeout */
} flash_status_type;

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
            __IO uint32_t btopt : 1;      /* [5] */
            __IO uint32_t reserved1 : 4;  /* [9:6] */
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
     * @brief flash reserved2 register, offset:0x40~0x24
     */
    __IO uint32_t reserved2[8];

    /**
     * @brief flash unlock2 register, offset:0x44
     */
    union
    {
        __IO uint32_t unlock2;
        struct
        {
            __IO uint32_t ukval : 32; /* [31:0] */
        } unlock2_bit;
    };

    /**
     * @brief flash reserved3 register, offset:0x48
     */
    __IO uint32_t reserved3;

    /**
     * @brief flash sts2 register, offset:0x4C
     */
    union
    {
        __IO uint32_t sts2;
        struct
        {
            __IO uint32_t obf : 1;        /* [0] */
            __IO uint32_t reserved1 : 1;  /* [1] */
            __IO uint32_t prgmerr : 1;    /* [2] */
            __IO uint32_t reserved2 : 1;  /* [3] */
            __IO uint32_t epperr : 1;     /* [4] */
            __IO uint32_t odf : 1;        /* [5] */
            __IO uint32_t reserved3 : 26; /* [31:6] */
        } sts2_bit;
    };

    /**
     * @brief flash ctrl2 register, offset:0x50
     */
    union
    {
        __IO uint32_t ctrl2;
        struct
        {
            __IO uint32_t fprgm : 1;      /* [0] */
            __IO uint32_t secers : 1;     /* [1] */
            __IO uint32_t bankers : 1;    /* [2] */
            __IO uint32_t reserved1 : 3;  /* [5:3] */
            __IO uint32_t erstr : 1;      /* [6] */
            __IO uint32_t oplk : 1;       /* [7] */
            __IO uint32_t reserved2 : 2;  /* [9:8] */
            __IO uint32_t errie : 1;      /* [10] */
            __IO uint32_t reserved3 : 1;  /* [11] */
            __IO uint32_t odfie : 1;      /* [12] */
            __IO uint32_t reserved4 : 19; /* [31:13] */
        } ctrl2_bit;
    };

    /**
     * @brief flash addr2 register, offset:0x54
     */
    union
    {
        __IO uint32_t addr2;
        struct
        {
            __IO uint32_t fa : 32; /* [31:0] */
        } addr2_bit;
    };

    /**
     * @brief flash reserved4 register, offset:0x80~0x58
     */
    __IO uint32_t reserved4[11];

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

} flash_type;

#define FLASH ((flash_type *)FLASH_REG_BASE)

static flash_status_type flash_bank1_operation_status_get(void)
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

static flash_status_type flash_bank2_operation_status_get(void)
{
    flash_status_type flash_status = FLASH_OPERATE_DONE;
    if (FLASH->sts2_bit.obf != RESET)
    {
        flash_status = FLASH_OPERATE_BUSY;
    }
    else if (FLASH->sts2_bit.prgmerr != RESET)
    {
        flash_status = FLASH_PROGRAM_ERROR;
    }
    else if (FLASH->sts2_bit.epperr != RESET)
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

static flash_status_type flash_bank1_operation_wait_for(uint32_t time_out)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* check for the flash status */
    status = flash_bank1_operation_status_get();

    while ((status == FLASH_OPERATE_BUSY) && (time_out != 0x00))
    {
        status = flash_bank1_operation_status_get();
        time_out--;
    }
    if (time_out == 0x00)
    {
        status = FLASH_OPERATE_TIMEOUT;
    }
    /* return the operation status */
    return status;
}

static flash_status_type flash_bank2_operation_wait_for(uint32_t time_out)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    /* check for the flash status */
    status = flash_bank2_operation_status_get();

    while ((status == FLASH_OPERATE_BUSY) && (time_out != 0x00))
    {
        status = flash_bank2_operation_status_get();
        time_out--;
    }
    if (time_out == 0x00)
    {
        status = FLASH_OPERATE_TIMEOUT;
    }
    /* return the operation status */
    return status;
}

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
        case 0x10000000:
            FLASH->sts2 = flash_flag;
            break;
        case 0x20000000:
            FLASH->sts3 = flash_flag;
            break;
        default:
            break;
    }
}

static void flash_spim_dummy_read(void)
{
    UNUSED(*(__IO uint32_t *)FLASH_SPIM_START_ADDR);
    UNUSED(*(__IO uint32_t *)(FLASH_SPIM_START_ADDR + 0x1000));
    UNUSED(*(__IO uint32_t *)(FLASH_SPIM_START_ADDR + 0x2000));
}

static flash_status_type flash_sector_erase(uint32_t sector_address)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    if ((sector_address >= FLASH_BANK1_START_ADDR) && (sector_address <= FLASH_BANK1_END_ADDR))
    {
        FLASH->ctrl_bit.secers = TRUE;
        FLASH->addr            = sector_address;
        FLASH->ctrl_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_bank1_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl_bit.secers = FALSE;
    }
    else if ((sector_address >= FLASH_BANK2_START_ADDR) && (sector_address <= FLASH_BANK2_END_ADDR))
    {
        FLASH->ctrl2_bit.secers = TRUE;
        FLASH->addr2            = sector_address;
        FLASH->ctrl2_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_bank2_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl2_bit.secers = FALSE;
    }
    /* spim : external flash */
    else if (sector_address >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.secers = TRUE;
        FLASH->addr3            = sector_address;
        FLASH->ctrl3_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl3_bit.secers = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
    }

    /* return the erase status */
    return status;
}

static flash_status_type flash_halfword_program(uint32_t address, uint16_t data)
{
    flash_status_type status = FLASH_OPERATE_DONE;
    if ((address >= FLASH_BANK1_START_ADDR) && (address <= FLASH_BANK1_END_ADDR))
    {
        FLASH->ctrl_bit.fprgm     = TRUE;
        *(__IO uint16_t *)address = data;
        /* wait for operation to be completed */
        status = flash_bank1_operation_wait_for(PROGRAMMING_TIMEOUT);

        /* disable the fprgm bit */
        FLASH->ctrl_bit.fprgm = FALSE;
    }
    else if ((address >= FLASH_BANK2_START_ADDR) && (address <= FLASH_BANK2_END_ADDR))
    {
        FLASH->ctrl2_bit.fprgm    = TRUE;
        *(__IO uint16_t *)address = data;
        /* wait for operation to be completed */
        status = flash_bank2_operation_wait_for(PROGRAMMING_TIMEOUT);

        /* disable the fprgm bit */
        FLASH->ctrl2_bit.fprgm = FALSE;
    }
    /* spim : external flash */
    else if (address >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.fprgm    = TRUE;
        *(__IO uint16_t *)address = data;
        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_PROGRAMMING_TIMEOUT);

        /* disable the fprgm bit */
        FLASH->ctrl3_bit.fprgm = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
    }

    /* return the program status */
    return status;
}

static error_status flash_write_nocheck(uint32_t write_addr, uint16_t *p_buffer, uint16_t num_write)
{
    uint16_t          i;
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

static void flash_read(uint32_t read_addr, uint16_t *p_buffer, uint16_t num_read)
{
    uint16_t i;
    for (i = 0; i < num_read; i++)
    {
        p_buffer[i] = *(uint16_t *)(read_addr);
        read_addr += 2;
    }
}

static void flash_unlock(void)
{
    FLASH->unlock  = FLASH_UNLOCK_KEY1;
    FLASH->unlock  = FLASH_UNLOCK_KEY2;
    FLASH->unlock2 = FLASH_UNLOCK_KEY1;
    FLASH->unlock2 = FLASH_UNLOCK_KEY2;
}

static void flash_lock(void)
{
    FLASH->ctrl_bit.oplk  = TRUE;
    FLASH->ctrl2_bit.oplk = TRUE;
}
/*************** AT32F403A_407_Firmware end ***************/
int bMcuFlashInit()
{
    return 0;
}

int bMcuFlashUnlock()
{
    int retval = 0;

    flash_unlock();

    return retval;
}

int bMcuFlashLock()
{
    int retval = 0;

    flash_lock();

    return retval;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    int retval = 0;
    flash_status_type status         = FLASH_OPERATE_DONE;
    uint32_t          sector_address = FLASH_BANK1_START_ADDR + raddr;

    if ((sector_address >= FLASH_BANK1_START_ADDR) && (sector_address <= FLASH_BANK1_END_ADDR))
    {
        FLASH->ctrl_bit.secers = TRUE;
        FLASH->addr            = sector_address;
        FLASH->ctrl_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_bank1_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl_bit.secers = FALSE;
    }
    else if ((sector_address >= FLASH_BANK2_START_ADDR) && (sector_address <= FLASH_BANK2_END_ADDR))
    {
        FLASH->ctrl2_bit.secers = TRUE;
        FLASH->addr2            = sector_address;
        FLASH->ctrl2_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_bank2_operation_wait_for(ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl2_bit.secers = FALSE;
    }
    /* spim : external flash */
    else if (sector_address >= FLASH_SPIM_START_ADDR)
    {
        FLASH->ctrl3_bit.secers = TRUE;
        FLASH->addr3            = sector_address;
        FLASH->ctrl3_bit.erstr  = TRUE;

        /* wait for operation to be completed */
        status = flash_spim_operation_wait_for(SPIM_ERASE_TIMEOUT);

        /* disable the secers bit */
        FLASH->ctrl3_bit.secers = FALSE;

        /* dummy read */
        flash_spim_dummy_read();
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
    uint32_t          offset_addr;
    uint32_t          sector_position;
    uint16_t          sector_offset;
    uint16_t          sector_remain;
    uint16_t          i;
    flash_status_type status     = FLASH_OPERATE_DONE;
    uint32_t          write_addr = FLASH_BASE + raddr;
    uint16_t          num_write  = (len + 1) / 2;
    uint16_t         *p_buffer   = (uint16_t *)pbuf;

    flash_unlock();
    offset_addr     = write_addr - FLASH_BASE;
    sector_position = offset_addr / SECTOR_SIZE;
    sector_offset   = (offset_addr % SECTOR_SIZE) / 2;
    sector_remain   = SECTOR_SIZE / 2 - sector_offset;
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
            if (flash_write_nocheck(sector_position * SECTOR_SIZE + FLASH_BASE, flash_buf,
                                    SECTOR_SIZE / 2) != SUCCESS)
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
    if (pbuf == NULL || (raddr + FLASH_BASE + len) > (FLASH_MAX_SIZE + FLASH_BASE))
    {
        return -1;
    }
    raddr = FLASH_BASE + raddr;
    memcpy(pbuf, (const uint8_t *)raddr, len);
    return len;
}

uint32_t bMcuFlashSectorSize()
{
    return SECTOR_SIZE;
}

uint32_t bMcuFlashChipSize()
{
    return FLASH_MAX_SIZE;
}

#endif
