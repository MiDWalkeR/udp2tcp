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
#include <stdarg.h>
#include <time.h>

#define MESSAGE_QUEUE_NAME "/udp_to_tcp_msq"

enum buffer_sizes {
    TOTAL_BUFFER_SIZE = 256,
    MIN_UDP_PACKET_SIZE = 16,
    MAX_UDP_PACKET_SIZE = 128,
    MAX_MSG_SIZE = MAX_UDP_PACKET_SIZE
};

static struct input {
    char *udp_ip_port;
    char *tcp_ip_port;
    struct char_set {
        char first_char;
        char second_char;
        char third_char;
        char forth_char;
    } char_set;
    const char *log_file;
} in_params;

static pthread_mutex_t m_connect = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c_connect = PTHREAD_COND_INITIALIZER;

static volatile bool is_tcp_connected = false;

static char tcp_server_ip[INET_ADDRSTRLEN];
static uint32_t tcp_server_port;
static mqd_t mqdes;
static int sockfd_tcp;

static void logger_msg(const char *format, ...) {
    FILE *file = fopen(in_params.log_file, "a");

    if (file != NULL) {
        va_list args;
        va_start(args, format);

        time_t rawtime;
        struct tm *timeinfo;
        char buffer[80];

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", timeinfo);

        fprintf(file, "%s", buffer);
        vfprintf(file, format, args);
        fprintf(file, "\n");

        va_end(args);
        fclose(file);
    }
}

static int get_sockfd_udp_connection(char *ip_port) {
    int sockfd_udp = 0;
    struct sockaddr_in server_addr;

    const char *ip = strtok(ip_port, ":"); 
    int port = atoi(strtok(NULL, ":"));

    if ((sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        logger_msg("Socket creation failed: %s", strerror(errno));
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(sockfd_udp, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        logger_msg("Binding failed: %s", strerror(errno));
        return -1;
    }

    printf("UDP Receiver running on %s:%d", ip, port);
    logger_msg("UDP Receiver running on %s:%d", ip, port);

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
        logger_msg("Receiving failed: %s", strerror(errno));
        return -1;
    }

    if (len < MIN_UDP_PACKET_SIZE || len >= MAX_UDP_PACKET_SIZE) {
        printf("Received message does not meet size criteria: %ld bytes\n", len);
        return -1; 
    }

    return len;
}

static void send_message_to_tcp_thread(const char* buffer, const size_t sz) {
    int ret = mq_send(mqdes, buffer, sz, 0);
    if(ret != 0)
    {
        perror("mq_send");
        logger_msg("mq send: %s", strerror(errno));
        mq_close(mqdes);
    }
}

static void update_buffer_with_chars(char *buffer, const size_t sz, const struct char_set *chars) {
    const size_t chars_set_sz = sizeof(struct char_set) / sizeof(chars->first_char);

    if (sz + chars_set_sz >= TOTAL_BUFFER_SIZE) {
        fprintf(stderr, "Buffer is too small to add characters.\n");
        return;
    }

    memmove(buffer + chars_set_sz, buffer, sz);
    memcpy(buffer, chars, chars_set_sz);

    printf("Updated Buffer: %s\n", buffer);
}

static void get_message_from_udp_thread(char* buffer) {
    struct mq_attr attr;
    unsigned int prio;

    mq_getattr(mqdes, &attr);

    if (attr.mq_curmsgs != 0) {

        ssize_t bytes_read = mq_receive(mqdes, buffer, MAX_MSG_SIZE, &prio);
        if (bytes_read == -1) {
            perror("mq_receive");
            mq_close(mqdes);

            exit(EXIT_FAILURE);
        }
        ssize_t ret = send(sockfd_tcp, buffer, bytes_read, 0);

        if (ret == -1) {
            memset(buffer, 0, TOTAL_BUFFER_SIZE);
            pthread_mutex_lock(&m_connect);
            is_tcp_connected = false;
            pthread_cond_signal(&c_connect);
            pthread_mutex_unlock(&m_connect);
            perror("Sending failed");
            logger_msg("Sending failed: %s", strerror(errno));
            close(sockfd_tcp);

            return;
        } 

        printf("Message sent: %s\n", buffer);
        logger_msg("Message sent: %s\n", buffer);
        memset(buffer, 0, TOTAL_BUFFER_SIZE);
    }
}

void* receiver(void* arg) {
    static char buffer[TOTAL_BUFFER_SIZE];

    char *udp_ip_port = ((struct input *)arg)->udp_ip_port;
    struct char_set *chars = &((struct input *)arg)->char_set;

    int sockfd_udp = get_sockfd_udp_connection(udp_ip_port);
    if(sockfd_udp == -1) {
        exit(EXIT_FAILURE);
    }

    while (true) {
        pthread_mutex_lock(&m_connect);
        while (is_tcp_connected == false) {
            pthread_cond_wait(&c_connect, &m_connect);
        }
        pthread_mutex_unlock(&m_connect);

        const ssize_t sz = get_and_update_message(sockfd_udp, buffer);
        const size_t chars_set_sz = sizeof(struct char_set) / sizeof(chars->first_char);
        if (sz > 0) {
            update_buffer_with_chars(buffer, sz, chars);
            send_message_to_tcp_thread(buffer, sz + chars_set_sz); 
            printf("Received message: %s\n", buffer);
            logger_msg("Message sent: %s\n", buffer);
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
        logger_msg("Socket creation failed %s", strerror(errno));
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_server_port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        logger_msg("Invalid address/ Address not supported: %s", strerror(errno));
        close(sockfd_tcp);
    }

    printf("Attempting to connect to the server...\n");
    logger_msg("Attempting to connect to the server...\n");

    if(connect(sockfd_tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        pthread_mutex_lock(&m_connect);
        is_tcp_connected = false;
        pthread_cond_signal(&c_connect);
        pthread_mutex_unlock(&m_connect);

        perror("Connection failed");
        logger_msg("Connection failed %s", strerror(errno));
        close(sockfd_tcp);

        printf("Retrying connection in 3 seconds...\n");
        logger_msg("Retrying connection in 3 seconds...\n");

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
        logger_msg("Connected to the server!\n");

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
                logger_msg("Sending failed %s", strerror(errno));
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
    if (argc != 8) {
        fprintf(
            stderr,
            "Usage: %s <IP:port> <IP:port> <log_path> <first_char> <second_char> <third_char> <forth_char>\n",
            argv[0]);

        exit(EXIT_FAILURE);
    }

    in_params.udp_ip_port = argv[1];
    in_params.tcp_ip_port = argv[2];
    in_params.log_file = argv[3]; 
    in_params.char_set.first_char  = *(char*)argv[4];
    in_params.char_set.second_char = *(char*)argv[5];
    in_params.char_set.third_char  = *(char*)argv[6]; 
    in_params.char_set.forth_char  = *(char*)argv[7]; 
    
    struct mq_attr msq_attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = MAX_MSG_SIZE,
        .mq_curmsgs = 0
    };
    mqdes = mq_open(MESSAGE_QUEUE_NAME, O_RDWR | O_CREAT, 0664, &msq_attr);

    pthread_t recv_thread;
    pthread_t send_thread;
    pthread_t alive_thread;

    if (pthread_create(&recv_thread, NULL, receiver, (void *)&in_params) != 0) {
        perror("Thread creation failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&send_thread, NULL, sender, in_params.tcp_ip_port) != 0) {
        perror("Thread creation failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (pthread_create(&alive_thread, NULL, keep_alive, NULL) != 0) {
        perror("Thread creation failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pthread_join(recv_thread, NULL) != 0) {
        perror("Thread join failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pthread_join(send_thread, NULL) != 0) {
        perror("Thread join failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (pthread_join(alive_thread, NULL) != 0) {
        perror("Thread join failed");
        logger_msg("Thread creation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return 0;
}

