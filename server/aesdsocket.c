#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <assert.h>
#include <syslog.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

static bool caughtsignal = false;


static void handle_signal(int signal_number)
{
	if(signal_number == SIGINT || signal_number == SIGTERM)
	{
		caughtsignal = true;
	}
}

typedef struct data
{
	struct sockaddr* clientsock;
	int cfd;
} data_t;

typedef struct node
{
	pthread_t thread;
	struct node* next;
} node_t;

static pthread_mutex_t file_mutex;

void* process_request(void* _req_data)
{
	data_t* req_data = (data_t*) _req_data;
	struct sockaddr* clientsock = req_data->clientsock;
	int cfd = req_data->cfd;
	uint32_t clientaddr = htonl(((struct sockaddr_in*)clientsock)->sin_addr.s_addr);
	syslog(LOG_INFO, "Accepted connection from %d.%d.%d.%d", (clientaddr&0xFF000000) >> 24, (clientaddr&0xFF0000) >> 16, (clientaddr&0xFF00) >> 8, (clientaddr&0xFF));

	int rc;
	int opfd = rc = open("/var/tmp/aesdsocketdata", O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
	if(rc < 0)
	{
		perror("open()");
		if(close(cfd))
		{
			perror("close()");
		}
		free(clientsock);
		free(req_data);
		return NULL;
	}


	rc = pthread_mutex_lock(&file_mutex);
	if(rc)
	{
		perror("pthread_mutex_lock()");
		free(clientsock);
		close(cfd);
		close(opfd);
		free(req_data);
		return NULL;
	}

	size_t buflen = 512;
	char buf[512];
	ssize_t readbytes, writtenbytes;
	while(readbytes = rc = recv(cfd, buf, buflen*sizeof(char), 0))
	{
		if(rc == -1)
		{
			perror("recv()");
			free(clientsock);
			close(cfd);
			close(opfd);
			free(req_data);
			return NULL;
		}

		bool isfinal = false;
		int i;
		for(i = 0; i < readbytes; i++)
		{
			if(buf[i] == '\n')
			{
				isfinal = true;
				readbytes = i;
				break;
			}
		}

		writtenbytes = 0;
		while(readbytes > 0 && (rc = write(opfd, buf+writtenbytes, readbytes)))
		{
			if(rc == -1)
			{
				perror("write()");
				free(clientsock);
				close(cfd);
				close(opfd);
				free(req_data);
				return NULL;
			}

			writtenbytes += rc;
			readbytes -= rc;
		}

		if(isfinal) break;
	}

	char nl = '\n';
	rc = write(opfd, &nl, sizeof(char));
	if(rc == -1)
	{
		perror("write()");
		free(clientsock);
		close(cfd);
		close(opfd);
		free(req_data);
		return NULL;
	}
	rc = pthread_mutex_unlock(&file_mutex);
	if(rc)
	{
		perror("pthread_mutex_unlock()");
		free(clientsock);
		close(cfd);
		close(opfd);
		free(req_data);
		return NULL;
	}


	off_t roff;
	roff = lseek(opfd, 0, SEEK_SET);
	if(roff == -1)
	{
		perror("lseek()");
		free(clientsock);
		close(cfd);
		close(opfd);
		free(req_data);
		return NULL;
	}

	while(readbytes = rc = read(opfd, buf, buflen*sizeof(char)))
	{
		if(rc == -1)
		{
			perror("read()");
			free(clientsock);
			close(cfd);
			close(opfd);
			free(req_data);
			return NULL;
		}

		writtenbytes = 0;
		while(readbytes > 0 && (rc = send(cfd, buf+writtenbytes, readbytes, 0)))
		{
			if(rc == -1)
			{
				perror("send()");
				free(clientsock);
				close(cfd);
				close(opfd);
				free(req_data);
				return NULL;
			}

			writtenbytes += rc;
			readbytes -= rc;
		}
	}

	free(clientsock);
	rc = close(cfd);
	if(rc)
	{
		perror("close()");
		free(req_data);
		return NULL;
	}
	rc = close(opfd);
	if(rc)
	{
		perror("close()");
		free(req_data);
		return NULL;
	}
	free(req_data);
	syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", clientaddr&0xF000, clientaddr&0x0F00, clientaddr&0x00F0, clientaddr&0x000F);

	return NULL;
}

int start_new_request(node_t** head, node_t** tail, data_t* req_data)
{
	if(*tail == NULL)
	{
		*head = *tail = malloc(sizeof(node_t));
		if(*tail == NULL)
		{
			perror("malloc()");
			return -1;
		}
	}
	else
	{
		(*tail)->next = malloc(sizeof(node_t));
		if((*tail)->next == NULL)
		{
			perror("malloc()");
			return -1;
		}
		*tail = (*tail)->next;
	}

	int rc;

	pthread_t thread;
	rc = pthread_create(&thread, NULL, process_request, req_data);
	if(rc)
	{
		perror("pthread_create()");
		free(*tail);
		return -1;
	}

	(*tail)->thread = thread;
	(*tail)->next = NULL;

	return 0;
}

static void timer_thread(union sigval sigval)
{
	if(pthread_mutex_lock(&file_mutex))
	{
		perror("pthread_mutex_lock()");
		return;
	}

	int rc;
        int fd = rc = open("/var/tmp/aesdsocketdata", O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
        if(rc < 0)
        {
                perror("open()");
                if(close(fd))
                {
                        perror("close()");
                }
		pthread_mutex_unlock(&file_mutex);
                return;
        }

	struct timespec wallclocktime;
	rc = clock_gettime(CLOCK_REALTIME, &wallclocktime);
	if(rc)
	{
		perror("clock_gettime()");
                if(close(fd))
                {
                        perror("close()");
                }
                pthread_mutex_unlock(&file_mutex);
                return;
	}

	struct tm *mlocaltime = localtime(&wallclocktime.tv_sec);
	char buf[64];
	size_t writtenchars = strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z", mlocaltime); 
	if(writtenchars == 0)
	{
		perror("strftime()");
                if(close(fd))
                {
                        perror("close()");
                }
		;
                pthread_mutex_unlock(&file_mutex);
		return;
	}
	rc = dprintf(fd, "timestamp:%s\n", buf);
	if(rc<0)
	{
		perror("dprintf()");
                if(close(fd))
		{
                        perror("close()");
                }
                ;
                pthread_mutex_unlock(&file_mutex);
		return;
	}

	if(pthread_mutex_unlock(&file_mutex))
	{
		perror("pthread_mutex_unlock()");
		close(fd);
		;
		return;
	}
	close(fd);
	;
}

int main(int argc, char* argv[])
{
	struct sigaction sigtermint_act;
	memset(&sigtermint_act, 0, sizeof(struct sigaction));
	sigtermint_act.sa_handler = handle_signal;

	int rc;
	rc = sigaction(SIGINT, &sigtermint_act, NULL);
	if(rc)
	{
		perror("sigaction()");
		return -1;
	}
	if(sigaction(SIGTERM, &sigtermint_act, NULL))
	{
		perror("sigaction()");
		return -1;
	}

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd == -1)
	{
		perror("socket()");
		return -1;
	}
	const int enable = 1;
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		perror("setsockopt(SO_REUSEADDR) failed");
		close(sfd);
		return -1;
	}
	

	struct addrinfo *my_addrinfo=NULL;
	struct addrinfo hints;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_protocol = 0;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(NULL, "9000", &hints, &my_addrinfo);	
	if(rc)
	{
		perror("getaddrinfo()");
	        freeaddrinfo(my_addrinfo);
		return -1;
	}


	rc = bind(sfd, my_addrinfo->ai_addr, my_addrinfo->ai_addrlen);
	if(rc)
	{
		perror("bind()");
       		freeaddrinfo(my_addrinfo);
		return -1;
	}


        openlog(NULL, 0, LOG_SYSLOG);

	if(argc > 1 && strcmp(argv[1], "-d") == 0)
        {
                pid_t p = fork();
                if(p == -1)
                {
                        perror("fork()");
                        return -1;
                }
                if(p) {
			return 0;
		}
        }

	int clock_id = CLOCK_MONOTONIC;
	timer_t tid;
	struct sigevent sev;
	memset(&sev, 0, sizeof(struct sigevent));
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = timer_thread;
	if(timer_create(clock_id, &sev, &tid))
	{
		perror("timer_create()");
		return -1;
	}
	struct itimerspec nv;
	nv.it_value.tv_sec = 1;
	nv.it_value.tv_nsec = 0;
	nv.it_interval.tv_sec = 10;
	nv.it_interval.tv_nsec = 0;
	if(timer_settime(tid, 0, &nv, NULL))
	{
		perror("timer_settime()");
		return -1;
	}

	node_t *head, *tail;

	while(!caughtsignal) {
		rc = listen(sfd, 1);
		if(rc)
		{
			perror("listen()");
			break;
		}

		socklen_t clientsocksize = sizeof(struct sockaddr_in);
		struct sockaddr* clientsock = malloc(clientsocksize);
		if(!clientsock)
		{
			perror("malloc()");
			break;
		}
		int cfd = rc = accept(sfd, clientsock, &clientsocksize);
		if(rc == -1)
		{
			perror("accept()");
			free(clientsock);
			break;
		}

		assert(clientsocksize == sizeof(struct sockaddr_in));
		
		data_t *req_data = malloc(sizeof(data_t));
		if(req_data == NULL)
		{
			perror("malloc()");
			free(clientsock);
			close(cfd);
			continue;
		}

		req_data->clientsock = clientsock;
		req_data->cfd = cfd;

		rc = start_new_request(&head, &tail, req_data);
		if(rc)
		{
			free(req_data);
			free(clientsock);
			close(cfd);
		}
	}

	while(head)
	{
		pthread_join(head->thread, NULL);
		node_t *prev = head;
		head = head->next;
		free(prev);
	}

	freeaddrinfo(my_addrinfo);
	close(sfd);
	timer_delete(tid);

	remove("/var/tmp/aesdsocketdata");
	syslog(LOG_INFO, "Caught signal, exiting");
	return 0;
}
