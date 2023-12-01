#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 128
#define GET_FROM_PORT 8080

#define SEND_TO_PORT 8080
#define SERVER_IP "127.0.0.1"

void* receiver(void* arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(GET_FROM_PORT);

    // Bind socket
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    printf("UDP Receiver running on port %d\n", GET_FROM_PORT);

    while(true) {
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
    int sockfd;
    struct sockaddr_in server_addr;
    char message[] = "Hello, TCP server!";

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SEND_TO_PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Attempt to connect to the server
        while (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("Connection failed. Retrying...\n");
            sleep(3); // Wait for 3 seconds before retrying
        }

        // Once connected, send messages in a loop
        while (1) {
            if (send(sockfd, message, strlen(message), 0) < 0) {
                perror("Sending failed");
                close(sockfd);
                break; // Break the sending loop on failure
            }
            printf("Message sent: %s\n", message);

            sleep(3); // 3-second interval between messages
        }
    }

    close(sockfd);
    return NULL;
}

int main(void) {
    pthread_t recv_thread;
    pthread_t send_thread;

    if(pthread_create(&recv_thread, NULL, receiver, NULL) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }
    
    if(pthread_create(&send_thread, NULL, sender, NULL) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    pthread_join(recv_thread, NULL);
    pthread_join(send_thread, NULL);

    return 0;
}

