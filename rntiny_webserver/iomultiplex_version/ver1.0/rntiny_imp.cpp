#include"rntiny_imp.h"

bool rnstrcasecmp(const char *str1, const char *str2)
{ 
	bool sign = true;
	while(sign&&*str1 != 0&&*str2 != 0)
	{
		if(*str1 != *str2&&*str1 != (*str2 + 32))
			sign = false;
		++str1;
		++str2;
	}
	return sign;
}
void rnget_filetype(const char *filename, char *filetype)
{
	if(strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if(strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if(strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if(strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else
		strcpy(filetype, "text/plain");
}
void rnserve_dynamic(int fd, char *filename, char *cgiargs)
{
	char buf[RNMAXLINE], *emptyargs[1];
	emptyargs[0] = nullptr;
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	rnio_writen(fd, buf, strlen(buf));

	if(rnfork() == 0)
	{
		setenv("QUERY_STRING", cgiargs, 1);
		if(dup2(fd, STDOUT_FILENO) < 0)
		{
			fprintf(stderr, "fail to redirect with dup2()\n");
			return;
		}
		if(execve(filename, emptyargs, environ) < 0)
		{
			fprintf(stderr, "fail to execute cgi with execve()\n");
			return;
		}
	}
	pid_t pid;
	if((pid = wait(NULL)) < 0)
	{
		fprintf(stderr, "fail to wait subprocess with wait()\n");
		return;
	}
	printf("subprocess pid %d completed\n", static_cast<int>(pid));
}
void rnserve_static(int fd, const char *filename, int filesize)
{
	int filefd;
	char filetype[RNMAXLINE], buf[RNMAXBUF];
	rnget_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	rnio_writen(fd, buf, strlen(buf));
	//printf("Response headers:\n");
	//printf("%s", buf);

	if((filefd = open(filename, O_RDONLY, 0))<0)
	{
		fprintf(stderr, "fail to open file(%s) in the rnserve_static()\n", filename);
		return;
	}
	void *filemapp = nullptr;
	if((filemapp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, filefd, 0)) == (void*)(-1))
	{
		fprintf(stderr, "fail to map file(%s) with mmap()\n", filename);
		return;
	}
	close(filefd);
	rnio_writen(fd, filemapp, filesize);
	if(munmap(filemapp, filesize)<0)
		fprintf(stderr, "fail to unmap virtual memory with munmap()\n");
}
bool rnparse_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr = nullptr;
	if(!strstr(uri, "cgi-bin"))
	{//static content
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		if((strlen(uri) == 1)&&uri[strlen(uri)-1] == '/')  //default page
			strcat(filename, "home.html");
		return true;
	}else //dynamic content
	{
		ptr = strchr(uri, '?');
		if(ptr)
		{
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}else
			strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		return false;
	}
}
void rnread_requesthdrs(rnio_t *rniop)
{
	char buf[RNMAXLINE];
	rnio_readlineb(rniop, buf, RNMAXLINE);
	while(strcmp(buf, "\r\n"))
	{
		printf("%s", buf);
		rnio_readlineb(rniop, buf, RNMAXLINE);
	}
	return;
}
void rnclienterror(int fd, const char *cause, const char *errnum,
		const char *shortmsg, const char *longmsg)
{
	char buf[RNMAXLINE], body[RNMAXBUF];
	//build http response body
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	//print http response
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	rnio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	rnio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	rnio_writen(fd, buf, strlen(buf));
	rnio_writen(fd, body, strlen(body));
}
void rndoit(int fd)
{
	bool is_static = true;
	struct stat stat_buf;
	char buf[RNMAXLINE],
		 method[RNMAXLINE],
		 uri[RNMAXLINE],
		 version[RNMAXLINE];
	rnio_t rniob;
	rnio_readinitb(&rniob, fd);
	rnio_readlineb(&rniob, buf, RNMAXLINE);
	printf("Request headers:\n");
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);
	if(!rnstrcasecmp(method, "GET"))
	{
		rnclienterror(fd, method, "501", "Not implemented",
				"Tiny does not implement this method");
		return;
	}
	rnread_requesthdrs(&rniob);
	
	char filename[RNMAXLINE], cgiargs[RNMAXLINE];
	is_static = rnparse_uri(uri, filename, cgiargs);
	if(stat(filename, &stat_buf)<0)
	{
		rnclienterror(fd, filename, "404", "Not found",
				"Tiny couldn't find this file");
		return;
	}
	if(is_static)  //serve static content
	{
		if(!(S_ISREG(stat_buf.st_mode))||!(S_IRUSR & stat_buf.st_mode))
		{
			rnclienterror(fd, filename, "403", "Forbidden",
					"Tiny couldn't read the file");
			return;
		}
		rnserve_static(fd, filename, stat_buf.st_size);
	}else //serve dynamic content
	{
		if(!(S_ISREG(stat_buf.st_mode))||!(S_IXUSR & stat_buf.st_mode))
		{
			rnclienterror(fd, filename, "403", "Forbidden",
					"Tiny couldn't run the CGI program");
			return;
		}
		rnserve_dynamic(fd, filename, cgiargs);
	}
}
