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

void send_initial_message(int sock) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "reason", "stream_monitoring");
    
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

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IP address to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return 1;
    }
    
    // Connect to server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return 1;
    }
    
    printf("Connected to server\n");
    
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