#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"

/*--------------------- privdata --------------------*/



/*--------------------- API --------------------*/



/*--------------------- debug --------------------*/
#include <sys/time.h>
#include <assert.h>
#include "adlist.h"
#include "sds.h"

// unsigned char *createList() {
//     unsigned char *zl = ziplistNew();
//     zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
//     zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
//     zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
//     zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
//     return zl;
// }

// unsigned char *createIntList() {
//     unsigned char *zl = ziplistNew();
//     char buf[32];

//     sprintf(buf, "100");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "128000");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "-100");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
//     sprintf(buf, "4294967296");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
//     sprintf(buf, "non integer");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "much much longer non integer");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     return zl;
// }

// long long usec(void) {
//     struct timeval tv;
//     gettimeofday(&tv,NULL);
//     return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
// }

// void stress(int pos, int num, int maxsize, int dnum) {
//     int i,j,k;
//     unsigned char *zl;
//     char posstr[2][5] = { "HEAD", "TAIL" };
//     long long start;
//     for (i = 0; i < maxsize; i+=dnum) {
//         zl = ziplistNew();
//         for (j = 0; j < i; j++) {
//             zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
//         }

//         /* Do num times a push+pop from pos */
//         start = usec();
//         for (k = 0; k < num; k++) {
//             zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
//             zl = ziplistDeleteRange(zl,0,1);
//         }
//         printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
//             i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
//         zfree(zl);
//     }
// }

// void pop(unsigned char *zl, int where) {
//     unsigned char *p, *vstr;
//     unsigned int vlen;
//     long long vlong;

//     p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
//     if (ziplistGet(p,&vstr,&vlen,&vlong)) {
//         if (where == ZIPLIST_HEAD)
//             printf("Pop head: ");
//         else
//             printf("Pop tail: ");

//         if (vstr)
//             if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
//         else
//             printf("%lld", vlong);

//         printf("\n");
//         ziplistDeleteRange(zl,-1,1);
//     } else {
//         printf("ERROR: Could not pop\n");
//         exit(1);
//     }
// }

// int randstring(char *target, unsigned int min, unsigned int max) {
//     int p = 0;
//     int len = min+rand()%(max-min+1);
//     int minval, maxval;
//     switch(rand() % 3) {
//     case 0:
//         minval = 0;
//         maxval = 255;
//     break;
//     case 1:
//         minval = 48;
//         maxval = 122;
//     break;
//     case 2:
//         minval = 48;
//         maxval = 52;
//     break;
//     default:
//         assert(NULL);
//     }

//     while(p < len)
//         target[p++] = minval+rand()%(maxval-minval+1);
//     return len;
// }

// void verify(unsigned char *zl, zlentry *e) {
//     int i;
//     int len = ziplistLen(zl);
//     zlentry _e;

//     for (i = 0; i < len; i++) {
//         memset(&e[i], 0, sizeof(zlentry));
//         e[i] = zipEntry(ziplistIndex(zl, i));

//         memset(&_e, 0, sizeof(zlentry));
//         _e = zipEntry(ziplistIndex(zl, -len+i));

//         assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
//     }
// }

// void ziplistRepr(unsigned char *zl) {
//     unsigned char *p;
//     int index = 0;
//     zlentry entry;

//     printf(
//         "{total bytes %d} "
//         "{length %u}\n"
//         "{tail offset %u}\n",
//         intrev32ifbe(ZIPLIST_BYTES(zl)),
//         intrev16ifbe(ZIPLIST_LENGTH(zl)),
//         intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
//     p = ZIPLIST_ENTRY_HEAD(zl);
//     while(*p != ZIP_END) {
//         entry = zipEntry(p);
//         printf(
//             "{"
//                 "addr 0x%08lx, "
//                 "index %2d, "
//                 "offset %5ld, "
//                 "rl: %5u, "
//                 "hs %2u, "
//                 "pl: %5u, "
//                 "pls: %2u, "
//                 "payload %5u"
//             "} ",
//             (long unsigned)p,
//             index,
//             (unsigned long) (p-zl),
//             entry.headersize+entry.len,
//             entry.headersize,
//             entry.prevrawlen,
//             entry.prevrawlensize,
//             entry.len);
//         p += entry.headersize;
//         if (ZIP_IS_STR(entry.encoding)) {
//             if (entry.len > 40) {
//                 if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
//                 printf("...");
//             } else {
//                 if (entry.len &&
//                     fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
//             }
//         } else {
//             printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
//         }
//         printf("\n");
//         p += entry.len;
//         index++;
//     }
//     printf("{end}\n\n");
// }

// gcc -g zmalloc.c sds.c util.c ziplist.c
int main(void) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    // if (argc == 2)
        // srand(atoi(argv[1]));
    srand(time(NULL));

    // zl = createIntList();
    // ziplistRepr(zl);

    // zl = createList();
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_HEAD);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // printf("Get element at index 3:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 3);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index 3\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index 4 (out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -1 (last element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -1\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -4 (first element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -4\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -5 (reverse out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -5);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 0 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 0);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 1 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 2 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 2);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate starting out of range:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front, deleting all items:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         zl = ziplistDelete(zl,&p);
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Delete inclusive range 0,0:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 0,1:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 1,2:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with start index out of range:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 5, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with num overflow:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 5);
    //     ziplistRepr(zl);
    // }

    // printf("Delete foo while iterating:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     while (ziplistGet(p,&entry,&elen,&value)) {
    //         if (entry && strncmp("foo",(char*)entry,elen) == 0) {
    //             printf("Delete foo\n");
    //             zl = ziplistDelete(zl,&p);
    //         } else {
    //             printf("Entry: ");
    //             if (entry) {
    //                 if (elen && fwrite(entry,elen,1,stdout) == 0)
    //                     perror("fwrite");
    //             } else {
    //                 printf("%lld",value);
    //             }
    //             p = ziplistNext(zl,p);
    //             printf("\n");
    //         }
    //     }
    //     printf("\n");
    //     ziplistRepr(zl);
    // }

    // printf("Regression test for >255 byte strings:\n");
    // {
    //     char v1[257],v2[257];
    //     memset(v1,'x',256);
    //     memset(v2,'y',256);
    //     zl = ziplistNew();
    //     zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
    //     zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

    //     /* Pop values again and compare their value. */
    //     p = ziplistIndex(zl,0);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v1,(char*)entry,elen) == 0);
    //     p = ziplistIndex(zl,1);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v2,(char*)entry,elen) == 0);
    //     printf("SUCCESS\n\n");
    // }

    // printf("Regression test deleting next to last entries:\n");
    // {
    //     char v[3][257];
    //     zlentry e[3];
    //     int i;

    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         memset(v[i], 'a' + i, sizeof(v[0]));
    //     }

    //     v[0][256] = '\0';
    //     v[1][  1] = '\0';
    //     v[2][256] = '\0';

    //     zl = ziplistNew();
    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
    //     }

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);
    //     assert(e[2].prevrawlensize == 1);

    //     /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
    //     unsigned char *p = e[1].p;
    //     zl = ziplistDelete(zl, &p);

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);

    //     printf("SUCCESS\n\n");
    // }

    // printf("Create long list and check indices:\n");
    // {
    //     zl = ziplistNew();
    //     char buf[32];
    //     int i,len;
    //     for (i = 0; i < 1000; i++) {
    //         len = sprintf(buf,"%d",i);
    //         zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
    //     }
    //     for (i = 0; i < 1000; i++) {
    //         p = ziplistIndex(zl,i);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(i == value);
    //         p = ziplistIndex(zl,-i-1);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(999-i == value);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Compare strings with ziplist entries:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
    //         printf("ERROR: not \"hello\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"hella",5)) {
    //         printf("ERROR: \"hella\"\n");
    //         return 1;
    //     }

    //     p = ziplistIndex(zl,3);
    //     if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
    //         printf("ERROR: not \"1024\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"1025",4)) {
    //         printf("ERROR: \"1025\"\n");
    //         return 1;
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with random payloads of different encoding:\n");
    // {
    //     int i,j,len,where;
    //     unsigned char *p;
    //     char buf[1024];
    //     int buflen;
    //     list *ref;
    //     listNode *refnode;

    //     /* Hold temp vars from ziplist */
    //     unsigned char *sstr;
    //     unsigned int slen;
    //     long long sval;

    //     for (i = 0; i < 20000; i++) {
    //         zl = ziplistNew();
    //         ref = listCreate();
    //         listSetFreeMethod(ref,sdsfree);
    //         len = rand() % 256;

    //         /* Create lists */
    //         for (j = 0; j < len; j++) {
    //             where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
    //             if (rand() % 2) {
    //                 buflen = randstring(buf,1,sizeof(buf)-1);
    //             } else {
    //                 switch(rand() % 3) {
    //                 case 0:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) >> 20);
    //                     break;
    //                 case 1:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()));
    //                     break;
    //                 case 2:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) << 20);
    //                     break;
    //                 default:
    //                     assert(NULL);
    //                 }
    //             }

    //             /* Add to ziplist */
    //             zl = ziplistPush(zl, (unsigned char*)buf, buflen, where);

    //             /* Add to reference list */
    //             if (where == ZIPLIST_HEAD) {
    //                 listAddNodeHead(ref,sdsnewlen(buf, buflen));
    //             } else if (where == ZIPLIST_TAIL) {
    //                 listAddNodeTail(ref,sdsnewlen(buf, buflen));
    //             } else {
    //                 assert(NULL);
    //             }
    //         }

    //         assert(listLength(ref) == ziplistLen(zl));
    //         for (j = 0; j < len; j++) {
    //             /* Naive way to get elements, but similar to the stresser
    //              * executed from the Tcl test suite. */
    //             p = ziplistIndex(zl,j);
    //             refnode = listIndex(ref,j);

    //             assert(ziplistGet(p,&sstr,&slen,&sval));
    //             if (sstr == NULL) {
    //                 buflen = sprintf(buf,"%lld",sval);
    //             } else {
    //                 buflen = slen;
    //                 memcpy(buf,sstr,buflen);
    //                 buf[buflen] = '\0';
    //             }
    //             assert(memcmp(buf,listNodeValue(refnode),buflen) == 0);
    //         }
    //         zfree(zl);
    //         listRelease(ref);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with variable ziplist size:\n");
    // {
    //     stress(ZIPLIST_HEAD,100000,16384,256);
    //     stress(ZIPLIST_TAIL,100000,16384,256);
    // }

    return 0;
}