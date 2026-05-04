/**
 *!
 * \file        b_mod_fs.c
 * \version     v0.0.1
 * \date        2020/06/02
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
#include "modules/inc/b_mod_fs.h"
#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))
#include <stdio.h>
#include <string.h>

#include "core/inc/b_core.h"
#include "core/inc/b_device.h"
#include "drivers/inc/b_driver_cmd.h"
#include "drivers/inc/b_drv_testflash.h"
#include "utils/inc/b_util_log.h"

/* Unified FS selection: each macro is 0 or 1, never both 1.
 * Both branches are always compiled — the preprocessor selects
 * the active implementation at build time. */
#if !defined(FS_FATFS) && !defined(FS_LITTLEFS)
#error "b_mod_fs.c: either FS_FATFS or FS_LITTLEFS must be defined"
#endif

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup FS
 * \{
 */

/**
 * \defgroup FS_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    uint8_t               used;
    uint8_t               fs_type; /* 0=FATFS, 1=LITTLEFS */
    char                  prefix[5];
    const bFSPartition_t *partition;
    union
    {
#if FS_FATFS_
        FATFS bfs;
#endif
#if FS_LITTLEFS_
        struct
        {
            struct lfs_config lfs_cfg;
            lfs_t             lfs;
            uint8_t           lfs_r_buf[LFS_CACHE_SIZE];
            uint8_t           lfs_w_buf[LFS_CACHE_SIZE];
            uint8_t           lfs_pre_buf[LFS_LOOKAHEAD_SIZE];
        } lfs_ctx;
#endif
    };
} bFS_t;
/**
 * \}
 */

/**
 * \defgroup FS_Private_Defines
 * \{
 */

#ifndef FS_MOUNT_NUMBER
#define FS_MOUNT_NUMBER (1)
#endif

/**
 * \}
 */

/**
 * \defgroup FS_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup FS_Private_Variables
 * \{
 */

const static bFSPartition_t *gpbPartition      = NULL;
static uint8_t               gbPartitionNumber = 0;

static bFS_t gbFSTable[FS_MOUNT_NUMBER];
static int   gbFSTableInited = 0;

/**
 * \}
 */

/**
 * \defgroup FS_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup FS_Private_Functions
 * \{
 */

static const bFSPartition_t *_bFSFindPartition(uint8_t index)
{
    int i = 0;
    for (i = 0; i < gbPartitionNumber; i++)
    {
        if (index == gpbPartition[i].index)
        {
            return &gpbPartition[i];
        }
    }
    return NULL;
}

static bFS_t *_bFSFindMounted(uint8_t index)
{
    if (!gbFSTableInited || IS_NULL(gpbPartition))
    {
        return NULL;
    }
    for (int i = 0; i < FS_MOUNT_NUMBER; i++)
    {
        if (gbFSTable[i].used != 1)
        {
            continue;
        }
        if (gbFSTable[i].partition != NULL && gbFSTable[i].partition->index == index)
        {
            return &gbFSTable[i];
        }
    }
    return NULL;
}

static bFS_t *_bFSFindFree(void)
{
    if (!gbFSTableInited)
    {
        return NULL;
    }
    for (int i = 0; i < FS_MOUNT_NUMBER; i++)
    {
        if (gbFSTable[i].used == 0)
        {
            return &gbFSTable[i];
        }
    }
    return NULL;
}

#if FS_LITTLEFS_

uint32_t lfs_crc(uint32_t crc, const void *buffer, size_t size)
{
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
        0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    const uint8_t *data = buffer;
    for (size_t i = 0; i < size; i++)
    {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }
    return crc;
}

static int _bFSDeviceRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                          void *buffer, lfs_size_t size)
{
    bFS_t *fs = c->context;
    if (fs == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionRead(fs->partition->index, c->block_size * block + off, buffer, size);
    if (ret >= 0)
    {
        return LFS_ERR_OK;
    }
    return LFS_ERR_CORRUPT;
}

static int _bFSDeviceWrite(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                           const void *buffer, lfs_size_t size)
{
    bFS_t *fs = c->context;
    if (fs == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionWrite(fs->partition->index, c->block_size * block + off,
                                (uint8_t *)buffer, size);
    if (ret >= 0)
    {
        return LFS_ERR_OK;
    }
    return LFS_ERR_CORRUPT;
}

static int _bFSDeviceErase(const struct lfs_config *c, lfs_block_t block)
{
    bFS_t *fs = c->context;
    if (fs == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionErase(fs->partition->index, c->block_size * block, c->block_size);
    if (ret >= 0)
    {
        return LFS_ERR_OK;
    }
    return LFS_ERR_CORRUPT;
}

static int _bFSDeviceSync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

#endif /* FS_LITTLEFS_ */

static int _bFSGetIndex(const char *path_str)
{
    if (path_str == NULL)
    {
        return -1;
    }
    /* FATFS style: "0:filename" or "1:filename" */
    if (*path_str >= '0' && *path_str <= '9' && path_str[1] == ':')
    {
        int number = *path_str - '0';
        return number;
    }
    /* LITTLEFS style: "/filename" (absolute path, no partition prefix).
     * When called from bFSOpen, the fd table already knows which partition
     * to use from the mount. We extract the partition from the mounted slot. */
    return -1;
}

/**
 * \}
 */

/**
 * \addtogroup FS_Exported_Functions
 * \{
 */

int bFSInit(const bFSPartition_t *partition, uint8_t partition_number)
{
    if (IS_NULL(partition) || partition_number == 0)
    {
        return -1;
    }
    gpbPartition      = partition;
    gbPartitionNumber = partition_number;
    memset(&gbFSTable[0], 0, sizeof(gbFSTable));
    gbFSTableInited = 1;
    return 0;
}

int bFSGetPartitionState(uint8_t index)
{
    const bFSPartition_t *partition = NULL;
    if (!gbFSTableInited || IS_NULL(gpbPartition) || gbPartitionNumber == 0)
    {
        return -1;
    }
    partition = _bFSFindPartition(index);
    if (IS_NULL(partition))
    {
        return -1;
    }
    if (bDeviceIsAbnormal(partition->dev_no))
    {
        return -1;
    }
    return 0;
}

int bFSGetPartitionInfo(uint8_t index, const bFSPartition_t **p_partition)
{
    const bFSPartition_t *partition = NULL;
    if (!gbFSTableInited || IS_NULL(gpbPartition) || gbPartitionNumber == 0 || IS_NULL(p_partition))
    {
        return -1;
    }
    partition = _bFSFindPartition(index);
    if (IS_NULL(partition))
    {
        return -1;
    }
    *p_partition = partition;
    return 0;
}

int bFSPartitionRead(uint8_t index, uint32_t offset, uint8_t *pbuf, uint32_t len)
{
    const bFSPartition_t *partition = NULL;
    if (IS_NULL(pbuf) || (len == 0))
    {
        return -1;
    }
    partition = _bFSFindPartition(index);
    if (IS_NULL(partition) || offset >= partition->total_size)
    {
        return -1;
    }
    int      fd   = -1;
    uint32_t rlen = (partition->total_size - offset);
    rlen          = (rlen > len) ? len : rlen;
    fd            = bOpen(partition->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    if (bLseek(fd, partition->base_addr + offset) < 0)
    {
        bClose(fd);
        return -2;
    }
    int ret = bRead(fd, pbuf, rlen);
    bClose(fd);
    return ret;
}

int bFSPartitionWrite(uint8_t index, uint32_t offset, uint8_t *pbuf, uint32_t len)
{
    const bFSPartition_t *partition = NULL;
    if (IS_NULL(pbuf) || (len == 0))
    {
        return -1;
    }
    partition = _bFSFindPartition(index);
    if (IS_NULL(partition) || offset >= partition->total_size)
    {
        return -1;
    }
    int      fd   = -1;
    uint32_t wlen = (partition->total_size - offset);
    wlen          = (wlen > len) ? len : wlen;
    fd            = bOpen(partition->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    if (bLseek(fd, partition->base_addr + offset) < 0)
    {
        bClose(fd);
        return -2;
    }
    int ret = bWrite(fd, pbuf, wlen);
    bClose(fd);
    return ret;
}

int bFSPartitionErase(uint8_t index, uint32_t offset, uint32_t len)
{
    const bFSPartition_t *partition = NULL;
    partition                       = _bFSFindPartition(index);
    if (IS_NULL(partition))
    {
        return -1;
    }
    if (offset >= partition->total_size)
    {
        return -1;
    }
    int fd = -1;
    fd     = bOpen(partition->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -1;
    }
    bFlashErase_t cmd_erase;
    cmd_erase.addr = partition->base_addr + offset;
    cmd_erase.num  = (len + partition->sector_size - 1) / partition->sector_size;
    int ret        = bCtl(fd, bCMD_ERASE_SECTOR, &cmd_erase);
    bClose(fd);
    return ret;
}

int bFSMount(uint8_t index, uint8_t mkfs)
{
    bFS_t *fs = _bFSFindMounted(index);
    if (fs != NULL)
    {
#if FS_FATFS_
        /* Slot found but FatFS[] entry may have been cleared by bFSUnmount.
         * Re-register the existing FATFS object so FatFs[vol] points back to it. */
        if (fs->fs_type == 0 && fs->bfs.fs_type == 0)
        {
            FRESULT res = f_mount(&(fs->bfs), (const char *)(&(fs->prefix[0])), 1);
            if (res == FR_OK)
            {
                return 0;
            }
            fs->used      = 0;
            fs->partition = NULL;
            return -1;
        }
#endif
        b_log_e("already mounted...\r\n");
        return -1;
    }
    const bFSPartition_t *partition = _bFSFindPartition(index);
    if (partition == NULL)
    {
        b_log_e("invalid index...\r\n");
        return -1;
    }
    /* Look for a free slot first. If all slots are used-but-unmounted (from a
     * prior bFSUnmount which keeps the slot), reuse the first such slot. */
    fs = NULL;
    for (int i = 0; i < FS_MOUNT_NUMBER; i++)
    {
        if (gbFSTable[i].used == 0)
        {
            fs = &gbFSTable[i];
            break;
        }
    }
    if (fs == NULL)
    {
        b_log_e("The maximum number of mounts is reached\r\n");
        return -2;
    }
    memset(&(fs->prefix[0]), 0, sizeof(fs->prefix));
    snprintf(fs->prefix, sizeof(fs->prefix), "%d:", partition->index);
    fs->partition = partition;
    fs->used      = 1;

    /* mkfs: 0=mount existing, BFS_MKFS_FATFS=mkfs FATFS, BFS_MKFS_LITTLEFS=mkfs LITTLEFS */
#if FS_FATFS_
    if (mkfs == BFS_MKFS_FATFS || mkfs == 0)
    {
        fs->fs_type = 0;
        if (mkfs == BFS_MKFS_FATFS)
        {
            if (bFSMkfs(index) != 0)
            {
                fs->used      = 0;
                fs->partition = NULL;
                return -1;
            }
        }
        memset(&fs->bfs, 0, sizeof(FATFS));
        FRESULT result = f_mount(&(fs->bfs), (const char *)(&(fs->prefix[0])), 1);
        if (result != FR_OK)
        {
            fs->used      = 0;
            fs->partition = NULL;
            return -1;
        }
        return 0;
    }
#endif

#if FS_LITTLEFS_
    if (mkfs == BFS_MKFS_LITTLEFS || mkfs == 0)
    {
        fs->fs_type = 1;
        memset(&(fs->lfs_ctx.lfs_cfg), 0, sizeof(fs->lfs_ctx.lfs_cfg));
        fs->lfs_ctx.lfs_cfg.context    = fs;
        fs->lfs_ctx.lfs_cfg.read       = _bFSDeviceRead;
        fs->lfs_ctx.lfs_cfg.prog       = _bFSDeviceWrite;
        fs->lfs_ctx.lfs_cfg.erase      = _bFSDeviceErase;
        fs->lfs_ctx.lfs_cfg.sync       = _bFSDeviceSync;
        fs->lfs_ctx.lfs_cfg.read_size  = 1;
        fs->lfs_ctx.lfs_cfg.prog_size  = 8;
        fs->lfs_ctx.lfs_cfg.block_size = partition->sector_size;
        if (partition->sector_size == 0)
        {
            fs->used      = 0;
            fs->partition = NULL;
            return -1;
        }
        fs->lfs_ctx.lfs_cfg.block_count      = partition->total_size / partition->sector_size;
        fs->lfs_ctx.lfs_cfg.block_cycles     = 500;
        fs->lfs_ctx.lfs_cfg.cache_size       = LFS_CACHE_SIZE;
        fs->lfs_ctx.lfs_cfg.lookahead_size   = LFS_LOOKAHEAD_SIZE;
        fs->lfs_ctx.lfs_cfg.lookahead_buffer = fs->lfs_ctx.lfs_pre_buf;
        fs->lfs_ctx.lfs_cfg.prog_buffer      = fs->lfs_ctx.lfs_w_buf;
        fs->lfs_ctx.lfs_cfg.read_buffer      = fs->lfs_ctx.lfs_r_buf;

        int result = lfs_mount(&(fs->lfs_ctx.lfs), &(fs->lfs_ctx.lfs_cfg));
        if (result != 0 && mkfs == BFS_MKFS_LITTLEFS)
        {
            result = bFSMkfs(index);
            if (result == 0)
            {
                result = lfs_mount(&(fs->lfs_ctx.lfs), &(fs->lfs_ctx.lfs_cfg));
            }
        }
        if (result != 0)
        {
            fs->used      = 0;
            fs->partition = NULL;
            return result;
        }
        return 0;
    }
#endif

    fs->used      = 0;
    fs->partition = NULL;
    return -1;
}

int bFSUnmount(uint8_t index)
{
    bFS_t *pfs = _bFSFindMounted(index);
    if (pfs == NULL)
    {
        b_log_e("Not mounted...\r\n");
        return -1;
    }
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        f_mount(NULL, (const char *)(&(pfs->prefix[0])), 1);
        pfs->used = 0; /* Clear slot so bFSMount can reuse it */
        return 0;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int result                   = lfs_unmount(&(pfs->lfs_ctx.lfs));
        pfs->used                    = 0;
        pfs->lfs_ctx.lfs_cfg.context = NULL; /* Keep cfg but clear context */
        if (result == 0)
        {
            return 0;
        }
        b_log_e("lfs_unmount err:%d\r\n", result);
        return result;
#endif
    }
    return -1;
}

bFSFd_t bFSOpen(bFSFile_t *fil, const char *path, int flag)
{
    if (IS_NULL(path) || IS_NULL(fil))
    {
        return -1;
    }
    int         index     = _bFSGetIndex(path);
    bFS_t      *pfs       = NULL;
    const char *file_path = path;

    printf("[bFSOpen] index=%d path=%s used_slots=", index, path);
    for (int i = 0; i < FS_MOUNT_NUMBER; i++) {
        printf(" [%d]=%d", i, gbFSTable[i].used);
    }
    printf("\n"); fflush(stdout);

    if (index >= 0)
    {
        /* FATFS style: "0:filename" or "1:filename" */
        pfs       = _bFSFindMounted(index);
        file_path = path + 2; /* skip "N:" prefix */
    }
    else
    {
        /* LITTLEFS style: "/filename" (no partition prefix).
         * Find the first mounted LITTLEFS partition. */
        for (int i = 0; i < FS_MOUNT_NUMBER; i++)
        {
            if (gbFSTable[i].used == 1 && gbFSTable[i].fs_type == 1)
            {
                pfs = &gbFSTable[i];
                break;
            }
        }
    }

    if (pfs == NULL)
    {
        b_log_e("Not mounted...\r\n");
        return -1;
    }
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        uint8_t mode = 0;
        if (flag & BFS_O_RD)
            mode |= FA_READ;
        if (flag & BFS_O_WR)
            mode |= FA_WRITE;
        if (flag & BFS_O_RDWR)
            mode |= FA_READ | FA_WRITE;
        if (flag & BFS_O_CREAT)
            mode |= FA_OPEN_ALWAYS;
        if (flag & BFS_O_EXCL)
            mode |= FA_CREATE_NEW;
        if (flag & BFS_O_TRUNC)
            mode |= FA_CREATE_ALWAYS;
        if (flag & BFS_O_APPEND)
            mode |= FA_OPEN_APPEND;
        fil->fs_context = pfs;
        /* Use full path (prefix + file_path) for FatFS */
        char full_path[64];
        snprintf(full_path, sizeof(full_path), "%s%s", pfs->prefix, file_path);
        FRESULT ret = f_open(&(fil->bfile), full_path, mode);
        if (ret == FR_NO_FILE)
        {
            return -1;
        }
        if (ret == FR_OK)
        {
            return (bFSFd_t)(intptr_t)fil;
        }
        return -1;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int lfflag = 0;
        if (flag & BFS_O_RD)
            lfflag |= LFS_O_RDONLY;
        if (flag & BFS_O_WR)
            lfflag |= LFS_O_WRONLY;
        if (flag & BFS_O_RDWR)
            lfflag |= LFS_O_RDWR;
        if (flag & BFS_O_CREAT)
            lfflag |= LFS_O_CREAT;
        if (flag & BFS_O_EXCL)
            lfflag |= LFS_O_EXCL;
        if (flag & BFS_O_TRUNC)
            lfflag |= LFS_O_TRUNC;
        if (flag & BFS_O_APPEND)
            lfflag |= LFS_O_APPEND;
        fil->lfs_ctx.cfg.buffer     = fil->lfs_ctx.buf;
        fil->lfs_ctx.cfg.attrs      = NULL;
        fil->lfs_ctx.cfg.attr_count = 0;
        int ret = lfs_file_opencfg(&(pfs->lfs_ctx.lfs), &(fil->lfs_ctx.lfp_file), file_path, lfflag,
                                   &(fil->lfs_ctx.cfg));
        if (0 == ret)
        {
            fil->fs_context = pfs;
            return (bFSFd_t)(intptr_t)fil;
        }
        b_log_e("ret:%d\r\n", ret);
        return -1;
#endif
    }
    return -1;
}

int bFSWrite(bFSFd_t fd, uint8_t *pbuf, uint32_t len)
{
    if (IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)(uintptr_t)fd;
    if (IS_NULL(pfile) || pfile->fs_context == NULL)
    {
        return -1;
    }
    bFS_t *pfs = pfile->fs_context;
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        uint32_t real_len = 0;
        FRESULT  ret      = f_write(&(pfile->bfile), pbuf, len, &real_len);
        if (ret == FR_OK)
        {
            return (int)real_len;
        }
        b_log_e("ret:%d\r\n", ret);
        return -1;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int ret = lfs_file_write(&(pfs->lfs_ctx.lfs), &(pfile->lfs_ctx.lfp_file), pbuf, len);
        if (ret < 0)
        {
            b_log_e("ret:%d\r\n", ret);
        }
        return ret;
#endif
    }
    return -1;
}

int bFSRead(bFSFd_t fd, uint8_t *pbuf, uint32_t len)
{
    if (IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)(uintptr_t)fd;
    if (IS_NULL(pfile) || pfile->fs_context == NULL)
    {
        return -1;
    }
    bFS_t *pfs = pfile->fs_context;
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        uint32_t real_len = 0;
        FRESULT  ret      = f_read(&(pfile->bfile), pbuf, len, &real_len);
        if (ret == FR_OK)
        {
            return (int)real_len;
        }
        b_log_e("ret:%d\r\n", ret);
        return -1;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int ret = lfs_file_read(&(pfs->lfs_ctx.lfs), &(pfile->lfs_ctx.lfp_file), pbuf, len);
        if (ret < 0)
        {
            b_log_e("ret:%d\r\n", ret);
        }
        return ret;
#endif
    }
    return -1;
}

int bFSClose(bFSFd_t fd)
{
    bFSFile_t *pfile = (bFSFile_t *)(uintptr_t)fd;
    if (IS_NULL(pfile) || pfile->fs_context == NULL)
    {
        return -1;
    }
    bFS_t *pfs = pfile->fs_context;
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        FRESULT ret = f_close(&(pfile->bfile));
        if (ret == FR_OK)
        {
            return 0;
        }
        b_log_e("ret:%d\r\n", ret);
        return -1;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int ret = lfs_file_close(&(pfs->lfs_ctx.lfs), &(pfile->lfs_ctx.lfp_file));
        if (ret < 0)
        {
            b_log_e("ret:%d\r\n", ret);
        }
        return ret;
#endif
    }
    return -1;
}

int bFSLseek(bFSFd_t fd, int32_t offset, int whence)
{
    bFSFile_t *pfile = (bFSFile_t *)(uintptr_t)fd;
    if (IS_NULL(pfile) || pfile->fs_context == NULL)
    {
        return -1;
    }
    bFS_t *pfs = pfile->fs_context;
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        uint32_t new_offset = 0;
        if (whence == BFS_SEEK_CUR)
        {
            int32_t c_offset = f_tell(&(pfile->bfile));
            if ((c_offset + offset) < 0)
            {
                new_offset = 0;
            }
            else
            {
                new_offset = f_tell(&(pfile->bfile)) + offset;
            }
        }
        else if (whence == BFS_SEEK_END)
        {
            int32_t c_offset = f_size(&(pfile->bfile));
            if ((c_offset + offset) < 0)
            {
                new_offset = 0;
            }
            else
            {
                new_offset = f_size(&(pfile->bfile)) + offset;
            }
        }
        else
        {
            if (offset < 0)
            {
                new_offset = 0;
            }
            else
            {
                new_offset = offset;
            }
        }
        FRESULT ret = f_lseek(&(pfile->bfile), new_offset);
        if (ret == FR_OK)
        {
            return 0;
        }
        b_log_e("ret:%d\r\n", ret);
        return -1;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        int lfswhence = 0;
        if (whence == BFS_SEEK_CUR)
        {
            lfswhence = LFS_SEEK_CUR;
        }
        else if (whence == BFS_SEEK_END)
        {
            lfswhence = LFS_SEEK_END;
        }
        else
        {
            lfswhence = LFS_SEEK_SET;
        }
        int ret = lfs_file_seek(&(pfs->lfs_ctx.lfs), &(pfile->lfs_ctx.lfp_file), offset, lfswhence);
        if (ret < 0)
        {
            b_log_e("ret:%d\r\n", ret);
        }
        return ret;
#endif
    }
    return -1;
}

#if (defined(_FS_MKFS_ENABLE) && (_FS_MKFS_ENABLE == 1))
#if FS_FATFS_
static uint8_t bMkfsBuf[FF_MAX_SS];
#endif
#if FS_LITTLEFS_
static uint8_t bMkfsLfsReadBuf[LFS_CACHE_SIZE];
static uint8_t bMkfsLfsProgBuf[LFS_CACHE_SIZE];
static uint8_t bMkfsLfsLookahead[LFS_LOOKAHEAD_SIZE];
#endif
#endif

int bFSMkfs(uint8_t index)
{
#if (defined(_FS_MKFS_ENABLE) && (_FS_MKFS_ENABLE == 1))
    bFS_t *pfs = _bFSFindMounted(index);
    if (IS_NULL(pfs))
    {
        const bFSPartition_t *part = _bFSFindPartition(index);
        if (IS_NULL(part))
        {
            return -1;
        }
        pfs = _bFSFindFree();
        if (IS_NULL(pfs))
        {
            return -1;
        }
        pfs->partition = part;
        pfs->used      = 1;
        memset(&(pfs->prefix[0]), 0, sizeof(pfs->prefix));
        snprintf(pfs->prefix, sizeof(pfs->prefix), "%d:", part->index);
    }
    else
    {
        /* Already mounted: use existing slot (initialized by bFSMount) */
    }
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        pfs->fs_type = 0;
        /* Pre-erase the entire TESTFLASH buffer so all bytes are 0xFF.
         * FatFS f_mkfs writes the VBR to various sector offsets; if any
         * byte in those offsets has been written by previous KV operations,
         * TESTFLASH's 1→0-only constraint silently drops the write.
         * Erasing the whole buffer guarantees the VBR is written correctly. */
        /* Erase entire partition so all bytes are 0xFF before mkfs.
         * FatFS f_mkfs writes the VBR to various sector offsets; if any
         * byte in those offsets has been written by previous KV operations,
         * TESTFLASH's 1→0-only constraint silently drops the write.
         * Erasing the whole partition guarantees the VBR is written correctly. */
        bFSPartitionErase(pfs->partition->index, 0, pfs->partition->total_size);
        MKFS_PARM mkfs_opt = {FM_FAT | FM_FAT32, 0, 0, 0, 0};
        FRESULT   ret      = f_mkfs((const char *)pfs->prefix, &mkfs_opt, bMkfsBuf, FF_MAX_SS);
        if (ret != FR_OK)
        {
            pfs->used = 0;
            return -1;
        }
        return 0;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        pfs->fs_type = 1;
        /* Erase entire partition so all bytes are 0xFF before mkfs. */
        bFSPartitionErase(pfs->partition->index, 0, pfs->partition->total_size);
        memset(&(pfs->lfs_ctx.lfs_cfg), 0, sizeof(pfs->lfs_ctx.lfs_cfg));
        pfs->lfs_ctx.lfs_cfg.context     = pfs;
        pfs->lfs_ctx.lfs_cfg.read        = _bFSDeviceRead;
        pfs->lfs_ctx.lfs_cfg.prog        = _bFSDeviceWrite;
        pfs->lfs_ctx.lfs_cfg.erase       = _bFSDeviceErase;
        pfs->lfs_ctx.lfs_cfg.sync        = _bFSDeviceSync;
        pfs->lfs_ctx.lfs_cfg.read_size   = 1;
        pfs->lfs_ctx.lfs_cfg.prog_size   = 8;
        pfs->lfs_ctx.lfs_cfg.block_size  = pfs->partition->sector_size;
        pfs->lfs_ctx.lfs_cfg.block_count = pfs->partition->total_size / pfs->partition->sector_size;
        pfs->lfs_ctx.lfs_cfg.block_cycles     = 500;
        pfs->lfs_ctx.lfs_cfg.cache_size       = LFS_CACHE_SIZE;
        pfs->lfs_ctx.lfs_cfg.lookahead_size   = LFS_LOOKAHEAD_SIZE;
        pfs->lfs_ctx.lfs_cfg.lookahead_buffer = bMkfsLfsLookahead;
        pfs->lfs_ctx.lfs_cfg.prog_buffer      = bMkfsLfsProgBuf;
        pfs->lfs_ctx.lfs_cfg.read_buffer      = bMkfsLfsReadBuf;

        int ret = lfs_format(&(pfs->lfs_ctx.lfs), &(pfs->lfs_ctx.lfs_cfg));
        if (ret < 0)
        {
            pfs->used = 0;
            return ret;
        }
        /* Do NOT mount here — bFSMount will do the final mount with fs->lfs_ctx buffers.
         * Mounting here with bMkfsLfs* buffers and then unmounting corrupts littlefs state. */
        return 0;
#endif
    }
    return -1;
#else
    return -1;
#endif
}

int bFSGetInfo(uint8_t index, uint32_t *ptotal_size, uint32_t *pfree_size)
{
    if (IS_NULL(ptotal_size) || IS_NULL(pfree_size))
    {
        return -1;
    }
    bFS_t *pfs = _bFSFindMounted(index);
    if (IS_NULL(pfs))
    {
        return -1;
    }
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        uint32_t fre_sect, tot_sect;
        DWORD    fclst  = 0;
        FATFS   *pfatfs = NULL;
        FRESULT  ret    = f_getfree((const char *)pfs->prefix, &fclst, &pfatfs);
        if (ret)
        {
            return -1;
        }
        tot_sect     = (pfatfs->n_fatent - 2) * pfatfs->csize;
        fre_sect     = fclst * pfatfs->csize;
        *ptotal_size = tot_sect * pfs->partition->sector_size;
        *pfree_size  = fre_sect * pfs->partition->sector_size;
        return 0;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        uint32_t total_space = pfs->lfs_ctx.lfs_cfg.block_size * pfs->lfs_ctx.lfs_cfg.block_count;
        int32_t  used_blocks = lfs_fs_size(&(pfs->lfs_ctx.lfs));
        if (used_blocks < 0)
        {
            return -1;
        }
        uint32_t free_space =
            (pfs->lfs_ctx.lfs_cfg.block_count - used_blocks) * pfs->lfs_ctx.lfs_cfg.block_size;
        *ptotal_size = total_space;
        *pfree_size  = free_space;
        return 0;
#endif
    }
    return -1;
}

int bFSFileGetInfo(bFSFd_t fd, uint32_t *pfile_size)
{
    if (IS_NULL(pfile_size))
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)(uintptr_t)fd;
    if (IS_NULL(pfile) || pfile->fs_context == NULL)
    {
        return -1;
    }
    bFS_t *pfs = pfile->fs_context;
    if (pfs->fs_type == 0)
    {
#if FS_FATFS_
        *pfile_size = f_size(&(pfile->bfile));
        return 0;
#endif
    }
    else
    {
#if FS_LITTLEFS_
        *pfile_size = pfile->lfs_ctx.lfp_file.ctz.size;
        return 0;
#endif
    }
    return -1;
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
