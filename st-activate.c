/* See LICENSE for license details. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void
die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char buf[4096];
	char *token = NULL, *start, *end;
	int ttyfd, restoretty = 0;
	ssize_t n;
	struct pollfd pfd;
	struct termios raw, saved;

	if (argc < 2)
		die("usage: %s <app> [args...]", argv[0]);

	if ((ttyfd = open("/dev/tty", O_RDWR | O_CLOEXEC)) >= 0) {
		if (tcgetattr(ttyfd, &saved) == 0) {
			raw = saved;
			raw.c_lflag &= ~(ICANON | ECHO);
			raw.c_cc[VMIN] = 0;
			raw.c_cc[VTIME] = 0;
			if (tcsetattr(ttyfd, TCSANOW, &raw) == 0)
				restoretty = 1;
		}

		dprintf(ttyfd, "\033]703;%s\007", argv[1]);

		pfd.fd = ttyfd;
		pfd.events = POLLIN;

		if (poll(&pfd, 1, 1000) > 0) {
			n = read(ttyfd, buf, sizeof(buf) - 1);
			if (n > 0) {
				buf[n] = '\0';
				start = strstr(buf, "\033]703;");
				if (start) {
					start += 6;
					end = strchr(start, '\007');
					if (end && end > start) {
						*end = '\0';
						token = start;
					}
				}
			}
		}
		if (restoretty)
			tcsetattr(ttyfd, TCSANOW, &saved);
		close(ttyfd);
	}

	if (token)
		setenv("XDG_ACTIVATION_TOKEN", token, 1);
	else
		fprintf(stderr, "st-activate: no activation token received\n");

	execvp(argv[1], &argv[1]);
	die("st-activate: execvp %s: %s", argv[1], strerror(errno));
}
