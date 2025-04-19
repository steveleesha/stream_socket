#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include "cJSON.h"

#define PORT 5566
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define COMMAND_INTERVAL 5  // Send command every 5 seconds
#define SERVER_STREAM_UPLOAD_URL "rtmp://192.168.1.100/stream"

typedef struct {
    int socket;
    char rtsp_url[256];
    char reason[256];
    char ip_addr[INET_ADDRSTRLEN];
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// 函数原型声明
void handle_client_message_by_index(int client_index, char *buffer);

// 发送设置RTSP URL命令
void send_upload_url(int client_socket, const char *url) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "upload_url", url);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(client_socket, json_str, strlen(json_str), 0);
    printf("responese upload url: %s\n", url);
    
    free(json_str);
    cJSON_Delete(root);
}

// 发送状态检查命令
void send_check_status(int client_socket) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", "check_status");
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(client_socket, json_str, strlen(json_str), 0);
    printf("send check status\n");
    free(json_str);
    cJSON_Delete(root);
}

// 发送移动命令
void send_move_command(int client_socket, const char *direction, int duration) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", "move");
    cJSON_AddStringToObject(root, "direction", direction);
    cJSON_AddNumberToObject(root, "duration", duration);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(client_socket, json_str, strlen(json_str), 0);
    printf("send move command: %s, %d\n", direction, duration);
    free(json_str);
    cJSON_Delete(root);
}

// 设置终端为非阻塞模式
void set_nonblocking_input() {
    struct termios ttystate;
    tcgetattr(STDIN_FILENO, &ttystate);
    ttystate.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
    
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

// 恢复终端设置
void reset_terminal() {
    struct termios ttystate;
    tcgetattr(STDIN_FILENO, &ttystate);
    ttystate.c_lflag |= ICANON | ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
    
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}


// 显示帮助信息
void show_help() {
    printf("\n可用命令:\n");
    printf("  c - 向所有客户端发送状态检查命令\n");
    printf("  m - 发送移动命令 (会提示输入方向和时间)\n");
    printf("  h - 显示此帮助信息\n");
    printf("  q - 退出服务器\n");
}

// 处理键盘输入的线程函数
void *keyboard_thread(void *arg) {
    (void)arg;  // 显式忽略未使用的参数
    set_nonblocking_input();
    show_help();
    
    char c;
    char input_buffer[256];
    
    while (1) {
        if (read(STDIN_FILENO, &c, 1) > 0) {
            switch (c) {
                case 'c':
                    pthread_mutex_lock(&clients_mutex);
                    for (int i = 0; i < client_count; i++) {
                        send_check_status(clients[i].socket);
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    break;
                    
                case 'm': {
                    printf("Input move direction (F/B/L/R/FL/FR/BL/BR/STOP): ");
                    reset_terminal();
                    fgets(input_buffer, sizeof(input_buffer), stdin);
                    input_buffer[strcspn(input_buffer, "\n")] = 0;
                    
                    char direction[20];
                    strncpy(direction, input_buffer, sizeof(direction) - 1);
                    direction[sizeof(direction) - 1] = '\0';
                    
                    printf("Input move duration(seconds): ");
                    fgets(input_buffer, sizeof(input_buffer), stdin);
                    int duration = atoi(input_buffer);
                    
                    pthread_mutex_lock(&clients_mutex);
                    for (int i = 0; i < client_count; i++) {
                        send_move_command(clients[i].socket, direction, duration);
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    set_nonblocking_input();
                    break;
                }
                
                case 'h':
                    show_help();
                    break;
                    
                case 'q':
                    printf("Exiting...\n");
                    reset_terminal();
                    exit(0);
                    break;
            }
        }
        usleep(100000); // 休眠100毫秒，减少CPU使用率
    }
    
    return NULL;
}

// 修改客户端处理线程函数
void *client_handler(void *arg) {
    int client_index = *((int *)arg);
    free(arg);
    
    int client_socket = clients[client_index].socket;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            // 直接使用客户端索引处理消息
            handle_client_message_by_index(client_index, buffer);
        } else if (bytes_read == 0) {
            // 客户端断开连接
            printf("client %s disconnect\n\n", clients[client_index].ip_addr);
            
            pthread_mutex_lock(&clients_mutex);
            // 移除客户端
            for (int j = client_index; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            pthread_mutex_unlock(&clients_mutex);
            
            close(client_socket);
            break;
        } else {
            // 接收错误
            perror("receive data failed");
            close(client_socket);
            break;
        }
    }
    
    return NULL;
}

// 添加一个新函数，通过索引处理客户端消息
void handle_client_message_by_index(int client_index, char *buffer) {
    ClientInfo *client = &clients[client_index];
    
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
        char *json_str = cJSON_Print(root);
        free(json_str);

        cJSON *reason = cJSON_GetObjectItem(root, "reason");
        cJSON *rtsp_url = cJSON_GetObjectItem(root, "rtsp_url");
        
        if (reason) {
            // 更新客户端信息
            strcpy(client->reason, reason->valuestring);
            printf("Client %s (ID: %d):\n", client->ip_addr, client_index);
            printf("reason: %s\n", client->reason);
            if (rtsp_url) {
                strcpy(client->rtsp_url, rtsp_url->valuestring);
                printf("rtsp_url: %s\n", client->rtsp_url);
            }

            send_upload_url(client->socket, SERVER_STREAM_UPLOAD_URL);
        }
        cJSON_Delete(root);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // 创建套接字
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // 设置套接字选项
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // 绑定套接字
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // 监听连接
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("server start, listen port %d\n", PORT);
    
    // 创建键盘输入线程
    pthread_t kb_thread;
    if (pthread_create(&kb_thread, NULL, keyboard_thread, NULL) != 0) {
        perror("create keyboard thread failed");
        exit(EXIT_FAILURE);
    }
    
    // 主循环接受客户端连接
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("----------------new client connect: %s----------------\n", client_ip);
        
        // 添加客户端到列表
        pthread_mutex_lock(&clients_mutex);
        if (client_count < MAX_CLIENTS) {
            clients[client_count].socket = new_socket;
            strcpy(clients[client_count].rtsp_url, "");
            strcpy(clients[client_count].reason, "");
            strcpy(clients[client_count].ip_addr, client_ip);
            client_count++;
            
            // 为每个客户端创建一个处理线程
            pthread_t client_thread;
            int *client_idx = malloc(sizeof(int));
            *client_idx = client_count - 1;  // 新添加的客户端索引
            
            if (pthread_create(&client_thread, NULL, client_handler, client_idx) != 0) {
                perror("create client handler thread failed");
                free(client_idx);
            } else {
                // 设置为分离状态，线程结束后自动释放资源
                pthread_detach(client_thread);
            }
        } else {
            printf("reach max client number, reject connection\n");
            close(new_socket);
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    
    // 清理
    pthread_join(kb_thread, NULL);
    close(server_fd);
    
    return 0;
} 