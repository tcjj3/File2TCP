#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/*
mkfifo /tmp/mysocket
socat -d -d -lmlocal2 -u FILE:/tmp/mysocket TCP:127.0.0.1:12345
echo "Hello, World" > /tmp/mysocket
*/


/*
#define FIFO_PATH "/tmp/mysocket"
#define REMOTE_TCP_IP "127.0.0.1"
#define REMOTE_TCP_PORT 12345
*/

#define BUFFER_SIZE 1024


int main(int argc, char** argv) {
    int fifo_fd, remote_tcp_socket_fd;
    struct sockaddr_in remote_tcp_socket_addr;
    char buffer[BUFFER_SIZE];
    
    char* REMOTE_TCP_IP = NULL;
    char* STR_REMOTE_TCP_PORT = NULL;
    int REMOTE_TCP_PORT = 0;
    if (argc > 2) {
        REMOTE_TCP_IP = argv[1];
        STR_REMOTE_TCP_PORT = argv[2];
        REMOTE_TCP_PORT = atoi(STR_REMOTE_TCP_PORT);
    } else {
        printf("Usage: %s REMOTE_TCP_IP REMOTE_TCP_PORT [FIFO_PATH]", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    char* FIFO_PATH = NULL;
    if (argc > 2) {
        FIFO_PATH = argv[3];
    } else {
        FIFO_PATH = "/tmp/mysocket";
    }
    
    // 创建命名管道
    unlink(FIFO_PATH);
    /*
    if (mkfifo(FIFO_PATH, 0666) == -1) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }
    */
    while (mkfifo(FIFO_PATH, 0666) == -1) {
        unlink(FIFO_PATH);
    }

    // 打开命名管道进行读取
    /*
    if ((fifo_fd = open(FIFO_PATH, O_RDONLY)) == -1) {
        perror("open");
        exit(EXIT_FAILURE);
        unlink(FIFO_PATH);
    }
    */
    while ((fifo_fd = open(FIFO_PATH, O_RDONLY)) == -1) {
        
    }

    // 创建远程TCP套接字
    /*
    if ((remote_tcp_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
        close(fifo_fd);
        unlink(FIFO_PATH);
    }
    */
    while ((remote_tcp_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        
    }

    memset(&remote_tcp_socket_addr, 0, sizeof(remote_tcp_socket_addr));
    remote_tcp_socket_addr.sin_family = AF_INET;
    remote_tcp_socket_addr.sin_port = htons(REMOTE_TCP_PORT);
    inet_aton(REMOTE_TCP_IP, &remote_tcp_socket_addr.sin_addr);

    // 连接到远程TCP端口
    /*
    if (connect(remote_tcp_socket_fd, (struct sockaddr*)&remote_tcp_socket_addr, sizeof(remote_tcp_socket_addr)) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
        close(fifo_fd);
        unlink(FIFO_PATH);
    }
    */
    while (connect(remote_tcp_socket_fd, (struct sockaddr*)&remote_tcp_socket_addr, sizeof(remote_tcp_socket_addr)) == -1) {
        
    }

    // 从命名管道中读取数据，并发送到远程TCP端口
    /*
    ssize_t bytes_read;
    while ((bytes_read = read(fifo_fd, buffer, BUFFER_SIZE)) > 0) {
        ssize_t bytes_sent = send(remote_tcp_socket_fd, buffer, bytes_read, 0);
        if (bytes_sent == -1) {
            perror("send");
            exit(EXIT_FAILURE);
            close(fifo_fd);
            unlink(FIFO_PATH);
        }
    }
    */
    ssize_t bytes_read;
    while (1) {
        if ((bytes_read = read(fifo_fd, buffer, BUFFER_SIZE)) > 0) {
            ssize_t bytes_sent = send(remote_tcp_socket_fd, buffer, bytes_read, 0);
            /*
            if (bytes_sent == -1) {
                perror("send");
                exit(EXIT_FAILURE);
                close(fifo_fd);
                unlink(FIFO_PATH);
            }
            */
            while (bytes_sent == -1) {
                close(remote_tcp_socket_fd);
                
                while ((remote_tcp_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                    
                }
                memset(&remote_tcp_socket_addr, 0, sizeof(remote_tcp_socket_addr));
                remote_tcp_socket_addr.sin_family = AF_INET;
                remote_tcp_socket_addr.sin_port = htons(REMOTE_TCP_PORT);
                inet_aton(REMOTE_TCP_IP, &remote_tcp_socket_addr.sin_addr);
                while (connect(remote_tcp_socket_fd, (struct sockaddr*)&remote_tcp_socket_addr, sizeof(remote_tcp_socket_addr)) == -1) {
                    
                }
                
                bytes_sent = send(remote_tcp_socket_fd, buffer, bytes_read, 0);
            }
        }
    }

    // 关闭套接字和命名管道
    close(fifo_fd);
    close(remote_tcp_socket_fd);
    unlink(FIFO_PATH);

    return 0;
}
