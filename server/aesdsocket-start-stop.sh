#!/bin/sh

case $1 in
	start)
		echo "Starting aesdsocket -d"
		start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocketi -- -d
		;;
	stop)
		echo "Stopping aesdsocket -d"
		start-stop-daemon -K -n aesdsocket --signal TERM
		;;
	*)
		echo "Usage: {start|stop}"
	exit 1
esac

exit 0

