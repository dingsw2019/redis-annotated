#ifndef __ZIPLIST_H
#define __ZIPLIST_H

// 迭代方向
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1


static unsigned char *ziplistResize(unsigned char *zl, unsigned int len);
unsigned char *ziplistPush(unsigned char *zl,unsigned char *s,unsigned int slen,int where);

#endif