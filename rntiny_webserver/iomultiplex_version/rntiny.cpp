//
#include"rntools.h"
#include"rntiny_imp.h"
#define WORKER_NUM 10
#define MASTER_LISTEN_NUM 1 //only 1
#define WORKER_LISTEN_NUM 100
#define WORKER_BALANCE_FACTOR (7/10)
#define SOCKADDR_LENGTH 30
#define LOG_PATH "tinyweb.log"

class rnlisten_block
{
public:
	rnlisten_block(int rnfd):valid(false),accept_flag(false),fd(rnfd)
	{
		if(sem_init(&sem, 1, 1) < 0) // used in multiprocess
			rnunix_error("sem_init");
	}
	~rnlisten_block()
	{
		sem_destroy(&sem);
	}
	bool is_valid(){return valid;}
	bool is_accept(){return accept_flag;}
	int get_fd(){return fd;};

	void set_valid(bool sign){valid = sign;}
	void set_accept_flag(bool sign){accept_flag = sign;}
	int wait()
	{
		while(true)
			if(sem_wait(&sem) < 0)
			{
				if(errno == EINTR)
					continue;
				else
					rnunix_error("sem_wait");
			}else
				return 0;
	}
	int try_wait()
	{
		if(sem_trywait(&sem) < 0)
		{
			if(errno == EAGAIN)
				return -1;
			else
				rnunix_error("sem_trywait");
		}
		return 0;
	}
	int post()
	{
		if(sem_post(&sem) < 0)
			rnunix_error("sem_post");
		return 0;
	}
private:
	bool valid;
	bool accept_flag;
	int fd;
	sem_t sem;
};
void setnonblocked(int fd)
{
	int fdfl;
	if((fdfl = fcntl(fd, F_GETFL, 0)) == -1)
		rnunix_error("fcntl, F_GETFL");
	fdfl |= O_NONBLOCK;
	if(fcntl(fd, F_SETFL, fdfl) < 0)
		rnunix_error("fcntl, F_SETFL");
}
rnlisten_block *listen_ptr = NULL;
unsigned long accept_cnt = 0;
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
	int fd_size = (MASTER_LISTEN_NUM*sizeof(rnlisten_block))/getpagesize() + 1;
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
			int read_fd_num1;
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
								ev1.data.fd = s;
								ev1.events = EPOLLIN|EPOLLET;
								if(epoll_ctl(ep_fd1, EPOLL_CTL_ADD, s, &ev1) < 0)
									rnunix_error("epoll_ctl in worker");
								++client_num;
								int tmp;
								if((tmp = getnameinfo((SA *)client_addr, client_addrlen,
												hostname, RNMAXLINE,
												port, RNMAXLINE, 
												NI_NUMERICHOST|NI_NUMERICSERV)) != 0)
								{
									fprintf(stderr, "%s", gai_strerror(tmp));
									exit(0);
								}
								fprintf(log_file_ptr, "%ld client %s(%s) connected\n", accept_cnt++, hostname, port);
							}
						}while(client_num < WORKER_BALANCE_FACTOR*WORKER_LISTEN_NUM);
					}
					fflush(log_file_ptr);
					listen_ptr->post();
				}
				if(client_num)
				{
					if((read_fd_num1 = epoll_wait(ep_fd1, ready_ev, 
									WORKER_LISTEN_NUM, -1)) < 0)
						rnunix_error("worker epoll_wait");
					for(int cnt = 0; cnt < read_fd_num1; ++cnt)
					{
						int fd = ready_ev[cnt].data.fd;
						if(ready_ev[cnt].events&EPOLLIN)
							rndoit(fd);
						close(fd);
						--client_num;
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
			printf("hello here is master\n");
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
