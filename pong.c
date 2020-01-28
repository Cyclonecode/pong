#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#define BUF_SIZE 1024
#define LINE_SIZE 1024
#define DEFAULT_PORT 8080
#define LOG_FORMAT "%Y-%m-%d %H:%M:%S"

struct stack_t {
    char** lines;
    int count;
};
// @todo insert non-volatile variable
static volatile int running = 1;

static uid_t ruid, euid;

void dump(struct stack_t* stack) {
    for (int i = 0; i < stack->count; i++) {
        printf("line %d: %s\n", i, stack->lines[i]);
    }
}

void free_stack(struct stack_t* stack) {
    if (stack) {
        // Cleanup stack.
        for (int i = 0; i < stack->count; i++) {
            free(stack->lines[i]);
        }
        free(stack->lines);
    }
}

void dolog(char* fmt, ...) {
    va_list ap;
    if (ruid == 0) {
      // Running as root.
      int uid = atoi(getenv("SUDO_UID"));
      seteuid(uid);
      setuid(uid);
    }
    FILE* fin = fopen("log.txt", "a");
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

void sigHandler(int sig) {
     char  c;

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

int main(int argc, char** argv) {
    int c, s, port, r;
    FILE* fp;
    FILE* fd;
    FILE* fbanner;
    int error;
    time_t date;
    struct tm* tm_info;
    char buffer[BUF_SIZE];
    char** quotes = 0;
    char line[LINE_SIZE] = {0};
    struct sockaddr_in saddr, client_addr;
    struct stack_t stack;
    socklen_t addr_len;
    int enable = 1;
    int one = 1;
    char* banner = 0;
    int banner_len = 0;
    ssize_t read = 0;
    char header[128];
    int opt;
    char* banner_file = "./banner";

    // Parse arguments.
    if (argc < 2) {
        perror("Syntax: pong <port>\n");
        exit(1);
    }

    // Get uid and euid.
    ruid = getuid();
    euid = geteuid();

    while ((opt = getopt(argc, argv, "b:")) != -1) {
        switch (opt) {
          case 'b':
            fbanner = fopen(optarg, "r");
            if (!fbanner) {
              printf("Failed to open banner file, trying to use default.\n");
            } else {
                banner_file = optarg;
                fclose(fbanner);
            }
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
    if (!banner) {
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
    if ((fp = fopen("./quotes.txt", "r")) == 0) {
        perror("Failed to open file.\n");
        exit(1);
    }

    // Initialize stack.
    stack.lines = 0;
    stack.count = 0;
    while (fgets(line, LINE_SIZE, (FILE*) fp) != 0)  {
        line[strlen(line) - 1] = 0;
        // Add to stack.
        stack.lines = realloc(stack.lines, sizeof(char*) * (stack.count + 1));
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
    if (bind(s, (struct sockaddr*)&saddr, sizeof(saddr)) != 0) {
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
        c = accept(s, (struct sockaddr*)&client_addr, &addr_len);
        if (c > 0) {
            // Set client in non-blocking mode.
            fcntl(c, F_SETFL, O_NONBLOCK);

            fd = fdopen(c, "r+");
            time(&date);
            tm_info = localtime(&date);
            strftime(buffer, 26, LOG_FORMAT, tm_info);

            // Log client connection.
            dolog("%s - %s\n", buffer, inet_ntoa(client_addr.sin_addr));

            printf("%s - %s\n", buffer, inet_ntoa(client_addr.sin_addr));

            // Read all initial data from client.
            do {
                read = recv(c, buffer, BUF_SIZE, 0);
            } while (read > 0);

            r = rand() % (stack.count - 1);

            int header_len = sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n", 1 + banner_len + strlen(stack.lines[r]));

            // First send a valid http response header.
            send(c, header, header_len, 0);

            // Then send the banner.
            send(c, banner, banner_len, 0);

            // Finally send a random quote to the client.
            fprintf(fd, "%s\n", stack.lines[r]);
            fflush(fd);
            fclose(fd);

            // Close client socket.
            close(c);
        }
        usleep(250);
    }

    printf("cleaning up\n");
    close(s);
    free_stack(&stack);
    free(banner);
    exit(0);

    return 0;
}
