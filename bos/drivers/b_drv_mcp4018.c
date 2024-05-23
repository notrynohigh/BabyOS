/**
 * \file        b_drv_mcp4018.c
 * \version     v0.0.1
 * \date        2024-05-21
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2024 by miniminiminini. All Rights Reserved.
 */

/* Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_mcp4018.h"
#include "utils/inc/b_util_log.h"
/**
 * \defgroup MCP4018_Private_FunctionPrototypes
 * \{
 */

/**
 * }
 */

/**
 * \defgroup MCP4018_Exported_Variables
 * \{
 */

/**
 * }
 */

/**
 * \defgroup MCP4018_Private_TypesDefinitions
 * \{
 */

/**
 * }
 */

/**
 * \defgroup MCP4018_Private_Defines
 * \{
 */
#define DRIVER_NAME MCP4018
/**
 * }
 */

/**
 * \defgroup MCP4018_Private_Macros
 * \{
 */

/**
 * }
 */

/**
 * \defgroup MCP4018_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bMCP4018_HalIf_t, DRIVER_NAME);

static uint8_t mcp4018_default_buf[1] = {MCP4018_DEFAULT_LEVEL};

/**
 * }
 */

/**
 * \defgroup MCP4018_Private_Functions
 * \{
 */
static int _bMCP4018Write(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    uint8_t tmp[1] = {0};
    bDRIVER_GET_HALIF(_if, bMCP4018_HalIf_t, pdrv);

    if (len != 1)
    {
        b_log_e("len!= 1!!!");
        return -1;
    }

    bHalI2CReadByte(_if, tmp, 1);

    if (tmp[0] == pbuf[0])
    {
        b_log("MCP4018=%d,no need to write", tmp[0]);
        return len;
    }

    bHalI2CWriteByte(_if, pbuf, 1);

    return len;
}

static int _bMCP4018Read(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    bDRIVER_GET_HALIF(_if, bMCP4018_HalIf_t, pdrv);

    if (len != 1)
    {
        b_log_e("len!= 1!!!");
        return -1;
    }

    bHalI2CReadByte(_if, pbuf, 1);

    return len;
}

static int _bMCP4018Open(bDriverInterface_t *pdrv)
{
    _bMCP4018Write(pdrv, 0, mcp4018_default_buf, 1);

    return 0;
}

static int _bMCP4018Close(bDriverInterface_t *pdrv)
{
    _bMCP4018Write(pdrv, 0, mcp4018_default_buf, 1);

    return 0;
}
/**
 * }
 */

/**
 * \defgroup MCP4018_Exported_Functions
 * \{
 */
int bMCP4018_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bMCP4018_Init);
    pdrv->read  = _bMCP4018Read;
    pdrv->write = _bMCP4018Write;
    pdrv->open  = _bMCP4018Open;
    pdrv->close = _bMCP4018Close;
    return 0;
}

bDRIVER_REG_INIT(B_DRIVER_MCP4018, bMCP4018_Init);
/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

/**
 * }
 */

/***** Copyright (c) 2024 miniminiminini *****END OF FILE*****/
