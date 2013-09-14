#ifndef __comm_h__
#define __comm_h__


void set_debug_verbosity(int32_t db_lvl, uint16_t db_flags);
void dbprintf(FILE *out,const uint32_t level, const uint16_t flags,const char *fmt, ...);

#define DB_PRINT_ALWAYS 0x01
#define DB_COLOR_FLAG	0x02
#define DB_CONGWIN_FLAG 0x04
#define DB_LINKLATER_FLAG 0x08
#define DB_SWP_OPTS_FLAG  0x10

#endif
