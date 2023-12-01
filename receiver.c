#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 128

void sigpipe_handler(int signo) {
}

void* receiver(void* arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    char* ip = strtok((char *)arg, ":"); 
    int port = atoi(strtok(NULL, ":"));

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    printf("UDP Receiver running on %s:%d\n", ip, port);

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);

        int len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);

        if (len < 0) {
            perror("Receiving failed");
            exit(EXIT_FAILURE);
        }

        printf("Received message: %s\n", buffer);
    }

    close(sockfd);

    return NULL;
}

void* sender(void* arg) {
    char* ip = strtok((char *)arg, ":");
    int port = atoi(strtok(NULL, ":"));
    char* message = "Hello, TCP server!";

    struct sigaction sa;
    sa.sa_handler = sigpipe_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    while(true) {
        int sockfd;
        struct sockaddr_in server_addr;

        if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            perror("Socket creation failed");
            continue;
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            close(sockfd);
            continue; 
        }

        printf("Attempting to connect to the server...\n");

        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            close(sockfd);
            printf("Retrying connection in 3 seconds...\n");
            sleep(3);
            continue;
        }

        printf("Connected to the server!\n");

        while (1) {
            ssize_t ret = send(sockfd, message, strlen(message), 0);

            if (ret == -1) {
                perror("Sending failed");
                break;
            }

            printf("Message sent: %s\n", message);
            sleep(3);
        }

        close(sockfd);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP:port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    pthread_t recv_thread;
    pthread_t send_thread;

    if (pthread_create(&recv_thread, NULL, receiver, argv[1]) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&send_thread, NULL, sender, argv[2]) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }


    if (pthread_join(recv_thread, NULL) != 0) {
        perror("Thread join failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_join(send_thread, NULL) != 0) {
        perror("Thread join failed");
        exit(EXIT_FAILURE);
    }
    
    return 0;
}

