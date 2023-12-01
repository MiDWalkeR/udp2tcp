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
#include <mqueue.h>

#define MESSAGE_QUEUE_NAME "/udp_to_tcp_msq"

enum buffer_sizes {
    TOTAL_BUFFER_SIZE = 256,
    MIN_UDP_PACKET_SIZE = 16,
    MAX_UDP_PACKET_SIZE = 128,
    MAX_MSG_SIZE = MAX_UDP_PACKET_SIZE
};

static pthread_mutex_t m_connect = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c_cond = PTHREAD_COND_INITIALIZER;

static bool is_tcp_connected = false;
static char tcp_server_ip[INET_ADDRSTRLEN];
static uint32_t tcp_server_port;

static mqd_t mqdes;

void sigpipe_handler(int signo) {}

void* receiver(void* arg) {
    struct mq_attr attr;
    struct mq_attr old_attr;
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    static char buffer[TOTAL_BUFFER_SIZE];

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
        pthread_mutex_lock(&m_connect);
        while (is_tcp_connected == false) {
            pthread_cond_wait(&c_cond, &m_connect);
        }
        pthread_mutex_unlock(&m_connect);

        memset(buffer, 0, TOTAL_BUFFER_SIZE);

        int len = recvfrom(sockfd, buffer, TOTAL_BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);

        if (len < 0) {
            perror("Receiving failed");
            exit(EXIT_FAILURE);
        }

        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

        // Compare with the address and port of the TCP server
        //if (strcmp(sender_ip, tcp_server_ip) == 0) {
        //    printf("Received packet from TCP server, ignoring...\n");
        //    continue;
        //}

        if (len < MIN_UDP_PACKET_SIZE || len >= MAX_UDP_PACKET_SIZE) {
            printf("Received message does not meet size criteria: %d bytes\n", len);
            continue;
        }
        
        attr.mq_flags = O_RDWR;
        mq_setattr(mqdes, &attr, &old_attr); 

        int ret = mq_send(mqdes, buffer, len, 0);
        if(ret != 0)
        {
            perror("mq_send");
        }

        mq_getattr(mqdes, &attr); 
        mq_setattr(mqdes, &old_attr, 0); 

        printf("Received message: %s\n", buffer);
    }

    mq_close(mqdes);
    close(sockfd);

    return NULL;
}

void* sender(void* arg) {
    const char *ip = strtok((char *)arg, ":");
    static char buffer[TOTAL_BUFFER_SIZE];
    struct mq_attr attr;
    struct mq_attr old_attr;
    unsigned int prio;

    tcp_server_port = atoi(strtok(NULL, ":"));
    memcpy(tcp_server_ip, ip, strlen(ip));
    
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
        server_addr.sin_port = htons(tcp_server_port);

        if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            close(sockfd);
            continue; 
        }

        printf("Attempting to connect to the server...\n");

        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            pthread_mutex_lock(&m_connect);
            is_tcp_connected = false;
            pthread_cond_signal(&c_cond);
            pthread_mutex_unlock(&m_connect);

            perror("Connection failed");
            close(sockfd);

            printf("Retrying connection in 3 seconds...\n");
            sleep(3);

            continue;
        }

        printf("Connected to the server!\n");

        pthread_mutex_lock(&m_connect);
        is_tcp_connected = true;
        pthread_cond_signal(&c_cond);
        pthread_mutex_unlock(&m_connect);

        while(true) {
            mq_getattr(mqdes, &attr);

            if(attr.mq_curmsgs != 0)
            {
                attr.mq_flags = O_NONBLOCK;

                mq_setattr(mqdes, &attr, &old_attr);
                ssize_t bytes_read = mq_receive(mqdes, buffer, MAX_MSG_SIZE, &prio);
                mq_setattr(mqdes, &old_attr, 0);

                ssize_t ret = send(sockfd, buffer, bytes_read, 0);

                if (ret == -1) {
                    perror("Sending failed");
                    break;
                }

                printf("Message sent: %s\n", buffer);
                sleep(3);

            }
        }

        mq_close(mqdes);
        close(sockfd);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP:port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct mq_attr msq_attr;
    mqdes = mq_open(MESSAGE_QUEUE_NAME, O_RDWR | O_CREAT, 0664, &msq_attr);

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

