#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>

#define DEFAULT_PORT 12345
#define DEFAULT_PERIOD_SECONDS 10 // Default to 10 seconds

static void usage(const char *prog_name) {
	fprintf(stderr, "Usage: %s [OPTIONS] <site_id> <monitor_ip_address>\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -p, --period <seconds>   Set the transmission period in seconds (default: %d)\n", DEFAULT_PERIOD_SECONDS);
	fprintf(stderr, "  -P, --port <port_number> Set the UDP port to send to (default: %d)\n", DEFAULT_PORT);
	fprintf(stderr, "Example: %s -p 60 home 192.168.1.100\n", prog_name);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	int sockfd;
	struct sockaddr_in monitor_addr;
	char *site_id = NULL;
	char *monitor_ip = NULL;
	int period_seconds = DEFAULT_PERIOD_SECONDS;
	int port = DEFAULT_PORT;
	int opt;

	static struct option long_options[] = {
		{"period", required_argument, 0, 'p'},
		{"port",   required_argument, 0, 'P'},
		{0, 0, 0, 0}
	};

	signal(SIGHUP, SIG_IGN); /* Ignore SIGHUP so it will persist across user logout */

	while ((opt = getopt_long(argc, argv, "p:P:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'p':
			period_seconds = atoi(optarg);
			if (period_seconds <= 0) {
				fprintf(stderr, "Error: Period must be a positive integer.\n");
				usage(argv[0]);
			}
			break;
		case 'P':
			port = atoi(optarg);
			if (port <= 1024 || port > 65535) {
				fprintf(stderr, "Error: Port number must be between 1025 and 65535.\n");
				usage(argv[0]);
			}
			break;
		default:
			usage(argv[0]);
		}
	}

	if (optind + 2 != argc) {
		fprintf(stderr, "Error: Missing site_id and monitor_ip_address.\n");
		usage(argv[0]);
	}

	site_id = argv[optind];
	monitor_ip = argv[optind + 1];

	// Create UDP socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&monitor_addr, 0, sizeof(monitor_addr));

	// Filling monitor information
	monitor_addr.sin_family = AF_INET;
	monitor_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, monitor_ip, &monitor_addr.sin_addr) <= 0) {
		perror("invalid monitor IP address/address not supported");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	printf("Sending heartbeat from site '%s' to %s:%d every %d seconds...\n",
		   site_id, monitor_ip, port, period_seconds);

	while (1) {
		char buffer[256];
		struct timeval tv;
		gettimeofday(&tv, NULL);
		snprintf(buffer, sizeof(buffer), "HEARTBEAT_SITE:%s_TIME:%ld.%06ld",
				 site_id, tv.tv_sec, tv.tv_usec);

		int rc = sendto(sockfd, buffer, strlen(buffer), 0,
				(const struct sockaddr *)&monitor_addr, sizeof(monitor_addr));
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			perror("sendto failed");
		}
		sleep(period_seconds);
	}

	close(sockfd);
	return 0;
}
