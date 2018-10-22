#include"rntools.h"
#ifndef RNTINY_IMP_H
#define RNTINY_IMP_H
//#define RNSOCKADDRLEN 20
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
class rnlisten_block
{
public:
	rnlisten_block(int rnfd):valid(false),accept_flag(false),fd(rnfd)
	{
		if(sem_init(&sem, 1, 1) < 0) // used in multiprocess
			rnunix_error("sem_init");
		if(sem_init(&sem_src, 1, 0) < 0) // used in multiprocess
			rnunix_error("sem_init");

	}
	~rnlisten_block()
	{
		sem_destroy(&sem);
		sem_destroy(&sem_src);
	}
	bool is_valid(){return valid;}
	bool is_accept(){return accept_flag;}
	int get_fd(){return fd;};

	void set_valid(bool sign){valid = sign;}
	void set_accept_flag(bool sign){accept_flag = sign;}
	int wait_src()
	{return wait_imp(&sem_src);}
	int wait()
	{return wait_imp(&sem);}
	int wait_imp(sem_t* sem0)
	{
		while(true)
			if(sem_wait(sem0) < 0)
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
	int post(){return post_imp(&sem);}
	int post_src(){return post_imp(&sem_src);}
	int post_imp(sem_t *sem0)
	{
		if(sem_post(sem0) < 0)
			rnunix_error("sem_post");
		return 0;
	}
private:
	bool valid;
	bool accept_flag;
	int fd;
	sem_t sem;
	sem_t sem_src;
};
#endif
