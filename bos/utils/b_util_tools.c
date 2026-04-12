#include "utils/inc/b_util_tools.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 按分隔符分割字符串并提取指定索引段数据
 * @param string 输入字符串
 * @param delimiter 分隔符
 * @param index 要提取的段索引（从0开始）
 * @param pnumber 输出：提取段转换后的整数值（无法转换则为0）
 * @param pstr 输出：提取段字符串的起始指针
 * @return 0-成功，-1-索引越界，-2-入参非法
 */
int bParseString(const char *string, const char *delimiter, uint8_t index, int32_t *pnumber,
                 const char **pstr)
{
    if (string == NULL || delimiter == NULL || pnumber == NULL || pstr == NULL)
    {
        return -2;
    }

    uint8_t     delim_len     = strlen(delimiter);
    const char *current_pos   = string;  // 当前遍历起始位置
    const char *segment_start = string;  // 目标段起始位置
    uint8_t     current_index = 0;       // 当前遍历到的段索引
    *pnumber                  = 0;       // 默认值初始化

    while (1)
    {
        const char *delim_pos = strstr(current_pos, delimiter);
        if (delim_pos != NULL && current_index < index)
        {
            current_index++;                        // 索引+1
            current_pos   = delim_pos + delim_len;  // 下一段起始位置（跳过分隔符）
            segment_start = current_pos;            // 更新段起始位置
            continue;
        }
        if (delim_pos != NULL && current_index == index)
        {
            *pstr               = segment_start;
            char   temp_buf[32] = {0};
            size_t segment_len  = delim_pos - segment_start;
            if (segment_len > sizeof(temp_buf) - 1)
            {
                segment_len = sizeof(temp_buf) - 1;
            }
            strncpy(temp_buf, segment_start, segment_len);

            char     *end_ptr = NULL;
            long long num     = strtoll(temp_buf, &end_ptr, 10);
            if (end_ptr != temp_buf && *end_ptr == '\0' && num >= INT32_MIN && num <= INT32_MAX)
            {
                *pnumber = (int32_t)num;
            }
            return 0;
        }

        if (delim_pos == NULL)
        {
            if (current_index == index)
            {
                *pstr               = segment_start;
                char   temp_buf[32] = {0};
                size_t segment_len  = strlen(segment_start);
                if (segment_len > sizeof(temp_buf) - 1)
                {
                    segment_len = sizeof(temp_buf) - 1;
                }
                strncpy(temp_buf, segment_start, segment_len);

                char     *end_ptr = NULL;
                long long num     = strtoll(temp_buf, &end_ptr, 10);
                if (end_ptr != temp_buf && *end_ptr == '\0' && num >= INT32_MIN && num <= INT32_MAX)
                {
                    *pnumber = (int32_t)num;
                }
                return 0;
            }
            else
            {
                return -1;
            }
        }
    }
}
