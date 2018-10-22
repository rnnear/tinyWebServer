#include"rntools.h"

static ssize_t rnio_read(rnio_t *rnptr, unsigned char *usrbuf, size_t n); //linux read version with buf
int rngetaddrinfo(const char *host, const char *service,
		const struct addrinfo *hints, struct addrinfo **result)
{
	int rnret;
	if((rnret = getaddrinfo(host, service, hints, result))!=0)
	{
		fprintf(stderr, "%s", gai_strerror(rnret));
		//std::cerr<<gai_strerror(rnret)<<std::endl;
		exit(0);
	}else
		return rnret;
}

void rnunix_error(const char str[])
{
	perror(str);
	exit(0);
}
pid_t rnfork()
{
	pid_t pid;
	if((pid = fork()) < 0)
	{
		perror("fork");
		exit(0);
	}
	return pid;
}

size_t rnsio_strlen(const char s[])
{
	size_t rnlen = 0;
	while(s[rnlen++] != '\0');
	return rnlen;
}
ssize_t rnsio_puts(const char s[])
{
	int prev_errno = errno;
	sigset_t mask, prev_mask;
	if(sigfillset(&mask) < 0)
	{
		char s[] = "sigfillset error.\n";
		write(STDOUT_FILENO,s, rnsio_strlen(s));
		_exit(1);
	}
	if(sigprocmask(SIG_BLOCK, &mask, &prev_mask)<0)
	{
		char s[] = "sigprocmask error.\n";
		write(STDOUT_FILENO,s, rnsio_strlen(s));
		_exit(1);
	}
	printf("%s", s);
	if(sigprocmask(SIG_SETMASK, &prev_mask, NULL)<0)
	{
		char s[] = "sigprocmask error.\n";
		write(STDOUT_FILENO,s, rnsio_strlen(s));
		exit(1);
	}
	errno = prev_errno;
	return 0;
}
ssize_t rnsio_puts2(const char s[])
{return write(STDOUT_FILENO, s, rnsio_strlen(s));}
ssize_t rnio_readn(int fd, void *usrbuf, size_t n)
{
	size_t nleft = n;
	ssize_t nread = 0;
	unsigned char *ptr = reinterpret_cast<unsigned char*>(usrbuf);
	while(nleft > 0)
	{
		if((nread = read(fd, ptr, nleft)) < 0)
		{
			if(errno == EINTR)
				nread = 0;
			else
				return -1;
		}else if(nread == 0)
			break;
		nleft -= nread;
		ptr += nread;
	}
	return (n - nleft);
}
ssize_t rnio_writen(int fd, void *usrbuf, size_t n)
{
	size_t nleft = n;
	ssize_t nwritten = 0;
	unsigned char *ptr = reinterpret_cast<unsigned char*>(usrbuf);
	while(nleft>0)
	{
		if((nwritten = write(fd, ptr, nleft))<0)
		{
			if(errno == EINTR)
				nwritten = 0;
			else
				return -1;
		}
		nleft -= nwritten;
		ptr += nwritten;
	}
	return (n - nleft);
}
void rnio_readinitb(rnio_t *rnptr, int fd)
{
	rnptr->rnio_fd = fd;
	rnptr->rnio_cnt = 0;
	rnptr->rnio_bufptr = rnptr->rnio_buf;
}
static ssize_t rnio_read(rnio_t *rnptr, unsigned char *usrbuf, size_t n) //linux read version with buf
{
	while(rnptr->rnio_cnt <= 0)
	{
		rnptr->rnio_cnt = read(rnptr->rnio_fd, rnptr->rnio_buf, sizeof(rnptr->rnio_buf));
		if(rnptr->rnio_cnt < 0)
		{
			if(errno != EINTR)
				return -1;
		}else if(rnptr->rnio_cnt == 0)
			return 0;
		else
			rnptr->rnio_bufptr = rnptr->rnio_buf;
	}
	int cnt = n;
	if(rnptr->rnio_cnt < cnt)
		cnt = rnptr->rnio_cnt;
	memcpy(usrbuf, rnptr->rnio_bufptr, cnt);
	rnptr->rnio_cnt -= cnt;
	rnptr->rnio_bufptr += cnt;
	return cnt;
}
ssize_t rnio_readlineb(rnio_t *rnptr, void *usrbuf, size_t maxlen)
{
	unsigned char c, *rnbuf = reinterpret_cast<unsigned char*>(usrbuf);
	int res;
	size_t n;
	for(n = 1; n < maxlen; ++n)
	{
		if((res = rnio_read(rnptr, &c, 1)) == 1)
		{
			*(rnbuf++) = c;
			if(c == '\n')
			{
				++n;
				break;
			}
		}else if(res == 0)
		{
			if(n == 1)
				return 0;
			else
				break;
		}else
			return -1;
	}
	*rnbuf = 0;
	return n - 1;
}
ssize_t rnio_readnb(rnio_t *rnptr, void *usrbuf, size_t n)
{
	size_t nleft = n;
	ssize_t nread;
	unsigned char *ptr = reinterpret_cast<unsigned char *>(usrbuf);
	while(nleft > 0)
	{
		if((nread = rnio_read(rnptr, ptr, nleft))<0)
			return -1;
		else if(nread == 0)
			break;
		nleft -= nread;
		ptr += nread;
	}
	return (n - nleft);
}
int rnopen_clientfd(char *hostname, char *port)
{
	int rnsockfd;
	struct addrinfo rnhints, *res_list, *ptr;
	memset(&rnhints, 0, sizeof(struct addrinfo));
	//rnhints.ai_family = AF_INET;	   //only ipv4 addr
	rnhints.ai_socktype = SOCK_STREAM; //only connected socket address
	rnhints.ai_flags = AI_ADDRCONFIG;  //according host configuration
	rnhints.ai_flags |= AI_NUMERICSERV;//only port number
	rngetaddrinfo(hostname, port, &rnhints, &res_list);

	for(ptr = res_list; ptr; ptr = ptr->ai_next)
	{
		if((rnsockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0)
			continue;
		if(connect(rnsockfd, ptr->ai_addr, ptr->ai_addrlen)==0)
			break;
		close(rnsockfd);
	}
	freeaddrinfo(res_list);
	if(!ptr)
		return -1;
	else
		return rnsockfd;
}
int rnopen_listenfd(const char* port)
{
	struct addrinfo rnhints, *res_list, *ptr;
	int rnsockfd;
	memset(&rnhints, 0, sizeof(decltype(rnhints)));
	rnhints.ai_socktype = SOCK_STREAM;
	rnhints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG|AI_NUMERICSERV;
	rngetaddrinfo(NULL, port, &rnhints, &res_list);

	for(ptr = res_list; ptr; ptr = ptr->ai_next)
	{
		if((rnsockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol))<0)
			continue;

		int rnoptval = 1;
		if(setsockopt(rnsockfd, SOL_SOCKET, SO_REUSEADDR, &rnoptval, sizeof(int))<0)
		{
			close(rnsockfd);
			continue;
		}
		if(bind(rnsockfd, ptr->ai_addr, ptr->ai_addrlen) == 0)
			break;
		close(rnsockfd);
	}
	freeaddrinfo(res_list);
	if(!ptr)
		return -1;
	if(listen(rnsockfd, RNLISTENQ) < 0)
	{
		close(rnsockfd);
		return -1;
	}
	return rnsockfd;
}

