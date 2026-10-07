#define _POSIX_C_SOURCE 200809L

/*
 * File2TCP - FIFO to TCP transport bridge
 *
 * Original prototype: 2024-06-05
 * Public hardening pass: 2026-10-08
 *
 * The original 2024 prototype remains available in the Git history.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_FIFO_PATH "/tmp/mysocket"
#define BUFFER_SIZE 65536
#define INITIAL_RETRY_DELAY 1
#define MAX_RETRY_DELAY 16

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void usage(const char *program) {
    fprintf(stderr,
            "Usage: %s REMOTE_HOST REMOTE_TCP_PORT [FIFO_PATH]\n"
            "Example: %s 127.0.0.1 9999 /tmp/mysocket\n",
            program, program);
}

static int parse_port(const char *text, uint16_t *port) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 65535) {
        return -1;
    }

    *port = (uint16_t)value;
    return 0;
}

static int ensure_fifo(const char *path) {
    struct stat st;

    if (lstat(path, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            fprintf(stderr, "Refusing to replace non-FIFO path: %s\n", path);
            return -1;
        }
        return 0;
    }

    if (errno != ENOENT) {
        perror("lstat");
        return -1;
    }

    if (mkfifo(path, 0666) == 0) {
        return 0;
    }

    if (errno == EEXIST && lstat(path, &st) == 0 && S_ISFIFO(st.st_mode)) {
        return 0;
    }

    perror("mkfifo");
    return -1;
}

static int connect_once(const char *host, uint16_t port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char port_text[6];
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    rc = getaddrinfo(host, port_text, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

static int connect_with_backoff(const char *host, uint16_t port) {
    unsigned delay = INITIAL_RETRY_DELAY;

    while (!stop_requested) {
        int fd = connect_once(host, port);
        if (fd >= 0) {
            fprintf(stderr, "Connected to %s:%u\n", host, (unsigned)port);
            return fd;
        }

        fprintf(stderr,
                "Unable to connect to %s:%u; retrying in %u second(s)...\n",
                host, (unsigned)port, delay);

        for (unsigned i = 0; i < delay && !stop_requested; ++i) {
            sleep(1);
        }

        if (delay < MAX_RETRY_DELAY) {
            delay *= 2;
            if (delay > MAX_RETRY_DELAY) {
                delay = MAX_RETRY_DELAY;
            }
        }
    }

    return -1;
}

static int send_all(int fd, const unsigned char *buffer, size_t length) {
    size_t offset = 0;

    while (offset < length && !stop_requested) {
        ssize_t sent = send(fd,
                            buffer + offset,
                            length - offset,
#ifdef MSG_NOSIGNAL
                            MSG_NOSIGNAL
#else
                            0
#endif
        );

        if (sent > 0) {
            offset += (size_t)sent;
            continue;
        }

        if (sent < 0 && errno == EINTR) {
            continue;
        }

        if (sent == 0) {
            errno = ECONNRESET;
        }
        return -1;
    }

    return stop_requested ? -1 : 0;
}

static int open_fifo_reader(const char *path) {
    int fd;

    while (!stop_requested) {
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            return fd;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("open FIFO");
        sleep(1);
    }

    return -1;
}

int main(int argc, char **argv) {
    const char *remote_host;
    const char *fifo_path;
    uint16_t remote_port;
    int tcp_fd = -1;
    int fifo_fd = -1;
    unsigned char buffer[BUFFER_SIZE];
    int exit_code = EXIT_FAILURE;

    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    remote_host = argv[1];
    if (parse_port(argv[2], &remote_port) != 0) {
        fprintf(stderr, "Invalid TCP port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    fifo_path = (argc == 4) ? argv[3] : DEFAULT_FIFO_PATH;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    if (ensure_fifo(fifo_path) != 0) {
        goto cleanup;
    }

    /*
     * Establish the TCP connection before opening the FIFO for reading.
     * This gives the producer natural back-pressure: if the receiver is not
     * available yet, a FIFO writer will block instead of silently losing data.
     */
    tcp_fd = connect_with_backoff(remote_host, remote_port);
    if (tcp_fd < 0) {
        goto cleanup;
    }

    fprintf(stderr, "Waiting for FIFO writer on %s...\n", fifo_path);
    fifo_fd = open_fifo_reader(fifo_path);
    if (fifo_fd < 0) {
        goto cleanup;
    }

    while (!stop_requested) {
        ssize_t bytes_read = read(fifo_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            if (send_all(tcp_fd, buffer, (size_t)bytes_read) != 0) {
                perror("send");
                fprintf(stderr,
                        "TCP stream interrupted. Exiting instead of silently resuming on a new "
                        "connection because the peer's exact byte offset cannot be proven after "
                        "a disconnect. Restart the stream, or add application-level sequence/ACK "
                        "logic if seamless recovery is required.\n");
                goto cleanup;
            }
            continue;
        }

        if (bytes_read == 0) {
            /* Producer closed the FIFO. Re-open it and wait for the next writer. */
            close(fifo_fd);
            fifo_fd = -1;
            fprintf(stderr, "FIFO writer closed; waiting for the next writer...\n");
            fifo_fd = open_fifo_reader(fifo_path);
            if (fifo_fd < 0) {
                goto cleanup;
            }
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("read FIFO");
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if (fifo_fd >= 0) {
        close(fifo_fd);
    }
    if (tcp_fd >= 0) {
        close(tcp_fd);
    }

    return stop_requested ? EXIT_SUCCESS : exit_code;
}
