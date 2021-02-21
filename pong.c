#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#define BUF_SIZE 1024*2
#define LINE_SIZE 1024*2
#define LOG_FORMAT "%Y-%m-%d %H:%M:%S"
#define LOG_FILE "log.txt"
#define DEFAULT_SERVER_NAME "pong"
#define DEFAULT_BANNER_FILE "./banner"
#define DEFAULT_QUOTES_FILE "./quotes.txt"

struct stack_t {
    char **lines;
    int count;
};
// @todo insert non-volatile variable
static volatile int running = 1;

static uid_t ruid, euid;

void dump(struct stack_t *stack) {
    for (int i = 0; i < stack->count; i++) {
        printf("line %d: %s\n", i, stack->lines[i]);
    }
}

void freeStack(struct stack_t *stack) {
    if (stack) {
        // Cleanup stack.
        for (int i = 0; i < stack->count; i++) {
            free(stack->lines[i]);
        }
        free(stack->lines);
    }
}

void doLog(char *fmt, ...) {
    va_list ap;
    if (ruid == 0) {
        // Running as root.
        int uid = atoi(getenv("SUDO_UID"));
        seteuid(uid);
        setuid(uid);
    }
    FILE *fin = fopen(LOG_FILE, "a");
    if (fin) {
        va_start(ap, fmt);
        vfprintf(fin, fmt, ap);
        va_end(ap);
        fclose(fin);
    }
    if (ruid == 0) {
        // Switch back to root.
        seteuid(euid);
        setuid(ruid);
    }
}

int isWhiteListed(struct in_addr in, struct stack_t whitelist) {
    if (whitelist.count) {
        char* ip = inet_ntoa(in);
        for (int i = 0; i < whitelist.count; i++) {
            if (strncmp(ip, whitelist.lines[i], strlen(ip)) == 0) {
                return 1;
            }
        }
        return 0;
    }
    return 1;
}

void closeSocket(int s) {
    char buffer[BUF_SIZE];
    shutdown(s, SHUT_WR);
    while (recv(s, buffer, BUF_SIZE, 0) > 0);
    close(s);
}

void sigHandler(int sig) {
    char c;

    signal(sig, SIG_IGN);
    printf("Do you really want to quit? [y/n]: ");
    c = getchar();
    if (c == 'y' || c == 'Y') {
        running = 0;
        return;
    } else {
        signal(SIGINT, sigHandler);
    }
    getchar(); // Get new line character
}

void help() {
  printf("Syntax:\npong [-s name] [-q file] [-b file] [-w ip,...] [-xv] port\n");
}

int main(int argc, char **argv) {
    int c, s, port, r;
    FILE *fp;
    FILE *fbanner;
    int error;
    time_t date;
    struct tm *tm_info;
    char buffer[BUF_SIZE];
    char logBuffer[BUF_SIZE];
    char **quotes = 0;
    char line[LINE_SIZE] = {0};
    struct sockaddr_in saddr, client_addr;
    struct stack_t stack;
    struct stack_t whitelist = {
        0,
        0,
    };
    socklen_t addr_len;
    int enable = 1;
    int one = 1;
    char *banner = 0;
    ssize_t banner_len = 0;
    ssize_t read = 0;
    ssize_t header_len = 0;
    ssize_t verbose = 0;
    ssize_t pos = 0;
    char header[256];
    int opt;
    struct timeval tv = {
        1,
        0,
    };
    char *start, end;
    char *banner_file = DEFAULT_BANNER_FILE;
    char *quotes_file = DEFAULT_QUOTES_FILE;
    char *serverName = DEFAULT_SERVER_NAME;

    // Parse arguments.
    if (argc < 2) {
        help();
        exit(1);
    }

    // Get uid and euid.
    ruid = getuid();
    euid = geteuid();

    while ((opt = getopt(argc, argv, "w:q:b:s:vx")) != -1) {
        switch (opt) {
            case '?':
               help();
               exit(2);
               break;
            case 's':
            printf("%s\n", optarg);
                serverName = optarg;
                break;
            case 'x':
                banner_file = 0;
                break;
            case 'q':
                fp = fopen(optarg, "r");
                if (!fp) {
                    printf("Failed to open quotes file, trying to use default.\n");
                } else {
                    quotes_file = optarg;
                    fclose(fp);
                }
                break;
            case 'w':
            {
                char *ptr = strtok(optarg, ",");
                if (ptr) {
                    while (ptr) {
                        whitelist.lines = realloc(whitelist.lines, sizeof(char *) * (whitelist.count + 1));
                        whitelist.lines[whitelist.count] = malloc(strlen(ptr) + 1);
                        strncpy(whitelist.lines[whitelist.count], ptr, strlen(ptr));
                        whitelist.lines[whitelist.count][strlen(ptr)] = 0;
                        whitelist.count++;
                        ptr = strtok(NULL, ",");
                    }
                }
            } break;
            case 'b':
                fbanner = fopen(optarg, "r");
                if (!fbanner) {
                    printf("Failed to open banner file, trying to use default.\n");
                } else {
                    banner_file = optarg;
                    fclose(fbanner);
                }
                break;
            case 'v':
                verbose = 1;
                break;
        }
    }
    // Assume last argument is port.
    port = atoi(argv[optind]);
    if (port < 1 || port > 65535) {
        perror("Invalid port, specify a port between 1 and 65535.\n");
        exit(1);
    }

    // Check if we should set default banner.
    if (banner_file) {
        // Read in our banner.
        fbanner = fopen(banner_file, "r");
        if (fbanner) {
            fseek(fbanner, 0, SEEK_END);
            banner_len = ftell(fbanner);
            fseek(fbanner, 0, SEEK_SET);
            banner = malloc(banner_len + 1);
            fread(banner, banner_len, 1, fbanner);
            fclose(fbanner);
        }
    }

    // Load quotes.
    if ((fp = fopen(quotes_file, "r")) == 0) {
        perror("Failed to open file.\n");
        exit(1);
    }

    // Initialize stack.
    stack.lines = 0;
    stack.count = 0;
    while (fgets(line, LINE_SIZE, (FILE *) fp) != 0) {
        line[strlen(line) - 1] = 0;
        // Add to stack.
        stack.lines = realloc(stack.lines, sizeof(char *) * (stack.count + 1));
        stack.lines[stack.count] = malloc(strlen(line) + 1);
        strncpy(stack.lines[stack.count], line, strlen(line));
        stack.lines[stack.count][strlen(line)] = 0;
        stack.count++;
    }
    fclose(fp);

    // Create socket.
    if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        error = errno;
        perror("Error creating socket");
        exit(error);
    }
    // Reuse address in waiting state.
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        error = errno;
        perror("Failed to reuse address");
        exit(error);
    }
    // Set non-blocking mode for socket.
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

    // Setup address.
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    // Bind socket to specific address.
    if (bind(s, (struct sockaddr *) &saddr, sizeof(saddr)) != 0) {
        error = errno;
        perror("Error binding to address");
        exit(error);
    }
    // Set socket in listening state.
    if (listen(s, 10) != 0) {
        error = errno;
        perror("Error listening on socket");
        exit(error);
    }
    memset(&client_addr, 0, sizeof(client_addr));
    addr_len = sizeof(client_addr);

    // Randomize.
    srand(0);

    printf("===========================================\n");
    printf("Start waiting for clients\n");
    printf("===========================================\n");

    signal(SIGINT, sigHandler);

    // Ignore SIGPIPE trigged when sending data to an already closed socket.
    signal(SIGPIPE, SIG_IGN);
    while (running) {
        c = accept(s, (struct sockaddr *) &client_addr, &addr_len);
        if (c > 0) {
            if (!isWhiteListed(client_addr.sin_addr, whitelist)) {
                printf("Client %s is not allowed to connect.\n", inet_ntoa(client_addr.sin_addr));
                if (verbose) {
                  doLog("Client %s is not allowed to connect.\n", inet_ntoa(client_addr.sin_addr));
                }
                header_len = snprintf(header, sizeof(header), "HTTP/1.1 401 Unauthorized\nServer: %s\n\n", serverName);
                send(c, header, header_len, 0);
                closeSocket(c);
                continue;
            }
            // TODO: Use select instead to see when something is happening on the socket.
            // Set client in non-blocking mode.
            // fcntl(c, F_SETFL, O_NONBLOCK);

            // Set timeout for blocking calls.
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

            time(&date);
            tm_info = localtime(&date);
            strftime(logBuffer, 26, LOG_FORMAT, tm_info);

            // Log client connection.
            doLog("%s - %s\n\n", logBuffer, inet_ntoa(client_addr.sin_addr));
            printf("%s - %s\n", logBuffer, inet_ntoa(client_addr.sin_addr));

            // Read all initial data from client.
            pos = 0;
            do {
                read = recv(c, &buffer[pos], BUF_SIZE, 0);
                if (read > 0) {
                    pos += read;
                }
            } while (read > 0);
            buffer[pos] = 0;

            if (pos == 0) {
                close(c);
                usleep(250);
                continue;
            }

            // Log request headers and body in verbose mode.
            if (verbose) {
                doLog("%s\n", buffer);
            }

            // Method SP Request-URI SP HTTP-Version CRLF
            char *start = strchr(buffer, ' ') + 1;
            char *end = strchr(start, ' ');

            // Check so we don't have any path
            if (end - start > 1) {
                // Always send 404
                header_len = sprintf(header, "HTTP/1.1 404 NOT FOUND\n\n");
                send(c, header, header_len, 0);
                close(c);
                usleep(250);
                continue;
            }

            r = rand() % (stack.count - 1);

            header_len = snprintf(header,
                                 sizeof(header),
                                 "HTTP/1.1 200 OK\nServer: %s\nContent-Type: text/plain\nContent-Length: %lu\n\n",
                                 serverName,
                                 banner_len + strlen(stack.lines[r]));

            // First send a valid http response header.
            send(c, header, header_len, 0);

            // Then send the banner.
            send(c, banner, banner_len, 0);

            // Finally send a random quote to the client.
            send(c, stack.lines[r], strlen(stack.lines[r]), 0);

            // Close client socket.
            closeSocket(c);
        }
        usleep(250);
    }

    close(s);
    freeStack(&stack);
    freeStack(&whitelist);
    free(banner);
    exit(0);

    return 0;
}
