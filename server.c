#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <ip_address> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

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

    while(true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            return 1;
        }

        int pid = fork();

        if (pid == 0) {
            close(server_fd);

            int valread;
            while ((valread = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
                printf("Received message: %s\n", buffer);
                memset(buffer, 0, sizeof(buffer)); 
            }

            if (valread == 0) {
                printf("Client disconnected\n");
            } else {
                perror("Read failed");
            }

            close(new_socket);
            return 0;
        } else if (pid < 0) {
            perror("Fork failed");
            return 1;
        }

        close(new_socket);
    }

    close(server_fd);

    return 0;
}

