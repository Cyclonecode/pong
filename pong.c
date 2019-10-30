#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>

#define BUF_SIZE 1024
#define LINE_SIZE 1024
#define SERVER_PORT 1033

struct stack_t {
    char** lines;
    int count;
};

static volatile int running = 1;

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
    FILE* fin = fopen("log.txt", "a");
    if (fin) {
        va_start(ap, fmt);
        vfprintf(fin, fmt, ap);
        va_end(ap);
        fclose(fin);
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
    int c, s, port;
    FILE* fp;
    time_t date;
    struct tm* tm_info;
    char buffer[BUF_SIZE];
    char** quotes = 0;
    char* message = "Hello sucker\n";
    char line[LINE_SIZE] = {0};
    struct sockaddr_in saddr, client_addr;
    struct stack_t stack;
    socklen_t addr_len;
    int enable = 1;
    int one = 1;
    int sts = 0;

    // Parse arguments.
    if (argc < 2) {
    }
    port = atoi(argv[1]);

    // Load quotes.
    if ((fp = fopen("/home/krister/pong/quotes.txt", "r")) == 0) {
        perror("Failed to open file.\n");
        exit(1);
    }
    // Initialize stack.
    stack.lines = 0; // (char**) malloc(sizeof(char*));
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
/*    for (int i = 0; i < stack.count; i++) {
      printf("string %d:%s\n", i, stack.lines[i]);
    }
    exit(0);
*/

    // Create socket.
    if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror("Error creating socket.\n");
        exit(1);
    }
    // Reuse address in waiting state.
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("Failed to reuse address.");
        exit(1);
    }
    // Set non-blocking mode for socket.
    if (ioctl(s, FIONBIO, (char *)&one) < 0) {
        perror("Failed to set socket in non-blocking mode.\n");
        exit(1);
    }
    // Setup address.
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    // Bind socket to specific address.
    if (bind(s, (struct sockaddr*)&saddr, sizeof(saddr)) != 0) {
        perror("Error binding to address.\n");
        exit(2);
    }
    // Set socket in listening state.
    if (listen(s, 10) != 0) {
        perror("Error listening on socket.\n");
        exit(3);
    }
    memset(&client_addr, 0, sizeof(client_addr));
    addr_len = sizeof(client_addr);

    // Randomize.
    srand(0);

    // Read in our banner.
    FILE* fin = fopen("/home/krister/pong/banner", "r");
    fseek(fin, 0, SEEK_END);
    int fs = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    char* banner = malloc(fs + 1);
    fread(banner, fs, 1, fin);
    fclose(fin);
//    free(banner);

    printf("===========================================\n");
    printf("Start waiting for clients\n");
    printf("===========================================\n");

    signal(SIGINT, sigHandler);

    while (running) {
        c = accept(s, (struct sockaddr*)&client_addr, &addr_len);
        if (c > 0) {
            FILE* fd = fdopen(c, "r+");
            time(&date);
            tm_info = localtime(&date);
            strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
            printf(" - %s connected\n%s", inet_ntoa(client_addr.sin_addr), buffer);

            // Log client connection.
            dolog("%s - %s\n", buffer, inet_ntoa(client_addr.sin_addr));

            // Just read maximum BUF_SIZE from client.
            recv(c, buffer, BUF_SIZE, 0);

            send(c, banner, fs, 0);

            int r = rand() % (stack.count - 1);
            fprintf(fd, "I have a question for you: %s?\n", stack.lines[r]);
            fflush(fd);
            fclose(fd);
            //send(c, stack.lines[r], strlen(stack.lines[r]), 0);
            //send(c, "\n", 1, 0);

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
