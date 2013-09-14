
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include "transport_meta.h"
#include "comm.h"

static uint32_t DEBUG_LEVEL;
static uint16_t DEBUG_FLAGS;

void set_debug_verbosity(int32_t db_lvl, const uint16_t db_flags)
{
    DEBUG_LEVEL = URANGE(0,db_lvl,3);
    DEBUG_FLAGS = db_flags;
    SET_BIT(DEBUG_FLAGS, DB_PRINT_ALWAYS);
}

void dbprintf(FILE *out,const uint32_t level, const uint16_t flags,const char *fmt, ...)
{
    if( ! IS_SET( DEBUG_FLAGS, flags ))
        return;
    if( level > DEBUG_LEVEL )
        return;
    va_list ap;
    va_start(ap,fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
}
