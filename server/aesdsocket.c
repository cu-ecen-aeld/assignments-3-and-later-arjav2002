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

bool caughtsignal = false;

static void handle_signal(int signal_number)
{
	if(signal_number == SIGINT || signal_number == SIGTERM)
	{
		caughtsignal = true;
	}
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
	
	off_t roff;

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


	if(argc > 1 && strcmp(argv[1], "-d") == 0)
        {
                pid_t p = fork();
                if(p == -1)
                {
                        perror("fork()");
                        return -1;
                }
                if(p) return 0;
		printf("Running in child\n");
        }


        openlog(NULL, LOG_CONS | LOG_PERROR, LOG_SYSLOG);


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
		
		uint32_t clientaddr = htonl(((struct sockaddr_in*)clientsock)->sin_addr.s_addr);
		syslog(LOG_INFO, "Accepted connection from %d.%d.%d.%d", clientaddr&0xFF000000 >> 24, clientaddr&0x00FF0000 >> 16, clientaddr&0x0000FF00 >> 8, clientaddr&0x000000FF);

		int opfd = rc = open("/var/tmp/aesdsocketdata", O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
		if(rc < 0)
		{
			perror("open()");
			if(close(cfd))
			{
				perror("close()");
			}
			free(clientsock);
			return -1;
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
				break;
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
					break;
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
			break;
		}

		roff = lseek(opfd, 0, SEEK_SET);
		if(roff == -1)
		{
			perror("lseek()");
			free(clientsock);
			close(cfd);
			close(opfd);
			break;
		}

		while(readbytes = rc = read(opfd, buf, buflen*sizeof(char)))
		{
			if(rc == -1)
			{
				perror("read()");
				free(clientsock);
				close(cfd);
				close(opfd);
				break;
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
					break;
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
			break;
		}
		rc = close(opfd);
		if(rc)
		{
			perror("close()");
			break;
		}
                syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", clientaddr&0xF000, clientaddr&0x0F00, clientaddr&0x00F0, clientaddr&0x000F);

	}

	freeaddrinfo(my_addrinfo);
	close(sfd);

	remove("/var/tmp/aesdsocketdata");
	syslog(LOG_INFO, "Caught signal, exiting");
	return 0;
}
