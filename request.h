#ifndef __REQUEST_H__

void requestHandle(int fd,Stat stat);

typedef struct stati {
    int id;
    int stat_dyn;
    int stat_stc;
} *Stati;

#endif
