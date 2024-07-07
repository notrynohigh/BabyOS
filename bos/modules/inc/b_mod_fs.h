/**
 *!
 * \file        b_mod_fs.h
 * \version     v0.0.1
 * \date        2020/06/02
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
#ifndef __B_MOD_FS_H__
#define __B_MOD_FS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_FS_ENABLE) && (_FS_ENABLE == 1))

#if (defined(FS_FATFS))
#include "thirdparty/fatfs/ff.h"
#endif

#if (defined(FS_LITTLEFS))
#include "thirdparty/littlefs/lfs.h"
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
 * \defgroup FS_Exported_TypesDefinitions
 * \{
 */
typedef struct
{
    uint16_t index;
    uint32_t dev_no;
    uint32_t base_addr;
    uint32_t total_size;
    uint16_t sector_size;
} bFSPartition_t;

typedef struct
{
    uint8_t  used;
    uint16_t index;
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

typedef struct
{
#if defined(FS_FATFS)
    FIL bfile;
#endif
#if defined(FS_LITTLEFS)
    struct lfs_file_config cfg;
    lfs_file_t             bfile;
    uint8_t                buf[LFS_CACHE_SIZE];
#endif
} bFSFile_t;

/**
 * \}
 */

/**
 * \defgroup FS_Exported_Defines
 * \{
 */
#define BFS_O_RD (0x1)
#define BFS_O_WR (0x2)
#define BFS_O_RDWR (0x4)
#define BFS_O_CREAT (0x8)
#define BFS_O_EXCL (0x10)
#define BFS_O_TRUNC (0x20)
#define BFS_O_APPEND (0x40)

#define BFS_SEEK_SET (0x1)
#define BFS_SEEK_CUR (0x2)
#define BFS_SEEK_END (0x3)

#ifndef FS_BLOCK_SIZE
// Number of minimum erasure units
#define FS_BLOCK_SIZE (1)
#endif

/**
 * \}
 */

/**
 * \defgroup FS_Exported_Functions
 * \{
 */

int bFSInit(const bFSPartition_t *partition, uint16_t partition_number);
/**
 * \brief
 * \param pfs
 * \param index
 * \param mkfs (1)Format if Mount Fails
 * \return int
 */
int bFSMount(bFS_t **pfs, uint16_t index, uint8_t mkfs);
int bFSUnmount(bFS_t *pfs);
int bFSOpen(bFS_t *pfs, bFSFile_t *pfile, const char *path, int flag);
int bFSWrite(bFS_t *pfs, bFSFile_t *pfile, uint8_t *pbuf, uint32_t len, uint32_t *wlen);
int bFSRead(bFS_t *pfs, bFSFile_t *pfile, uint8_t *pbuf, uint32_t len, uint32_t *rlen);
int bFSClose(bFS_t *pfs, bFSFile_t *pfile);
int bFSLseek(bFS_t *pfs, bFSFile_t *pfile, int32_t offset, int whence);
int bFSMkfs(bFS_t *pfs);

int bFSGetInfo(bFS_t *pfs, uint32_t *ptotal_size, uint32_t *pfree_size);
int bFSFileGetInfo(bFS_t *pfs, bFSFile_t *pfile, uint32_t *pfile_size);

int bFSGetPartitionState(uint16_t index);
int bFSGetPartitionInfo(uint16_t index, const bFSPartition_t **partition);
int bFSPartitionRead(uint16_t index, uint32_t offset, uint8_t *pbuf, uint32_t len);
int bFSPartitionWrite(uint16_t index, uint32_t offset, uint8_t *pbuf, uint32_t len);
int bFSPartitionErase(uint16_t index, uint32_t offset, uint32_t len);

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

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
