#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h> /* For ctime and time_t */
#include <signal.h> /* For signal handling */
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#define LISTEN_PORT 12345
#define MAX_SITES 10
#define MAX_SITE_ID_LEN 64
#define LOG_FILE_PATH "/var/log/monitor_heartbeat.log" /* Standard log location */

static struct {
	char site_id[MAX_SITE_ID_LEN];
	struct timeval last_heartbeat_time;
} monitored_sites[MAX_SITES];
static int num_monitored_sites = 0;

static int lookup_site(char *site_id)
{
	for (int i = 0; i < num_monitored_sites; i++)
		if (strcmp(site_id, monitored_sites[i].site_id) == 0)
			return i;
	return -1;
}

static int time_to_exit = 0;
static FILE *log_fp = NULL;

/* Function to log a message to file */
static void log_message(const char *format, ...)
{
	va_list args;

	if (!log_fp)
		return;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t current_time = tv.tv_sec;
	char time_str[64];
	struct tm *tm_info;

	tm_info = localtime(&current_time);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

	fprintf(log_fp, "[%s.%06ld] ", time_str, (long)tv.tv_usec);

	va_start(args, format);
	vfprintf(log_fp, format, args);
	va_end(args);
	fprintf(log_fp, "\n");
	fflush(log_fp); /* Ensure log is written immediately */
}

static void usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS] <site_id1> [site_id2 ...]\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -P, --port <port_number> Listen on a specific UDP port (default: %d)\n", LISTEN_PORT);
	fprintf(stderr, "  -L, --log-file <path>	Specify a custom log file (default: %s)\n", LOG_FILE_PATH);
	fprintf(stderr, "Example: %s home remote-site1 remote-site2\n", prog_name);
	exit(EXIT_FAILURE);
}

static void cleanup_and_exit(int exit_code)
{
	if (log_fp)
		fclose(log_fp);
	exit(exit_code);
}

int main(int argc, char *argv[])
{
	int sockfd;
	struct sockaddr_in servaddr, cliaddr;
	char buffer[1024];
	socklen_t len;
	int opt;
	int listen_port = LISTEN_PORT;
	char *custom_log_file = NULL;

	signal(SIGHUP, SIG_IGN); /* ignore hangup signal so it will persist across user logging out */

	static struct option long_options[] = {
		{"port",	required_argument, 0, 'P'},
		{"log-file", required_argument, 0, 'L'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "P:L:", long_options, NULL)) != -1) {
		switch (opt) {
			case 'P':
				listen_port = atoi(optarg);
				if (listen_port <= 1024 || listen_port > 65535) {
					fprintf(stderr, "Error: Port number must be between 1025 and 65535.\n");
					usage(argv[0]);
				}
				break;
			case 'L':
				custom_log_file = optarg;
				break;
			case '?':
				usage(argv[0]);
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Error: No site IDs specified to monitor.\n");
		usage(argv[0]);
	}

	/* Open log file (either default or custom) */
	const char *final_log_path = custom_log_file ? custom_log_file : LOG_FILE_PATH;
	log_fp = fopen(final_log_path, "a");
	if (log_fp == NULL) {
		perror("Failed to open log file");
		exit(EXIT_FAILURE);
	}
	log_message("Starting monitor on port %d, logging to %s", listen_port, final_log_path);


	/* Initialize monitored sites from command line arguments */
	for (int i = optind; i < argc; ++i) {
		if (num_monitored_sites < MAX_SITES) {
			strncpy(monitored_sites[num_monitored_sites].site_id, argv[i], MAX_SITE_ID_LEN - 1);
			monitored_sites[num_monitored_sites].site_id[MAX_SITE_ID_LEN - 1] = '\0';
			monitored_sites[num_monitored_sites].last_heartbeat_time.tv_sec = 0;
			monitored_sites[num_monitored_sites].last_heartbeat_time.tv_usec = 0;
			log_message("Monitoring site: %s", monitored_sites[num_monitored_sites].site_id);
			num_monitored_sites++;
		} else {
			log_message("Too many initial sites specified. Max is %d. Ignoring: %s", MAX_SITES, argv[i]);
			break;
		}
	}

	/* Creating socket file descriptor */
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket creation failed");
		log_message("Socket creation failed.");
		cleanup_and_exit(1);
	}

	memset(&cliaddr, 0, sizeof(cliaddr));
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET; /* IPv4 */
	servaddr.sin_addr.s_addr = INADDR_ANY; /* Listen on all available interfaces */
	servaddr.sin_port = htons(listen_port);

	/* Bind the socket with the server address */
	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("bind failed");
		log_message("Socket bind failed on port %d.", listen_port);
		close(sockfd);
		cleanup_and_exit(1);
	}

	log_message("Monitor listening for heartbeats on UDP port %d...", listen_port);

	while (1) {
		len = sizeof(cliaddr);
		errno = 0;
		int n = recvfrom(sockfd, (char *)buffer, sizeof(buffer) - 1,
					MSG_WAITALL, (struct sockaddr *)&cliaddr, &len);
		if (n < 0) {
			fprintf(stderr, "n = %d, errno = %d\n", n, errno);
			if (errno == EINTR) {
				log_message("recvfrom interrupted by signal, time_to_exit = %d\n", time_to_exit);
				if (time_to_exit)
					goto cleanup_and_exit;
				continue;
			}
			perror("recvfrom failed");
			log_message("recvfrom failed: %s.", strerror(errno));
			goto cleanup_and_exit;
		}
		buffer[n] = '\0'; /* Null-terminate the received data */

		char *site_id_start = strstr(buffer, "HEARTBEAT_SITE:");
		char *time_start = strstr(buffer, "_TIME:");

		if (!site_id_start || !time_start) /* Bad data */
			continue;

		/* Extract site ID */
		site_id_start += strlen("HEARTBEAT_SITE:");
		char *site_id_end = strchr(site_id_start, '_');
		if (!site_id_end) /* Bad data */
			continue;
		*site_id_end = '\0'; /* Temporarily null-terminate for strtok */
		char site_id_parsed[MAX_SITE_ID_LEN];
		strncpy(site_id_parsed, site_id_start, sizeof(site_id_parsed) - 1);
		site_id_parsed[sizeof(site_id_parsed) - 1] = '\0';

		/* Extract timestamp */
		time_start += strlen("_TIME:");
		long sec = 0, usec = 0;
		if (sscanf(time_start, "%ld.%06ld", &sec, &usec) != 2) /* Bad data */
			continue;
		struct timeval received_tv = {.tv_sec = sec, .tv_usec = usec};

		int site_index = lookup_site(site_id_parsed);
		if (site_index != -1) {
			monitored_sites[site_index].last_heartbeat_time = received_tv;
			log_message("Heartbeat received from site: %s (IP: %s, Port: %d) at %ld.%06ld",
					site_id_parsed,
					inet_ntoa(cliaddr.sin_addr),
					ntohs(cliaddr.sin_port),
					(long)received_tv.tv_sec, (long)received_tv.tv_usec);
		} else {
			/* This is a heartbeat from a site not explicitly configured. */
			/* We can choose to log it and potentially add it, or just log and ignore. */
			/* For this example, we'll log it and NOT add it to the monitored_sites array */
			/* as the user explicitly provided a list of sites to monitor. */
			log_message("Heartbeat received from unknown site '%s' (IP: %s, Port: %d) at %ld.%06ld. Not configured for monitoring.",
					site_id_parsed,
					inet_ntoa(cliaddr.sin_addr),
					ntohs(cliaddr.sin_port),
					(long)received_tv.tv_sec, (long)received_tv.tv_usec);
		}
	}

cleanup_and_exit:
	close(sockfd);
	cleanup_and_exit(0);
	return 0;
}
