#ifndef __REQUEST_H__
#define __REQUEST_H__

typedef struct stati_t {
    int id;
    int stat_dyn;
    int stat_stc;
} *Stati;

typedef struct Thread_t {
    int id;
    struct timeval init_time;
    struct timeval free_time;
} *Thread;

void requestHandle(Thread thread, Stati stat);

#endif
