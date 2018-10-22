//
#include"rntools.h"
#include"rntiny_imp.h"
#define WORKER_NUM 10
#define MASTER_LISTEN_NUM 1 //only 1
#define WORKER_LISTEN_NUM 100
#define WORKER_BALANCE_FACTOR (7/10)
#define SOCKADDR_LENGTH 30
#define LOG_PATH "tinyweb.log"

class myevents
{
public:
	myevents(int nfd): fd(nfd), fin(2), pos(0){rnio_readinitb(&rniob,fd);}
	~myevents() = default;
	bool addline(const std::string& str)
	{
		buf.push_back(str);
		//if(fin == 1)
		//{
	
		//}
		if(str == "\r\n") 
		{
			pos = buf.size()-1;
			return true;
		}
		return false;
	}
	int read()
	{
		char nbuf[RNMAXLINE];
		int ret = 0;
		int s;
		while(!ret)
		{
			memset(nbuf, 0, RNMAXLINE);
			if((s = rnio_readlineb(&rniob, nbuf, RNMAXLINE)) < 0)
			{
#ifndef DEBUG
				printf("=========readlineb ret: %d, buf:%s, bufsize:%d\n", s, nbuf,strlen(nbuf));
#endif
				int len = strlen(nbuf);
				if(len == 0)
					return ret;  //EAGAIN error
				else
				{
					rnio_retrieve(&rniob, len); //didn't meet '\n' between a line
					return ret;
				}
			}else if(s == 0)
				return -1; //client close connection
			else
			{
#ifndef DEBUG
				printf("receive line:%s", nbuf);
#endif
				ret = (addline(std::string(nbuf)))?1:0;
			}
				//rnunix_error("rnio_readlineb, client error");
		}
		return ret;
	}
	int getfd()const{return fd;}
	int getpos()const{return pos;}
	std::string& operator[](size_t pos) {return buf[pos];}
	size_t getsize(){return buf.size();}
	void setip(std::string str){ip = str;}
	const char* getip(){return ip.c_str();}
	void setport(std::string str){port = str;}
	const char* getport(){return port.c_str();}
private:
	int fd;
	int fin;
	int pos; //"\r\n" position
	std::string ip;
	std::string port;
	rnio_t rniob;
	std::vector<std::string> buf;
//
};
rnlisten_block *listen_ptr = NULL;
unsigned long* accept_cnt;
int main(int argc, char *argv[])
{
	int listenfd, s;
	if(argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	FILE *log_file_ptr = fopen(LOG_PATH, "a"); //append log file
	char time_tmp[30];
	if(NULL == log_file_ptr)
		rnunix_error("fopen");
	else
	{
		time_t tt = time((time_t*)NULL);
		if(NULL == ctime_r(&tt, time_tmp))
			rnunix_error("ctime_r");
		fprintf(log_file_ptr, "---- tinyweb start at %s", time_tmp);
	}
	fflush(log_file_ptr);
	const char *shm_name = "/shm_tiny_web";
	if((s = shm_open(shm_name, O_RDWR|O_CREAT, 0660))<0) //
		rnunix_error("shm_open");
	int fd_size = (MASTER_LISTEN_NUM*sizeof(rnlisten_block) +sizeof(unsigned long)+8)/getpagesize() + 1;
	if(ftruncate(s, fd_size)<0)
		rnunix_error("ftruncate");
	void *shm_ptr = nullptr;  
	if((shm_ptr = mmap(0, fd_size, PROT_READ|PROT_WRITE, MAP_SHARED, s, 0)) == MAP_FAILED/*void*(-1)*/) //
		rnunix_error("mmap");
	close(s); //
	if((listenfd = rnopen_listenfd(argv[1]))<0) //
		rnunix_error("rnopen_listenfd");
	setnonblocked(listenfd);
	listen_ptr = new(shm_ptr) rnlisten_block(listenfd); //placement new	
	accept_cnt = new(shm_ptr+sizeof(rnlisten_block)+8) unsigned long(0);
	int ep_fd0; //master epoll listen fd
	struct epoll_event ev;
	struct epoll_event *ev_ret_ptr = 
		(struct epoll_event*)malloc(MASTER_LISTEN_NUM*sizeof(struct epoll_event));
	if(NULL  == ev_ret_ptr)
	{
		fprintf(stderr, "malloc failure\n");
		exit(1);
	}
	if((ep_fd0 = epoll_create(MASTER_LISTEN_NUM)) < 0) //
		rnunix_error("epoll_create() in master");
	ev.data.fd = listenfd;
	ev.events = EPOLLIN|EPOLLET|EPOLLONESHOT; //listen fd was set as edge triggered
	if(epoll_ctl(ep_fd0, EPOLL_CTL_ADD,listenfd, &ev) < 0)
		rnunix_error("epoll_ctl in master");
	int read_fd_num = 0;
	for(int cnt = WORKER_NUM; cnt; --cnt) //fork new worker process
	{
		if(rnfork() == 0)
		{
//			queue<int> ready_que_rd;
//			queue<int> ready_que_wd;
			struct epoll_event ev1;
			struct epoll_event ready_ev[WORKER_LISTEN_NUM];
			int ready_fd_num1;
			int s,ep_fd1;
			int client_num = 0;
			char client_addr[SOCKADDR_LENGTH];
			socklen_t client_addrlen = SOCKADDR_LENGTH;
			char hostname[RNMAXLINE], port[RNMAXLINE];

			if((ep_fd1 = epoll_create(WORKER_LISTEN_NUM)) < 0) //
				rnunix_error("epoll_create() in worker");
			while(true)
			{
				if(0 == listen_ptr->try_wait())
				{
					if(listen_ptr->is_valid()&&listen_ptr->is_accept())
					{
						do{
							if((s = accept(listen_ptr->get_fd(),
											(SA*)client_addr, &client_addrlen)) < 0)
							{
								if(errno == EINTR||errno == ECONNABORTED) //
									continue;
								else if(errno == EAGAIN)
								{
									listen_ptr->set_accept_flag(false);
									ev.data.fd = listenfd;
									ev.events = EPOLLIN|EPOLLET|EPOLLONESHOT;
									if(epoll_ctl(ep_fd0, EPOLL_CTL_MOD,listenfd, &ev) < 0)
										rnunix_error("epoll_ctl in master");
									break;
								}else
									rnunix_error("accept");
							}else
							{
								setnonblocked(s);
								int tmp;
								if((tmp = getnameinfo((SA *)client_addr, client_addrlen,
												hostname, RNMAXLINE,
												port, RNMAXLINE, 
												NI_NUMERICHOST|NI_NUMERICSERV)) != 0)
								{
									fprintf(stderr, "%s", gai_strerror(tmp));
									exit(0);
								}

								//ev1.data.fd = s;
								myevents *pp = new myevents(s); //new operator, attention
								pp->setip(hostname);
								pp->setport(port);
								ev1.data.ptr = pp;
								ev1.events = EPOLLIN|EPOLLET;
								if(epoll_ctl(ep_fd1, EPOLL_CTL_ADD, s, &ev1) < 0)
									rnunix_error("epoll_ctl in worker");
								++client_num;
								fprintf(log_file_ptr, "%ld client %s(%s) accepted by %d\n", (*accept_cnt)++, hostname, port, getpid());
							}
						}while(client_num < WORKER_BALANCE_FACTOR*WORKER_LISTEN_NUM);
					}
					fflush(log_file_ptr);
					listen_ptr->post();
				}
				if(client_num)
				{
					if((ready_fd_num1 = epoll_wait(ep_fd1, ready_ev, 
									WORKER_LISTEN_NUM, -1)) < 0)
						rnunix_error("worker epoll_wait");
					for(int cnt = 0; cnt < ready_fd_num1; ++cnt)
					{
						//int fd = ready_ev[cnt].data.fd;
						//if(ready_ev[cnt].events&EPOLLIN)
						//	rndoit(fd);
						//close(fd);
						//--client_num;
						myevents *myev_ptr_tmp = (myevents*)ready_ev[cnt].data.ptr;
						int fd = myev_ptr_tmp->getfd();
						if(ready_ev[cnt].events&EPOLLIN) //read event
						{
							int s;
							if((s = myev_ptr_tmp->read()) == -1) //client close
							{
								close(fd);
								fprintf(log_file_ptr, "~client %s(%s) close\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport());
								delete myev_ptr_tmp; //release memory
								--client_num;
							}else if(s == 1) //receive all request lines
							{
								ev1.data.ptr = myev_ptr_tmp;
								ev1.events = EPOLLOUT|EPOLLET;
								if(epoll_ctl(ep_fd1, EPOLL_CTL_MOD,fd, &ev1) < 0)
									rnunix_error("epoll_ctl in worker");
								else
									continue;
							}
						}else if(ready_ev[cnt].events&EPOLLOUT)
						{
							if(myev_ptr_tmp->getpos() == 0)
							{
								close(fd);
								fprintf(log_file_ptr, "~client %s(%s) wrong request\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport());
								delete myev_ptr_tmp; //release memory
								--client_num;
							}else
							{
								bool is_static = true;
								struct stat stat_buf;
								char buf[RNMAXLINE],
								method[RNMAXLINE],
								uri[RNMAXLINE],
								version[RNMAXLINE];
								strcpy(buf, (*myev_ptr_tmp)[0].c_str());
								sscanf(buf, "%s %s %s", method, uri, version);
								if(!rnstrcasecmp(method, "GET"))
								{
									rnclienterror(fd, method, "501", "Not implemented",
											"Tiny does not implement this method");
									close(fd);
									fprintf(log_file_ptr, "~client %s(%s) unsupported method %s\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport(), method);
									delete myev_ptr_tmp; //release memory
									--client_num;
									continue;
								}
								char filename[RNMAXLINE], cgiargs[RNMAXLINE];
								is_static = rnparse_uri(uri, filename, cgiargs);
								if(stat(filename, &stat_buf)<0)
								{
									rnclienterror(fd, filename, "404", "Not found",
											"Tiny couldn't find this file");
									close(fd);
									fprintf(log_file_ptr, "~client %s(%s) file(%s) did't exist\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport(), filename);
									delete myev_ptr_tmp; //release memory
									--client_num;
									continue;
								}
								if(is_static)  //serve static content
								{
									if(!(S_ISREG(stat_buf.st_mode))||!(S_IRUSR & stat_buf.st_mode))
									{
										rnclienterror(fd, filename, "403", "Forbidden",
												"Tiny couldn't read the file");
										close(fd);
										fprintf(log_file_ptr, "~client %s(%s) file(%s) forbidden\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport(), filename);
										delete myev_ptr_tmp; //release memory
										--client_num;
										continue;
									}
									rnserve_static(fd, filename, stat_buf.st_size);
								}else //serve dynamic content
								{
									if(!(S_ISREG(stat_buf.st_mode))||!(S_IXUSR & stat_buf.st_mode))
									{
										rnclienterror(fd, filename, "403", "Forbidden",
												"Tiny couldn't run the CGI program");
										close(fd);
										fprintf(log_file_ptr, "~client %s(%s) CGI program(%s) forbidden\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport(), filename);
										delete myev_ptr_tmp; //release memory
										--client_num;
										continue;
									}
									rnserve_dynamic(fd, filename, cgiargs);
								}
							}
							close(fd);
							fprintf(log_file_ptr, "client %s(%s) disconnect.\n", myev_ptr_tmp->getip(), myev_ptr_tmp->getport());
							delete myev_ptr_tmp; //release memory
							--client_num;
						}
						fflush(log_file_ptr);
					}
				}
			}
			close(ep_fd1);
			exit(0);
		}
	}
	while(true)
	{
		if((read_fd_num = epoll_wait(ep_fd0, ev_ret_ptr, MASTER_LISTEN_NUM, -1)) < 0)
			rnunix_error("master epoll_wait");
		for(int cnt = 0; cnt < read_fd_num; ++cnt)
		{
			//int fd = ev_ret_ptr[cnt].data.fd;
			listen_ptr->wait(); //p()
			listen_ptr->set_valid(true);
			listen_ptr->set_accept_flag(true);
			listen_ptr->post(); //v()
			//printf("hello here is master\n");
		}
	}
	while(true)
	{
		if(waitpid(-1,0,0) < 0)
		{
			if(ECHILD == errno)
				break;
			else if(EINTR == errno)
				continue;
		}
	}
	listen_ptr->~rnlisten_block();
	munmap(shm_ptr, fd_size);
	shm_unlink(shm_name);
	close(listenfd);
	free(ev_ret_ptr);
	close(ep_fd0);
	fclose(log_file_ptr);
}



//int main(int argc, char *argv[])
//{
//	int listenfd, connfd;
//	if(argc != 2)
//	{
//		fprintf(stderr, "usage: %s <port>\n", argv[0]);
//		exit(1);
//	}
//	if((listenfd = rnopen_listenfd(argv[1]))<0)
//		rnunix_error("rnopen_listenfd");
//	//struct sockaddr_t clientaddr;
//	char clientaddr[RNSOCKADDRLEN];
//	socklen_t clientlen = sizeof(clientaddr);
//	char hostname[RNMAXLINE], port[RNMAXLINE];
//	while(true)
//	{
//		if((connfd = accept(listenfd, (SA *)clientaddr, &clientlen)) < 0)
//			rnunix_error("accept");
//		int tmp;
//		if((tmp = getnameinfo((SA *)clientaddr, clientlen,
//						hostname, RNMAXLINE,
//						port, RNMAXLINE, 
//						NI_NUMERICHOST|NI_NUMERICSERV)) != 0)
//		{
//			fprintf(stderr, "%s", gai_strerror(tmp));
//			exit(0);
//		}
//		//printf("Accepted connection from (%s, %s)\n", hostname, port);
//		//rndoit(connfd);
//		//close(connfd);
//	}
//}
