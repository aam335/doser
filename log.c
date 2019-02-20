#include <ev.h>

ev_tstamp log_start;
int log_level;

void before_main(void) __attribute__((constructor (101)));
void before_main(void)
{
    log_start=ev_time();
}
