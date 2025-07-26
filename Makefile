all:	send_heartbeat monitor_heartbeat

CFLAGS=-Wall -Wextra -fsanitize=address

send_heartbeat:	send_heartbeat.c Makefile
	$(CC) ${CFLAGS} -o send_heartbeat send_heartbeat.c

monitor_heartbeat:	monitor_heartbeat.c Makefile
	$(CC) ${CFLAGS} -o monitor_heartbeat monitor_heartbeat.c

clean:
	rm -f send_heartbeat monitor_heartbeat

