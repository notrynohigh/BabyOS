#include "ff.h" /* Obtains integer types */
#include "modules/inc/b_mod_fs.h"

/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/
#include "diskio.h" /* Declarations of disk functions */
/* Definitions of physical drive number for each drive */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))
    if (bFSGetPartitionState(pdrv) == 0)
    {
        return RES_OK;
    }
#endif
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE  pdrv,   /* Physical drive nmuber to identify the drive */
                  BYTE *buff,   /* Data buffer to store read data */
                  LBA_t sector, /* Start sector in LBA */
                  UINT  count   /* Number of sectors to read */
)
{
#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))
    const bFSPartition_t *tmp = NULL;
    int                   ret = 0;

    ret = bFSGetPartitionInfo(pdrv, &tmp);
    if (ret < 0)
    {
        return RES_ERROR;
    }
    ret = bFSPartitionRead(pdrv, sector * tmp->sector_size, buff, count * tmp->sector_size);
    if (ret >= 0)
    {
        return RES_OK;
    }
#endif
    return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE        pdrv,   /* Physical drive nmuber to identify the drive */
                   const BYTE *buff,   /* Data to be written */
                   LBA_t       sector, /* Start sector in LBA */
                   UINT        count   /* Number of sectors to write */
)
{
#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))
    const bFSPartition_t *tmp = NULL;
    int                   ret = 0;

    ret = bFSGetPartitionInfo(pdrv, &tmp);
    if (ret < 0)
    {
        return RES_ERROR;
    }
    ret = bFSPartitionErase(pdrv, sector * tmp->sector_size, count * tmp->sector_size);
    if (ret < 0)
    {
        return RES_ERROR;
    }
    ret = bFSPartitionWrite(pdrv, sector * tmp->sector_size, (uint8_t *)buff, count * tmp->sector_size);
    if (ret >= 0)
    {
        return RES_OK;
    }
    return RES_ERROR;
#else
    return RES_PARERR;
#endif
}

#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE  pdrv, /* Physical drive nmuber (0..) */
                   BYTE  cmd,  /* Control code */
                   void *buff  /* Buffer to send/receive control data */
)
{

#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))
    const bFSPartition_t *tmp = NULL;
    int                   ret = 0;

    ret = bFSGetPartitionInfo(pdrv, &tmp);
    if (ret < 0)
    {
        return RES_ERROR;
    }
    switch (cmd)
    {
        case GET_SECTOR_COUNT:
            ((LBA_t *)buff)[0] = tmp->total_size / tmp->sector_size;
            break;
        case GET_SECTOR_SIZE:
            ((WORD *)buff)[0] = tmp->sector_size;
            break;
        case GET_BLOCK_SIZE:
            ((WORD *)buff)[0] = FS_BLOCK_SIZE;
            break;
    }
    return RES_OK;
#else
    return RES_PARERR;
#endif
}
