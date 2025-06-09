#include <syslog.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
	openlog(NULL, 0, LOG_USER);
	const char* filepath = argv[1];
	const char* content = argv[2];
	syslog(LOG_DEBUG, "Writing %s to %s", content, filepath);
	int fd = open(filepath, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR, S_IWUSR);
	if(fd == -1)
	{
		syslog(LOG_ERR, "%d", errno);
		return 1;
	}

	write(fd, content, strlen(content));	
	return 0;
}
