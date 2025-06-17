#!/bin/sh

case $1 in
	start)
		echo "Starting aesdsocket -d"
		start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket
		;;
	stop)
		echo "Stopping aesdsocket -d"
		start-stop-daemon -K -n aesdsocket
		;;
	*)
		echo "Usage: {start|stop}"
	exit 1
esac

exit 0

