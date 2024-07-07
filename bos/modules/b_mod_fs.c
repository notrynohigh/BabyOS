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

#include "core/inc/b_core.h"
#include "core/inc/b_device.h"
#include "drivers/inc/b_driver_cmd.h"
#include "utils/inc/b_util_log.h"

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
    char                  prefix[5];
    const bFSPartition_t *partition;
#if defined(FS_FATFS)
    FATFS bfs;
#endif
#if defined(FS_LITTLEFS)
    struct lfs_config cfg;
    lfs_t             bfs;
    uint8_t           r_buf[LFS_CACHE_SIZE];
    uint8_t           w_buf[LFS_CACHE_SIZE];
    uint8_t           pre_buf[LFS_LOOKAHEAD_SIZE];
#endif
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
    int    i   = 0;
    bFS_t *pfs = NULL;
    for (i = 0; i < FS_MOUNT_NUMBER; i++)
    {
        if (gbFSTable[i].used != 1)
        {
            continue;
        }
        if (gbFSTable[i].partition->index == index)
        {
            pfs = &gbFSTable[i];
            break;
        }
    }
    return pfs;
}

#if defined(FS_LITTLEFS)

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
    int ret =
        bFSPartitionRead(fs->partition->index, fs->cfg.block_size * block + off, buffer, size);
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
    int ret = bFSPartitionWrite(fs->partition->index, fs->cfg.block_size * block + off,
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
    int ret =
        bFSPartitionErase(fs->partition->index, fs->cfg.block_size * block, fs->cfg.block_size);
    if (ret >= 0)
    {
        return LFS_ERR_OK;
    }
    return LFS_ERR_CORRUPT;
}

static int _bFSDeviceSync(const struct lfs_config *c)
{
    return LFS_ERR_OK;
}

#endif

static int _bFSGetIndex(const char *path_str)
{
    int   number;
    char *endptr;

    number = strtol(path_str, &endptr, 10);

    if (*endptr == ':' && number >= 0 && number <= 255)
    {
        return number;
    }
    else
    {
        return -1;
    }
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
    if (IS_NULL(partition) || partition_number == 0 || !IS_NULL(gpbPartition))
    {
        return -1;
    }
    gpbPartition      = partition;
    gbPartitionNumber = partition_number;
    memset(&gbFSTable[0], 0, sizeof(gbFSTable));
    return 0;
}

int bFSGetPartitionState(uint8_t index)
{
    const bFSPartition_t *partition = NULL;
    if (IS_NULL(gpbPartition) || gbPartitionNumber == 0)
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
    if (IS_NULL(gpbPartition) || gbPartitionNumber == 0 || IS_NULL(p_partition))
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
    bLseek(fd, partition->base_addr + offset);
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
    bLseek(fd, partition->base_addr + offset);
    int ret = bWrite(fd, pbuf, wlen);
    bClose(fd);
    return ret;
}

int bFSPartitionErase(uint8_t index, uint32_t offset, uint32_t len)
{
    const bFSPartition_t *partition = NULL;
    bFlashErase_t         cmd_erase;
    if (len == 0)
    {
        return -1;
    }
    partition = _bFSFindPartition(index);
    if (IS_NULL(partition) || offset >= partition->total_size)
    {
        return -1;
    }
    int fd = -1;
    fd     = bOpen(partition->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    cmd_erase.addr = partition->base_addr + offset;
    cmd_erase.num  = len / partition->sector_size;
    int ret        = bCtl(fd, bCMD_ERASE_SECTOR, &cmd_erase);
    bClose(fd);
    return ret;
}

int bFSMount(uint8_t index, uint8_t mkfs)
{
    bFS_t *fs = _bFSFindMounted(index);
    if (fs != NULL)
    {
        b_log_e("already mounted...\r\n");
        return -1;
    }
    const bFSPartition_t *partition = _bFSFindPartition(index);
    if (partition == NULL)
    {
        b_log_e("invalid index...\r\n");
        return -1;
    }
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
    fs->partition = partition;
    fs->used      = 1;
    memset(&(fs->prefix[0]), 0, sizeof(fs->prefix));
    sprintf(&(fs->prefix[0]), "%d:", partition->index);
#if defined(FS_FATFS)
    FRESULT result = FR_OK;

    result = f_mount(&(fs->bfs), (const char *)(&(fs->prefix[0])), 1);
    if ((result == FR_NO_FILESYSTEM) && (mkfs == 1))
    {
        if (bFSMkfs(index) == 0)
        {
            result = f_mount(&(fs->bfs), (const char *)(&(fs->prefix[0])), 1);
        }
    }
    if (result == FR_OK)
    {
        return 0;
    }
    fs->used = 0;
    b_log_e("result:%d\r\n", result);
    return -1;
#elif (defined(FS_LITTLEFS))
    memset(&(fs->cfg), 0, sizeof(fs->cfg));
    fs->cfg.context          = fs;
    fs->cfg.read             = _bFSDeviceRead;
    fs->cfg.prog             = _bFSDeviceWrite;
    fs->cfg.erase            = _bFSDeviceErase;
    fs->cfg.sync             = _bFSDeviceSync;
    fs->cfg.read_size        = 1;
    fs->cfg.prog_size        = 8;
    fs->cfg.block_size       = partition->sector_size;
    fs->cfg.block_count      = partition->total_size / partition->sector_size;
    fs->cfg.block_cycles     = 500;
    fs->cfg.cache_size       = LFS_CACHE_SIZE;
    fs->cfg.lookahead_size   = LFS_LOOKAHEAD_SIZE;
    fs->cfg.lookahead_buffer = fs->pre_buf;
    fs->cfg.prog_buffer      = fs->w_buf;
    fs->cfg.read_buffer      = fs->r_buf;

    int result = lfs_mount(&(fs->bfs), &(fs->cfg));
    if (result && (mkfs == 1))
    {
        result = bFSMkfs(index);
        if (result == 0)
        {
            result = lfs_mount(&(fs->bfs), &(fs->cfg));
        }
    }
    if (result != 0)
    {
        fs->used = 0;
    }
    b_log_e("result:%d\r\n", result);
    return result;
#else
    return -1;
#endif
}

int bFSUnmount(uint8_t index)
{
    bFS_t *pfs = _bFSFindMounted(index);
    if (pfs == NULL)
    {
        b_log_e("Not mounted...\r\n");
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT result = FR_OK;

    result = f_mount(NULL, (const char *)(&(pfs->prefix[0])), 1);
    if (result == FR_OK)
    {
        pfs->used = 0;
        return 0;
    }
    b_log_e("result:%d\r\n", result);
    return -1;
#elif (defined(FS_LITTLEFS))

    int result = lfs_unmount(&(pfs->bfs));
    if (result == 0)
    {
        pfs->used        = 0;
        pfs->cfg.context = NULL;
    }
    b_log_e("err:%d\r\n", result);
    return result;
#else
    return -1;
#endif
}

int bFSOpen(bFSFile_t *fil, const char *path, int flag)
{
    if (IS_NULL(path) || IS_NULL(fil))
    {
        return -1;
    }
    int index = _bFSGetIndex(path);
    if (index < 0)
    {
        b_log_e("path invalid...\r\n");
        b_log_e("The right example: \"0:babyos.txt\" \"1:babyos.txt\" ...\r\n");
        return -1;
    }
    bFS_t *pfs = _bFSFindMounted(index);
    if (pfs == NULL)
    {
        b_log_e("Not mounted...\r\n");
        return -1;
    }
#if defined(FS_FATFS)
    uint8_t mode = 0;
    if (flag & BFS_O_RD)
    {
        mode |= FA_READ;
    }
    if (flag & BFS_O_WR)
    {
        mode |= FA_WRITE;
    }
    if (flag & BFS_O_RDWR)
    {
        mode |= FA_READ | FA_WRITE;
    }
    if (flag & BFS_O_CREAT)
    {
        mode |= FA_OPEN_ALWAYS;
    }
    if (flag & BFS_O_EXCL)
    {
        mode |= FA_CREATE_NEW;
    }
    if (flag & BFS_O_TRUNC)
    {
        mode |= FA_CREATE_ALWAYS;
    }
    if (flag & BFS_O_APPEND)
    {
        mode |= FA_OPEN_APPEND;
    }
    FRESULT ret = f_open(&(fil->bfile), path, mode);
    if (FR_OK == ret)
    {
        fil->reserved = pfs;
        return ((int)fil);
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    int lfflag = 0;
    if (flag & BFS_O_RD)
    {
        lfflag |= LFS_O_RDONLY;
    }
    if (flag & BFS_O_WR)
    {
        lfflag |= LFS_O_WRONLY;
    }
    if (flag & BFS_O_RDWR)
    {
        lfflag |= LFS_O_RDWR;
    }
    if (flag & BFS_O_CREAT)
    {
        lfflag |= LFS_O_CREAT;
    }
    if (flag & BFS_O_EXCL)
    {
        lfflag |= LFS_O_EXCL;
    }
    if (flag & BFS_O_TRUNC)
    {
        lfflag |= LFS_O_TRUNC;
    }
    if (flag & BFS_O_APPEND)
    {
        lfflag |= LFS_O_APPEND;
    }
    fil->cfg.buffer     = fil->buf;
    fil->cfg.attrs      = NULL;
    fil->cfg.attr_count = 0;
    int ret             = lfs_file_opencfg(&(pfs->bfs), &(fil->bfile), path, lfflag, &(fil->cfg));
    if (0 == ret)
    {
        fil->reserved = pfs;
        return ((int)fil);
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#else
    return -1;
#endif
}

int bFSWrite(int fd, uint8_t *pbuf, uint32_t len)
{
    uint32_t real_len = 0;
    if (!BFS_FD_IS_VALID(fd) || IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)fd;
    bFS_t     *pfs   = pfile->reserved;
    if (pfs == NULL)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_write(&(pfile->bfile), pbuf, len, &real_len);
    if (ret == FR_OK)
    {
        return real_len;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    B_UNUSED(real_len);
    int ret = lfs_file_write(&(pfs->bfs), &(pfile->bfile), pbuf, len);
    b_log_e("ret:%d\r\n", ret);
    return ret;
#else
    B_UNUSED(real_len);
    return -1;
#endif
}

int bFSRead(int fd, uint8_t *pbuf, uint32_t len)
{
    uint32_t real_len = 0;
    if (!BFS_FD_IS_VALID(fd) || IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)fd;
    bFS_t     *pfs   = pfile->reserved;
    if (pfs == NULL)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_read(&(pfile->bfile), pbuf, len, &real_len);
    if (ret == FR_OK)
    {
        return real_len;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    B_UNUSED(real_len);
    int ret = lfs_file_read(&(pfs->bfs), &(pfile->bfile), pbuf, len);
    b_log_e("ret:%d\r\n", ret);
    return ret;
#else
    B_UNUSED(real_len);
    return -1;
#endif
}

int bFSClose(int fd)
{
    if (!BFS_FD_IS_VALID(fd))
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)fd;
    bFS_t     *pfs   = pfile->reserved;
    if (pfs == NULL)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_close(&(pfile->bfile));
    if (ret == FR_OK)
    {
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    int ret = lfs_file_close(&(pfs->bfs), &(pfile->bfile));
    if (ret < 0)
    {
        b_log_e("ret:%d\r\n", ret);
    }
    return ret;
#else
    return -1;
#endif
}

int bFSLseek(int fd, int32_t offset, int whence)
{
    if (!BFS_FD_IS_VALID(fd))
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)fd;
    bFS_t     *pfs   = pfile->reserved;
    if (pfs == NULL)
    {
        return -1;
    }
#if defined(FS_FATFS)
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
#elif (defined(FS_LITTLEFS))
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
    int ret = lfs_file_seek(&(pfs->bfs), &(pfile->bfile), offset, lfswhence);
    if (ret < 0)
    {
        b_log_e("ret:%d\r\n", ret);
    }
    return ret;
#else
    return -1;
#endif
}

#if (defined(_FS_MKFS_ENABLE) && (_FS_MKFS_ENABLE == 1))
#if defined(FS_FATFS)
static uint8_t bMkfsBuf[FF_MAX_SS];
#endif
#endif
int bFSMkfs(uint8_t index)
{
#if (defined(_FS_MKFS_ENABLE) && (_FS_MKFS_ENABLE == 1))
    bFS_t *pfs = _bFSFindMounted(index);
    if (IS_NULL(pfs))
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_mkfs((const char *)pfs->prefix, NULL, bMkfsBuf, FF_MAX_SS);
    if (ret == FR_OK)
    {
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    int ret = lfs_format(&(pfs->bfs), &(pfs->cfg));
    if (ret < 0)
    {
        b_log_e("ret:%d\r\n", ret);
    }
    return ret;
#else
    return -1;
#endif
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
#if defined(FS_FATFS)
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
#elif (defined(FS_LITTLEFS))
    uint32_t total_space = pfs->cfg.block_size * pfs->cfg.block_count;
    int32_t  used_blocks = lfs_fs_size(&(pfs->bfs));
    if (used_blocks < 0)
    {
        return -1;
    }
    uint32_t free_space = (pfs->cfg.block_count - used_blocks) * pfs->cfg.block_size;
    *ptotal_size        = total_space;
    *pfree_size         = free_space;
    return 0;
#else
    return -1;
#endif
}

int bFSFileGetInfo(int fd, uint32_t *pfile_size)
{
    if (!BFS_FD_IS_VALID(fd))
    {
        return -1;
    }
    bFSFile_t *pfile = (bFSFile_t *)fd;
#if defined(FS_FATFS)
    *pfile_size = f_size(&(pfile->bfile));
    return 0;
#elif (defined(FS_LITTLEFS))
    *pfile_size = lfs_max(pfile->bfile.pos, pfile->bfile.ctz.size);
    return 0;
#else
    return -1;
#endif
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
