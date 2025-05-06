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
#include <sys/types.h>

#define PORT 5566
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define COMMAND_INTERVAL 5  // Send command every 5 seconds
#define SERVER_STREAM_UPLOAD_URL "rtmp://192.168.1.100/stream"
#define BROADCAST_PORT 5567
#define BROADCAST_INTERVAL 5  // 每5秒广播一次

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

// 发送获取JPEG图像命令
void send_get_jpeg_command(int client_socket) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", "get_jpeg");
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(client_socket, json_str, strlen(json_str), 0);
    printf("send get_jpeg command\n");
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
    printf("  j - 请求所有客户端发送一张JPEG图像\n");
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
                
                case 'j':
                    pthread_mutex_lock(&clients_mutex);
                    for (int i = 0; i < client_count; i++) {
                        send_get_jpeg_command(clients[i].socket);
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    break;
                    
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

// 接收客户端发送的JPEG图像
int receive_jpeg_image(int client_socket, const char *client_ip, long long size) {
    char image_path[256];
    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    
    // 创建保存图像的目录
    mkdir("images", 0755);
    
    // 生成唯一文件名，包含客户端IP和时间
    sprintf(image_path, "images/%s_%04d%02d%02d_%02d%02d%02d.jpg",
            client_ip,
            time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
            time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
    
    // 打开文件进行写入
    FILE *fp = fopen(image_path, "wb");
    if (!fp) {
        perror("无法创建图像文件");
        return -1;
    }
    
    printf("正在接收图像数据，大小: %lld 字节\n", size);
    
    // 读取并保存图像数据
    char buffer[4096];
    size_t bytes_read;
    size_t total_received = 0;
    
    while (total_received < size) {
        bytes_read = recv(client_socket, buffer, 
                         sizeof(buffer) < (size - total_received) ? sizeof(buffer) : (size - total_received), 
                         0);
        
        if (bytes_read <= 0) {
            perror("接收图像数据失败");
            fclose(fp);
            return -1;
        }
        
        fwrite(buffer, 1, bytes_read, fp);
        total_received += bytes_read;
        
        // 显示进度
        printf("\r接收进度: %.1f%%", (total_received * 100.0) / size);
        fflush(stdout);
    }
    
    printf("\n图像接收完成，已保存至 %s\n", image_path);
    fclose(fp);
    
    return 0;
}

// 添加一个新函数，通过索引处理客户端消息
void handle_client_message_by_index(int client_index, char *buffer) {
    ClientInfo *client = &clients[client_index];
    
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
        char *json_str = cJSON_Print(root);
        printf("\n%s\n", json_str);
        free(json_str);

        // 处理初始化消息
        cJSON *reason = cJSON_GetObjectItem(root, "reason");
        cJSON *rtsp_url = cJSON_GetObjectItem(root, "rtsp_url");
        
        // 处理图像响应
        cJSON *response = cJSON_GetObjectItem(root, "response");
        cJSON *size = cJSON_GetObjectItem(root, "size");
        
        if (reason) {
            // 初始化消息处理
            strcpy(client->reason, reason->valuestring);
            printf("Client %s (ID: %d):\n", client->ip_addr, client_index);
            printf("reason: %s\n", client->reason);
            if (rtsp_url) {
                strcpy(client->rtsp_url, rtsp_url->valuestring);
                printf("rtsp_url: %s\n", client->rtsp_url);
            }

            send_upload_url(client->socket, SERVER_STREAM_UPLOAD_URL);
        } else if (response && strcmp(response->valuestring, "jpeg_image") == 0 && size) {
            // JPEG图像响应处理
            printf("接收到图像响应，客户端: %s\n", client->ip_addr);
            
            // 接收图像数据
            receive_jpeg_image(client->socket, client->ip_addr, (long long)size->valuedouble);
        }
        cJSON_Delete(root);
    }
}

// 广播线程函数
void *broadcast_thread(void *arg) {
    int broadcast_sock;
    struct sockaddr_in broadcast_addr;
    int broadcast_enable = 1;
    char broadcast_message[256];
    
    // 创建UDP套接字
    if ((broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("broadcast socket create failed");
        return NULL;
    }
    
    // 设置广播选项
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("set broadcast option failed");
        close(broadcast_sock);
        return NULL;
    }
    
    // 设置广播地址
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");  // 广播地址
    
    // 获取本机IP地址
    char host_ip[INET_ADDRSTRLEN];
    struct sockaddr_in host_addr;
    socklen_t host_len = sizeof(host_addr);
    
    // 创建一个临时套接字来获取本机IP
    int temp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (temp_sock < 0) {
        perror("temp socket create failed");
        close(broadcast_sock);
        return NULL;
    }
    
    // 连接到一个外部地址（不会真正发送数据）
    memset(&host_addr, 0, sizeof(host_addr));
    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(80);
    host_addr.sin_addr.s_addr = inet_addr("8.8.8.8");  // 谷歌DNS服务器
    
    if (connect(temp_sock, (struct sockaddr*)&host_addr, sizeof(host_addr)) < 0) {
        perror("temp connect failed");
        close(temp_sock);
        close(broadcast_sock);
        return NULL;
    }
    
    // 获取本机地址
    if (getsockname(temp_sock, (struct sockaddr*)&host_addr, &host_len) < 0) {
        perror("get local ip failed");
        close(temp_sock);
        close(broadcast_sock);
        return NULL;
    }
    
    inet_ntop(AF_INET, &host_addr.sin_addr, host_ip, INET_ADDRSTRLEN);
    close(temp_sock);
    
    printf("server ip: %s\n", host_ip);
    
    // 准备广播消息
    snprintf(broadcast_message, sizeof(broadcast_message), 
             "{\"server_ip\":\"%s\",\"server_port\":%d}", 
             host_ip, PORT);
    
    // 广播循环
    while (1) {
        if (sendto(broadcast_sock, broadcast_message, strlen(broadcast_message), 0,
                  (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
            perror("broadcast send failed");
        } else {
            printf("Broadcast message: %s\n", broadcast_message);
        }
        
        sleep(BROADCAST_INTERVAL);
    }
    
    close(broadcast_sock);
    return NULL;
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
    
    // 在main函数中创建广播线程
    pthread_t bc_thread;
    if (pthread_create(&bc_thread, NULL, broadcast_thread, NULL) != 0) {
        perror("create broadcast thread failed");
        // 继续运行，不退出
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