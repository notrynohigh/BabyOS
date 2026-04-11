#ifndef __B_UTIL_TOOLS_H__
#define __B_UTIL_TOOLS_H__

#include "b_config.h"

int bParseString(const char *string, const char *delimiter, uint8_t index, int32_t *pnumber,
                 const char **pstr);

#endif /* __B_UTIL_TOOLS_H__ */
