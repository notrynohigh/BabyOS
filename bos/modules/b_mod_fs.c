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
static uint16_t              gbPartitionNumber = 0;

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

static const bFSPartition_t *_bFSFindPartition(uint16_t index)
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
    const bFSPartition_t *tmp = _bFSFindPartition(fs->index);
    if (tmp == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionRead(fs->index, fs->cfg.block_size * block + off, buffer, size);
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
    const bFSPartition_t *tmp = _bFSFindPartition(fs->index);
    if (tmp == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionWrite(fs->index, fs->cfg.block_size * block + off, (uint8_t *)buffer, size);
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
    const bFSPartition_t *tmp = _bFSFindPartition(fs->index);
    if (tmp == NULL)
    {
        return LFS_ERR_CORRUPT;
    }
    int ret = bFSPartitionErase(fs->index, fs->cfg.block_size * block, fs->cfg.block_size);
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

/**
 * \}
 */

/**
 * \addtogroup FS_Exported_Functions
 * \{
 */

int bFSInit(const bFSPartition_t *partition, uint16_t partition_number)
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

int bFSGetPartitionState(uint16_t index)
{
    const bFSPartition_t *tmp = NULL;
    if (IS_NULL(gpbPartition) || gbPartitionNumber == 0)
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (IS_NULL(tmp))
    {
        return -1;
    }
    if (bDeviceIsAbnormal(tmp->dev_no))
    {
        return -1;
    }
    return 0;
}

int bFSGetPartitionInfo(uint16_t index, const bFSPartition_t **partition)
{
    const bFSPartition_t *tmp = NULL;
    if (IS_NULL(gpbPartition) || gbPartitionNumber == 0 || IS_NULL(partition))
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (IS_NULL(tmp))
    {
        return -1;
    }
    *partition = tmp;
    return 0;
}

int bFSPartitionRead(uint16_t index, uint32_t offset, uint8_t *pbuf, uint32_t len)
{
    const bFSPartition_t *tmp = NULL;
    if (IS_NULL(pbuf) || (len == 0))
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (IS_NULL(tmp) || offset >= tmp->total_size)
    {
        return -1;
    }
    int      fd   = -1;
    uint32_t rlen = (tmp->total_size - offset);
    rlen          = (rlen > len) ? len : rlen;
    fd            = bOpen(tmp->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    bLseek(fd, tmp->base_addr + offset);
    int ret = bRead(fd, pbuf, rlen);
    bClose(fd);
    return ret;
}

int bFSPartitionWrite(uint16_t index, uint32_t offset, uint8_t *pbuf, uint32_t len)
{
    const bFSPartition_t *tmp = NULL;
    if (IS_NULL(pbuf) || (len == 0))
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (IS_NULL(tmp) || offset >= tmp->total_size)
    {
        return -1;
    }
    int      fd   = -1;
    uint32_t wlen = (tmp->total_size - offset);
    wlen          = (wlen > len) ? len : wlen;
    fd            = bOpen(tmp->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    bLseek(fd, tmp->base_addr + offset);
    int ret = bWrite(fd, pbuf, wlen);
    bClose(fd);
    return ret;
}

int bFSPartitionErase(uint16_t index, uint32_t offset, uint32_t len)
{
    const bFSPartition_t *tmp = NULL;
    bFlashErase_t         cmd_erase;
    if (len == 0)
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (IS_NULL(tmp) || offset >= tmp->total_size)
    {
        return -1;
    }
    int fd = -1;
    fd     = bOpen(tmp->dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -2;
    }
    cmd_erase.addr = tmp->base_addr + offset;
    cmd_erase.num  = len / tmp->sector_size;
    int ret        = bCtl(fd, bCMD_ERASE_SECTOR, &cmd_erase);
    bClose(fd);
    return ret;
}

int bFSMount(bFS_t **pfs, uint16_t index, uint8_t mkfs)
{
    const bFSPartition_t *tmp = NULL;
    if (IS_NULL(pfs))
    {
        return -1;
    }
    tmp = _bFSFindPartition(index);
    if (tmp == NULL)
    {
        return -1;
    }
    bFS_t *fs = NULL;
    int    i  = 0;
    for (i = 0; i < FS_MOUNT_NUMBER; i++)
    {
        if (gbFSTable[i].used == 0)
        {
            fs = &gbFSTable[i];
            break;
        }
    }
    if (fs == NULL)
    {
        return -2;
    }
    fs->index = index;
#if defined(FS_FATFS)
    FRESULT result = FR_OK;
    uint8_t disk_str[8];
    sprintf((char *)disk_str, "%d:", index);
    result = f_mount(&(fs->bfs), (const char *)disk_str, 1);

    if ((result == FR_NO_FILESYSTEM) && (mkfs == 1))
    {
        if (bFSMkfs(fs) == 0)
        {
            result = f_mount(&(fs->bfs), (const char *)disk_str, 1);
        }
    }
    if (result == FR_OK)
    {
        fs->used = 1;
        *pfs     = fs;
        return 0;
    }
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
    fs->cfg.block_size       = tmp->sector_size;
    fs->cfg.block_count      = tmp->total_size / tmp->sector_size;
    fs->cfg.block_cycles     = 500;
    fs->cfg.cache_size       = LFS_CACHE_SIZE;
    fs->cfg.lookahead_size   = LFS_LOOKAHEAD_SIZE;
    fs->cfg.lookahead_buffer = fs->pre_buf;
    fs->cfg.prog_buffer      = fs->w_buf;
    fs->cfg.read_buffer      = fs->r_buf;

    int err = lfs_mount(&(fs->bfs), &(fs->cfg));
    if (err && (mkfs == 1))
    {
        err = bFSMkfs(fs);
        if (err == 0)
        {
            err = lfs_mount(&(fs->bfs), &(fs->cfg));
        }
    }
    if (err == 0)
    {
        fs->used = 1;
        *pfs     = fs;
    }
    b_log_e("err:%d\r\n", err);
    return err;
#else
    return -1;
#endif
}

int bFSUnmount(bFS_t *pfs)
{
    if (IS_NULL(pfs) || pfs->used == 0)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT result = FR_OK;
    uint8_t disk_str[8];
    sprintf((char *)disk_str, "%d:", pfs->index);
    result = f_mount(NULL, (const char *)disk_str, 1);
    if (result == FR_OK)
    {
        pfs->used = 0;
        return 0;
    }
    b_log_e("result:%d\r\n", result);
    return -1;
#elif (defined(FS_LITTLEFS))

    int err = lfs_unmount(&(pfs->bfs));
    if (err == 0)
    {
        pfs->used        = 0;
        pfs->cfg.context = NULL;
    }
    b_log_e("err:%d\r\n", err);
    return err;
#else
    return -1;
#endif
}

int bFSOpen(bFS_t *pfs, bFSFile_t *pfile, const char *path, int flag)
{
    if (IS_NULL(pfs) || IS_NULL(path) || IS_NULL(pfile))
    {
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
    FRESULT ret = f_open(&(pfile->bfile), path, mode);
    if (FR_OK == ret)
    {
        return 0;
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
    pfile->cfg.buffer     = pfile->buf;
    pfile->cfg.attrs      = NULL;
    pfile->cfg.attr_count = 0;
    int ret = lfs_file_opencfg(&(pfs->bfs), &(pfile->bfile), path, lfflag, &(pfile->cfg));
    if (0 == ret)
    {
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#else
    return -1;
#endif
}

int bFSWrite(bFS_t *pfs, bFSFile_t *pfile, uint8_t *pbuf, uint32_t len, uint32_t *wlen)
{
    uint32_t real_len = 0;
    if (IS_NULL(pfs) || IS_NULL(pfile) || IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_write(&(pfile->bfile), pbuf, len, &real_len);
    if (ret == FR_OK)
    {
        if (wlen)
        {
            *wlen = real_len;
        }
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    B_UNUSED(real_len);
    int ret = lfs_file_write(&(pfs->bfs), &(pfile->bfile), pbuf, len);
    if (ret >= 0)
    {
        if (wlen)
        {
            *wlen = ret;
        }
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#else
    B_UNUSED(real_len);
    return -1;
#endif
}

int bFSRead(bFS_t *pfs, bFSFile_t *pfile, uint8_t *pbuf, uint32_t len, uint32_t *rlen)
{
    uint32_t real_len = 0;
    if (IS_NULL(pfs) || IS_NULL(pfile) || IS_NULL(pbuf) || len == 0)
    {
        return -1;
    }
#if defined(FS_FATFS)
    FRESULT ret = f_read(&(pfile->bfile), pbuf, len, &real_len);
    if (ret == FR_OK)
    {
        if (rlen)
        {
            *rlen = real_len;
        }
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#elif (defined(FS_LITTLEFS))
    B_UNUSED(real_len);
    int ret = lfs_file_read(&(pfs->bfs), &(pfile->bfile), pbuf, len);
    if (ret >= 0)
    {
        if (rlen)
        {
            *rlen = ret;
        }
        return 0;
    }
    b_log_e("ret:%d\r\n", ret);
    return -1;
#else
    B_UNUSED(real_len);
    return -1;
#endif
}

int bFSClose(bFS_t *pfs, bFSFile_t *pfile)
{
    if (IS_NULL(pfs) || IS_NULL(pfile))
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

int bFSLseek(bFS_t *pfs, bFSFile_t *pfile, int32_t offset, int whence)
{
    if (IS_NULL(pfs) || IS_NULL(pfile))
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
int bFSMkfs(bFS_t *pfs)
{
#if (defined(_FS_MKFS_ENABLE) && (_FS_MKFS_ENABLE == 1))
    if (IS_NULL(pfs))
    {
        return -1;
    }
#if defined(FS_FATFS)
    uint8_t disk_str[8];
    sprintf((char *)disk_str, "%d:", pfs->index);
    FRESULT ret = f_mkfs((const char *)disk_str, NULL, bMkfsBuf, FF_MAX_SS);
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

int bFSGetInfo(bFS_t *pfs, uint32_t *ptotal_size, uint32_t *pfree_size)
{
    if (IS_NULL(pfs) || IS_NULL(ptotal_size) || IS_NULL(pfree_size))
    {
        return -1;
    }
#if defined(FS_FATFS)
    uint16_t              index = pfs->index;
    const bFSPartition_t *tmp   = NULL;
    tmp                         = _bFSFindPartition(index);
    if (tmp == NULL)
    {
        return -1;
    }
    uint32_t fre_sect, tot_sect;
    DWORD    fclst  = 0;
    FATFS   *pfatfs = NULL;
    uint8_t  disk_str[8];
    sprintf((char *)disk_str, "%d:", pfs->index);
    FRESULT ret = f_getfree((const char *)disk_str, &fclst, &pfatfs);
    if (ret)
    {
        return -1;
    }
    tot_sect     = (pfatfs->n_fatent - 2) * pfatfs->csize;
    fre_sect     = fclst * pfatfs->csize;
    *ptotal_size = tot_sect * tmp->sector_size;
    *pfree_size  = fre_sect * tmp->sector_size;
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

int bFSFileGetInfo(bFS_t *pfs, bFSFile_t *pfile, uint32_t *pfile_size)
{
    if (IS_NULL(pfs) || IS_NULL(pfile) || IS_NULL(pfile_size))
    {
        return -1;
    }
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
