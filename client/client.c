#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cJSON.h"

#define SERVER_IP "127.0.0.1"
#define PORT 5566
#define BUFFER_SIZE 1024

#define HAS_RTSP_URL 1
#ifdef HAS_RTSP_URL
#define RTSP_URL "rtsp://admin:admin123456@127.0.0.1:8554/profile1"
#endif

#define BROADCAST_PORT 5567
#define DISCOVERY_TIMEOUT 30  // 30秒超时

void send_initial_message(int sock) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "reason", "init_slam");
    
    // 如果有预设的RTSP URL，则添加到初始消息中
    // 如果没有，服务器可能会在后续命令中提供URL
    #ifdef HAS_RTSP_URL
    cJSON_AddStringToObject(root, "rtsp_url", RTSP_URL);
    #endif
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(sock, json_str, strlen(json_str), 0);
    
    free(json_str);
    cJSON_Delete(root);
}

void send_status_response(int sock) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddNumberToObject(root, "battery", 85); // 假设电池电量为85%
    cJSON_AddBoolToObject(root, "is_moving", 0); // 假设当前未在移动
    cJSON_AddStringToObject(root, "current_position", "home");
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(sock, json_str, strlen(json_str), 0);
    
    free(json_str);
    cJSON_Delete(root);
}

int robot_move(const char *direction, int duration) {
    printf("robot move %s for %d seconds\n", direction, duration);
    return 0;
}

// 接收广播发现服务器
int discover_server(char *server_ip, int *server_port) {
    int broadcast_sock;
    struct sockaddr_in broadcast_addr, server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    struct timeval timeout;
    
    // 创建UDP套接字
    if ((broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("广播接收套接字创建失败");
        return -1;
    }
    
    // 设置套接字选项，允许重用地址
    int opt = 1;
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("设置套接字选项失败");
        close(broadcast_sock);
        return -1;
    }
    
    // 绑定到广播端口
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(broadcast_sock, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
        perror("绑定广播端口失败");
        close(broadcast_sock);
        return -1;
    }
    
    printf("等待发现服务器...\n");
    
    // 设置超时
    FD_ZERO(&readfds);
    FD_SET(broadcast_sock, &readfds);
    timeout.tv_sec = DISCOVERY_TIMEOUT;
    timeout.tv_usec = 0;
    
    // 等待广播消息
    if (select(broadcast_sock + 1, &readfds, NULL, NULL, &timeout) <= 0) {
        printf("发现服务器超时\n");
        close(broadcast_sock);
        return -1;
    }
    
    // 接收广播消息
    int bytes_received = recvfrom(broadcast_sock, buffer, BUFFER_SIZE - 1, 0,
                                 (struct sockaddr*)&server_addr, &addr_len);
    
    if (bytes_received <= 0) {
        perror("接收广播消息失败");
        close(broadcast_sock);
        return -1;
    }
    
    buffer[bytes_received] = '\0';
    printf("收到服务器广播: %s\n", buffer);
    
    // 解析JSON消息
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        printf("解析广播消息失败\n");
        close(broadcast_sock);
        return -1;
    }
    
    cJSON *ip = cJSON_GetObjectItem(root, "server_ip");
    cJSON *port = cJSON_GetObjectItem(root, "server_port");
    
    if (!ip || !port) {
        printf("广播消息格式错误\n");
        cJSON_Delete(root);
        close(broadcast_sock);
        return -1;
    }
    
    strcpy(server_ip, ip->valuestring);
    *server_port = port->valueint;
    
    cJSON_Delete(root);
    close(broadcast_sock);
    
    printf("发现服务器: IP=%s, 端口=%d\n", server_ip, *server_port);
    return 0;
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char server_ip[INET_ADDRSTRLEN];
    int server_port = PORT;
    
    // 尝试通过广播发现服务器
    if (discover_server(server_ip, &server_port) < 0) {
        // 如果发现失败，使用默认设置
        strcpy(server_ip, SERVER_IP);
        server_port = PORT;
        printf("使用默认服务器设置: IP=%s, 端口=%d\n", server_ip, server_port);
    }
    
    // 创建套接字
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("套接字创建失败");
        return 1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    
    // 转换IP地址
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("无效的地址/不支持的地址");
        return 1;
    }
    
    // 连接到服务器
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("连接失败");
        return 1;
    }
    
    printf("已连接到服务器 %s:%d\n", server_ip, server_port);
    
    // Send initial message
    send_initial_message(sock);
    
    // Main loop to receive commands
    while (1) {
        int bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // 确保字符串正确终止
            cJSON *root = cJSON_Parse(buffer);
            if (root) {
                cJSON *command = cJSON_GetObjectItem(root, "command");
                cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
                
                char *json_str = cJSON_Print(root);
                printf("%s\n", json_str);
                free(json_str);
                
                if (command && timestamp) {
                    printf("收到命令: %s 时间戳: %.0f\n", 
                           command->valuestring, timestamp->valuedouble);
                    
                    // 处理不同类型的命令
                    if (strcmp(command->valuestring, "check_status") == 0) {
                        // 发送状态回复
                        send_status_response(sock);
                    } else if (strcmp(command->valuestring, "move") == 0) {
                        // 处理移动命令
                        cJSON *direction = cJSON_GetObjectItem(root, "direction");
                        cJSON *duration = cJSON_GetObjectItem(root, "duration");
                        
                        if (direction && duration) {
                            printf("移动方向: %s 持续时间: %.0f秒\n", 
                                   direction->valuestring, duration->valuedouble);
                            robot_move(direction->valuestring, duration->valuedouble);  
                        } else if (direction && !duration) {
                            printf("移动方向: %s\n", direction->valuestring);
                            robot_move(direction->valuestring, 0);
                        } else if (!direction) {
                            printf("移动方向: 停止\n");
                        }
                    } else {
                        printf("unknown command: %s\n", command->valuestring);
                    }
                }
                
                cJSON_Delete(root);
            }
        } else if (bytes_read == 0) {
            printf("服务器断开连接\n");
            break;
        } else {
            perror("接收失败");
            break;
        }
    }
    
    close(sock);
    return 0;
} 