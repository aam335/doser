#include <stdio.h>
#include <ev.h>
extern ev_tstamp log_start;
extern int log_level;
// debug printf w/timestamsp
#define dbgp(fmt, ...) {printf("%.4f %s [%i]:" fmt "\n",ev_time()-log_start,__FUNCTION__,__LINE__,##__VA_ARGS__);}
#define dbgpl(level,fmt, ...) if (log_level>level) dbgp(fmt,##__VA_ARGS__)
// console printf w/timestamp
#define conp(fmt, ...) {printf("%.4f " fmt "\n",ev_time()-log_start,##__VA_ARGS__);}

#define err_exit(fmt, ...) {dbgp(fmt,##__VA_ARGS__);exit(errno);}