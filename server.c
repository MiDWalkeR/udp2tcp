#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

static int server_fd;
static struct sockaddr_in address;
static size_t addrlen = sizeof(address);

void send_udp(const char *ip, int port) {
    static char buffer[BUFFER_SIZE];
    int udp_socket;
    struct sockaddr_in udp_address;

    if ((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&udp_address, 0, sizeof(udp_address));
    memset(buffer, 0, BUFFER_SIZE);

    udp_address.sin_family = AF_INET;
    udp_address.sin_port = htons(port);
    udp_address.sin_addr.s_addr = inet_addr(ip);

    snprintf(buffer, BUFFER_SIZE, "UDP message from server to %s:%d", ip, port);
    sendto(udp_socket, buffer, strlen(buffer), 0, (const struct sockaddr *)&udp_address, sizeof(udp_address));

    close(udp_socket);
}

void send_tcp(const char *ip, int port) {
    int tcp_socket;
    struct sockaddr_in tcp_address;
    static char buffer[BUFFER_SIZE];

    if ((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&tcp_address, 0, sizeof(tcp_address));
    memset(buffer, 0, BUFFER_SIZE);

    tcp_address.sin_family = AF_INET;
    tcp_address.sin_port = htons(port);
    tcp_address.sin_addr.s_addr = inet_addr(ip);

    if (connect(tcp_socket, (struct sockaddr *)&tcp_address, sizeof(tcp_address)) < 0) {
        perror("TCP Connect failed");
        close(tcp_socket);
        return;
    }

    snprintf(buffer, BUFFER_SIZE, "TCP message from server to %s:%d", ip, port);
    send(tcp_socket, buffer, strlen(buffer), 0);

    close(tcp_socket);
}

void* send_messages(void* arg) {
    while (true) {
        //send_udp("127.0.0.1", 8080);
        send_tcp("127.0.0.1", 8080);

        sleep(5); 
    }
    return NULL;
}

void* receive_messages(void* arg) {
    static char buffer[BUFFER_SIZE];

    while (true) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
            return NULL;
        }

        memset(buffer, 0, BUFFER_SIZE);
        int valread;
        while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
            printf("Received message: %s\n", buffer);
        }

        if (valread == 0) {
            printf("Client disconnected\n");
        } else {
            perror("Read failed");
        }

        close(new_socket);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <ip_address> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server running on %s:%d\n", ip, port);

    pthread_t send_thread, receive_thread;

    // Create threads
    if (pthread_create(&send_thread, NULL, send_messages, NULL) != 0) {
        perror("Thread creation for sending failed");
        return 1;
    }

    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Thread creation for receiving failed");
        return 1;
    }

    // Wait for threads to finish
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);

    return 0;
}


