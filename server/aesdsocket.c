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

int main()
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

        openlog(NULL, LOG_CONS | LOG_PERROR, LOG_SYSLOG);


	while(!caughtsignal) {
		rc = listen(sfd, 1);
		if(rc)
		{
			perror("listen()");
			freeaddrinfo(my_addrinfo);
			return -1;
		}

		socklen_t clientsocksize = sizeof(struct sockaddr_in);
		struct sockaddr* clientsock = malloc(clientsocksize);
		if(!clientsock)
		{
			perror("malloc()");
			freeaddrinfo(my_addrinfo);
			return -1;
		}
		int cfd = rc = accept(sfd, clientsock, &clientsocksize);
		if(rc == -1)
		{
			perror("accept()");
			freeaddrinfo(my_addrinfo);
			free(clientsock);
			return -1;
		}

		assert(clientsocksize == sizeof(struct sockaddr_in));
		
		uint32_t clientaddr = ((struct sockaddr_in*)clientsock)->sin_addr.s_addr;
		syslog(LOG_INFO, "Accepted connection from %d.%d.%d.%d", clientaddr&0xF000, clientaddr&0x0F00, clientaddr&0x00F0, clientaddr&0x000F);

		int opfd = rc = open("/var/tmp/aesdsocketdata", O_CREAT, O_RDWR, O_APPEND);
		if(rc < 0)
		{
			perror("open()");
			freeaddrinfo(my_addrinfo);
			if(close(cfd))
			{
				perror("close()");
				return -1;
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
				freeaddrinfo(my_addrinfo);
				free(clientsock);
				close(cfd);
				close(opfd);
				return -1;
			}

			writtenbytes = 0;
			while(readbytes > 0 && (rc = write(opfd, buf+writtenbytes, readbytes)))
			{
				if(rc == -1)
				{
	                                perror("write()");
                                	freeaddrinfo(my_addrinfo);
                        	        free(clientsock);
                	                close(cfd);
        	                        close(opfd);
	                                return -1;
				}

				writtenbytes += rc;
				readbytes -= rc;
			}
		}

		char nline = '\n';
                if(write(opfd, &nline, sizeof(char)) == -1)
                {
                	perror("write()");
                        freeaddrinfo(my_addrinfo);
                        free(clientsock);
                        close(cfd);
                        close(opfd);
                        return -1;
                }


		while(readbytes = rc = read(opfd, buf, buflen*sizeof(char)))
		{
			if(rc == -1)
			{
				perror("read()");
				freeaddrinfo(my_addrinfo);
				free(clientsock);
				close(cfd);
				close(opfd);
				return -1;
			}

			writtenbytes = 0;
			while(readbytes > 0)
			{
				rc = send(sfd, buf+writtenbytes, readbytes, 0);
				if(rc == -1)
				{
					perror("send()");
					freeaddrinfo(my_addrinfo);
					free(clientsock);
					close(cfd);
					close(opfd);
					return -1;
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
			return -1;
		}
		rc = close(opfd);
		if(rc)
		{
			perror("close()");
			return -1;
		}
                syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", clientaddr&0xF000, clientaddr&0x0F00, clientaddr&0x00F0, clientaddr&0x000F);

	}

	freeaddrinfo(my_addrinfo);
	close(sfd);

	remove("/var/tmp/aesdsocketdata");
	syslog(LOG_INFO, "Caught signal, exiting");
	return 0;
}
