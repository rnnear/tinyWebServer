#include"rnhead.h"

#ifndef RNTOOLS_H
#define RNTOOLS_H
#define RNMAXLINE 1024
#define RNMAXBUF 4096
#define RNIO_BUFSIZE 8192
#define RNLISTENQ 10
typedef struct sockaddr SA;
typedef struct{
	int rnio_fd;				//file descriptor for this internal buf
	int rnio_cnt;				//unread bytes in internal buf
	char *rnio_bufptr; //next unread byte in internal buf
	char rnio_buf[RNIO_BUFSIZE];
} rnio_t;
pid_t rnfork();
int rngetaddrinfo(const char *host, const char *service,
		const struct addrinfo *hints, struct addrinfo **result);

void rnunix_error(const char str[]);
ssize_t rnsio_puts(const char s[]);
ssize_t rnsio_puts2(const char s[]);
ssize_t rnio_readn(int fd, void *usrbuf, size_t n);
ssize_t rnio_writen(int fd, void *usrbuf, size_t n);
void rnio_readinitb(rnio_t *rnptr, int fd);
ssize_t rnio_readlineb(rnio_t *rnptr, void *usrbuf, size_t maxlen); //read bytes till maxlen-1, include '\n', '\0' will be added.
ssize_t rnio_readnb(rnio_t *rnptr, void *usrbuf, size_t n);
int rnopen_clientfd(char *hostname, char *port);
int rnopen_listenfd(const char* port);
void setnonblocked(int fd);
void rnio_retrieve(rnio_t *ptr, int cnt);
#endif

