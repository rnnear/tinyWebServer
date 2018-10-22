//
#include"rntools.h"
#include"rntiny_imp.h"

int main(int argc, char *argv[])
{
	int listenfd, connfd;
	pid_t pid;
	if(argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	if((listenfd = rnopen_listenfd(argv[1]))<0)
		rnunix_error("rnopen_listenfd");
	//struct sockaddr_t clientaddr;
	char clientaddr[RNSOCKADDRLEN];
	socklen_t clientlen = sizeof(clientaddr);
	char hostname[RNMAXLINE], port[RNMAXLINE];
	if(signal(SIGCHLD, rnsighander_child) == SIG_ERR)
		rnunix_error("signal()");
	while(true)
	{
		if((connfd = accept(listenfd, (SA *)clientaddr, &clientlen)) < 0)
			rnunix_error("accept");
		int tmp;
		if((tmp = getnameinfo((SA *)clientaddr, clientlen,
						hostname, RNMAXLINE,
						port, RNMAXLINE, 
						NI_NUMERICHOST|NI_NUMERICSERV)) != 0)
		{
			fprintf(stderr, "%s", gai_strerror(tmp));
			exit(0);
		}
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		if((pid = rnfork()) == 0)
		{
			rndoit(connfd);
			close(connfd);
			printf("connection (%s, %s) break.\n", hostname, port);
			exit(0);
		}
	}
	exit(0);
}
