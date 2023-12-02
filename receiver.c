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
#include <errno.h>
#include <mqueue.h>

#define MESSAGE_QUEUE_NAME "/udp_to_tcp_msq"

enum buffer_sizes {
    TOTAL_BUFFER_SIZE = 256,
    MIN_UDP_PACKET_SIZE = 16,
    MAX_UDP_PACKET_SIZE = 128,
    MAX_MSG_SIZE = MAX_UDP_PACKET_SIZE
};

static pthread_mutex_t m_connect = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c_connect = PTHREAD_COND_INITIALIZER;

static volatile bool is_tcp_connected = false;

static char tcp_server_ip[INET_ADDRSTRLEN];
static uint32_t tcp_server_port;
static mqd_t mqdes;
static int sockfd_tcp;

static int get_sockfd_udp_connection(char *ip_port) {
    int sockfd_udp = 0;
    struct sockaddr_in server_addr;

    const char *ip = strtok(ip_port, ":"); 
    int port = atoi(strtok(NULL, ":"));

    if ((sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(sockfd_udp, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        return -1;
    }

    printf("UDP Receiver running on %s:%d\n", ip, port);

    return sockfd_udp;
}

static ssize_t get_and_update_message(const int sockfd, char *buffer)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    memset(buffer, 0, TOTAL_BUFFER_SIZE);
    ssize_t len = recvfrom(
            sockfd,
            buffer,
            TOTAL_BUFFER_SIZE,
            0,
            (struct sockaddr *)&client_addr,
            &client_addr_len);

    if (len < 0) {
        perror("Receiving failed");
        return -1;
    }

    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

    if (strcmp(sender_ip, tcp_server_ip) == 0) {
        printf("Received packet from TCP server, ignoring...\n");
        return -1; 
    }

    if (len < MIN_UDP_PACKET_SIZE || len >= MAX_UDP_PACKET_SIZE) {
        printf("Received message does not meet size criteria: %ld bytes\n", len);
        return -1; 
    }

    return len;
}

static void send_message_to_tcp_thread(const char* buffer, const size_t sz) {
    struct mq_attr attr;
    struct mq_attr old_attr;

    mq_setattr(mqdes, &attr, &old_attr); 

    int ret = mq_send(mqdes, buffer, sz, 0);
    if(ret != 0)
    {
        perror("mq_send");
    }

    mq_getattr(mqdes, &attr); 
    mq_setattr(mqdes, &old_attr, 0); 
}

static void get_message_from_udp_thread(char* buffer) {
    struct mq_attr attr;
    struct mq_attr old_attr;
    unsigned int prio;

    mq_getattr(mqdes, &attr);

    if (attr.mq_curmsgs != 0) {
        attr.mq_flags = O_NONBLOCK;

        mq_setattr(mqdes, &attr, &old_attr);
        ssize_t bytes_read = mq_receive(mqdes, buffer, MAX_MSG_SIZE, &prio);
        mq_setattr(mqdes, &old_attr, 0);

        ssize_t ret = send(sockfd_tcp, buffer, bytes_read, 0);

        if (ret == -1) {
            memset(buffer, 0, TOTAL_BUFFER_SIZE);
            pthread_mutex_lock(&m_connect);
            is_tcp_connected = false;
            pthread_cond_signal(&c_connect);
            pthread_mutex_unlock(&m_connect);
            perror("Sending failed");
            close(sockfd_tcp);

            return;
        } 

        memset(buffer, 0, TOTAL_BUFFER_SIZE);
        printf("Message sent: %s\n", buffer);
    }
}

void* receiver(void* arg) {
    static char buffer[TOTAL_BUFFER_SIZE];

    int sockfd_udp = get_sockfd_udp_connection((char *)arg);
    if(sockfd_udp == -1) {
        exit(EXIT_FAILURE);
    }

    while (true) {
        pthread_mutex_lock(&m_connect);
        while (is_tcp_connected == false) {
            pthread_cond_wait(&c_connect, &m_connect);
        }
        pthread_mutex_unlock(&m_connect);

        ssize_t sz = get_and_update_message(sockfd_udp, buffer);
        if (sz > 0) {
            send_message_to_tcp_thread(buffer, sz); 
            printf("Received message: %s\n", buffer);
        }
    }

    mq_close(mqdes);
    close(sockfd_udp);

    return NULL;
}

static int connect_to_the_server(const char* ip, const uint32_t tcp_server_port) {
    struct sockaddr_in server_addr;

    if ((sockfd_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Socket creation failed");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_server_port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd_tcp);
    }

    printf("Attempting to connect to the server...\n");

    if(connect(sockfd_tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        pthread_mutex_lock(&m_connect);
        is_tcp_connected = false;
        pthread_cond_signal(&c_connect);
        pthread_mutex_unlock(&m_connect);

        perror("Connection failed");
        close(sockfd_tcp);

        printf("Retrying connection in 3 seconds...\n");

        return -1;
    }

    pthread_mutex_lock(&m_connect);
    is_tcp_connected = true;
    pthread_cond_signal(&c_connect);
    pthread_mutex_unlock(&m_connect);

    return 0;
}

void* sender(void* arg) {
    const char *ip = strtok((char *)arg, ":");
    static char buffer[TOTAL_BUFFER_SIZE];
    tcp_server_port = atoi(strtok(NULL, ":"));
    memcpy(tcp_server_ip, ip, strlen(ip));
    
    signal(SIGPIPE, SIG_IGN);
    
    while(true) {
        int ret = connect_to_the_server(ip, tcp_server_port);
        if (ret == -1) {
            sleep(2);
            continue;
        }

        printf("Connected to the server!\n");

        while (true) {
            if (is_tcp_connected == false) {
                break;
            }

            get_message_from_udp_thread(buffer);
            sleep(3);
        }
    }

    mq_close(mqdes);
    close(sockfd_tcp);

    return NULL;
}

void* keep_alive(void* arg) {

    while(true) {
        pthread_mutex_lock(&m_connect);
        if(is_tcp_connected) {
            ssize_t ret = send(sockfd_tcp, ".", 1, 0);

            if (ret == -1) {
                is_tcp_connected = false;
                perror("Sending failed");
                close(sockfd_tcp);
            } 
        }
        pthread_cond_signal(&c_connect);
        pthread_mutex_unlock(&m_connect);

        sleep(3);
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
    pthread_t alive_thread;

    if (pthread_create(&recv_thread, NULL, receiver, argv[1]) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&send_thread, NULL, sender, argv[2]) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }
    
    if (pthread_create(&alive_thread, NULL, keep_alive, argv[2]) != 0) {
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
    
    if (pthread_join(alive_thread, NULL) != 0) {
        perror("Thread join failed");
        exit(EXIT_FAILURE);
    }

    return 0;
}

