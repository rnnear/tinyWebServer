#include"rntools.h"
#ifndef RNTINY_IMP_H
#define RNTINY_IMP_H
#define RNMAXLINE 1024
#define RNMAXBUF 4096
#define RNSOCKADDRLEN 20
typedef struct sockaddr SA;
void rndoit(int fd);
void rnread_requesthdrs(rnio_t *rniop);
bool rnparse_uri(char *uri, char *filename, char *cgiargs);
void rnserve_static(int fd, const char *filename, int filesize);
void rnget_filetype(char *filename, char *filetype);
void rnserve_dynamic(int fd, char *filename, char *cgiargs);
void rnclienterror(int fd, const char *cause, const char *errnum,
		const char *shortmsg, const char *longmsg);
bool rnstrcasecmp(const char *str1, const char *str2);
void rnget_filetype(const char *filename, char *filetype);
#endif
