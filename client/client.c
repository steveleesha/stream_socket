#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>   // 添加时间头文件
#include <sys/stat.h>  // 添加文件状态头文件
#include <fcntl.h>  // 添加文件控制头文件
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

// 全局变量，用于控制连接状态
volatile int connected = 0;
int server_sock = -1;

// 信号处理函数，用于优雅地关闭连接
void signal_handler(int sig) {
    if (server_sock >= 0) {
        printf("关闭连接并退出...\n");
        close(server_sock);
    }
    exit(0);
}

// 发送初始消息
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

// 连接到服务器的函数
int connect_to_server(const char *server_ip, int server_port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    // 创建套接字
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("套接字创建失败");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    
    // 转换IP地址
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("无效的地址/不支持的地址");
        close(sock);
        return -1;
    }
    
    // 连接到服务器
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("连接失败");
        close(sock);
        return -1;
    }
    
    printf("已连接到服务器 %s:%d\n", server_ip, server_port);
    
    // 发送初始消息
    send_initial_message(sock);
    
    return sock;
}

// 模拟拍摄照片，实际应用中可能调用摄像头API
int capture_jpeg(const char *filename) {
    // 模拟拍照，仅用于演示
    // 实际应用中，这里应该调用相机API拍照
    printf("模拟拍摄照片: %s\n", filename);
    
    // 创建一个简单的测试图像文件（如果实际系统中没有摄像头）
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("无法创建图像文件");
        return -1;
    }
    
    // 写入一些假数据，表示JPEG文件内容
    const char *test_data = "JPEG TEST IMAGE DATA";
    fwrite(test_data, strlen(test_data), 1, fp);
    fclose(fp);
    
    return 0;
}

// 发送JPEG图像给服务器
int send_jpeg_image(int sock, const char *filename) {
    // 打开文件
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("无法打开图像文件");
        return -1;
    }
    
    // 获取文件大小
    struct stat file_stat;
    if (stat(filename, &file_stat) < 0) {
        perror("无法获取文件状态");
        fclose(fp);
        return -1;
    }
    
    // 准备要发送的JSON头信息
    cJSON *header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "response", "jpeg_image");
    cJSON_AddNumberToObject(header, "timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(header, "size", (double)file_stat.st_size);
    
    char *header_str = cJSON_PrintUnformatted(header);
    
    // 发送头信息
    if (send(sock, header_str, strlen(header_str), 0) < 0) {
        perror("发送图像头信息失败");
        free(header_str);
        cJSON_Delete(header);
        fclose(fp);
        return -1;
    }
    
    // 给服务器一点时间处理头信息
    usleep(100000);  // 100ms
    
    // 读取并发送文件内容
    char buffer[4096];
    size_t bytes_read;
    size_t total_sent = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("发送图像数据失败");
            free(header_str);
            cJSON_Delete(header);
            fclose(fp);
            return -1;
        }
        total_sent += bytes_read;
    }
    
    printf("已发送图像 %s (%zu 字节)\n", filename, total_sent);
    
    free(header_str);
    cJSON_Delete(header);
    fclose(fp);
    
    return 0;
}

// 处理服务器消息的线程函数
void *server_handler(void *arg) {
    int sock = *((int *)arg);
    char buffer[BUFFER_SIZE];
    
    while (connected) {
        int bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
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
                    } else if (strcmp(command->valuestring, "get_jpeg") == 0) {
                        // 处理获取JPEG图像命令
                        printf("收到获取JPEG图像命令\n");
                        
                        // 生成临时文件名
                        char filename[64];
                        sprintf(filename, "capture_%ld.jpg", (long)time(NULL));
                        
                        // 拍照
                        if (capture_jpeg(filename) == 0) {
                            // 发送图像
                            send_jpeg_image(sock, filename);
                            
                            // 删除临时文件
                            remove(filename);
                        }
                    } else {
                        printf("未知命令: %s\n", command->valuestring);
                    }
                }
                
                cJSON_Delete(root);
            }
        } else if (bytes_read == 0) {
            printf("服务器断开连接\n");
            connected = 0;
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("接收失败");
            connected = 0;
            break;
        }
    }
    
    return NULL;
}

// 显示帮助信息
void show_help() {
    printf("\n可用命令:\n");
    printf("  connect - 自动发现并连接到服务器\n");
    printf("  connect IP [PORT] - 连接到指定IP和端口的服务器\n");
    printf("  disconnect - 断开与服务器的连接\n");
    printf("  status - 显示当前连接状态\n");
    printf("  help - 显示此帮助信息\n");
    printf("  exit - 退出程序\n");
}

// 添加线程参数结构体
typedef struct {
    char server_ip[INET_ADDRSTRLEN];
    int server_port;
} ConnectionParams;

// 连接线程函数
void *connection_thread(void *arg) {
    ConnectionParams *params = (ConnectionParams *)arg;
    char server_ip[INET_ADDRSTRLEN];
    int server_port = PORT;
    
    // 复制参数，因为我们会释放传入的结构体
    strcpy(server_ip, params->server_ip);
    server_port = params->server_port;
    free(params);
    
    printf("开始连接过程...\n");
    
    // 尝试通过广播发现服务器
    if (strlen(server_ip) == 0) {
        printf("尝试自动发现服务器...\n");
        if (discover_server(server_ip, &server_port) < 0) {
            // 如果发现失败，使用默认设置
            strcpy(server_ip, SERVER_IP);
            server_port = PORT;
            printf("自动发现失败，使用默认服务器设置: IP=%s, 端口=%d\n", server_ip, server_port);
        }
    }
    
    // 连接到服务器
    int sock = connect_to_server(server_ip, server_port);
    if (sock < 0) {
        printf("连接服务器失败\n");
        return NULL;
    }
    
    // 保存连接套接字
    server_sock = sock;
    connected = 1;
    
    // 创建处理服务器消息的线程
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, server_handler, &server_sock) != 0) {
        perror("创建服务器处理线程失败");
        close(server_sock);
        server_sock = -1;
        connected = 0;
        return NULL;
    }
    
    pthread_detach(server_thread);
    return NULL;
}

// 主函数
int main() {
    char server_ip[INET_ADDRSTRLEN];
    int server_port = PORT;
    char cmd_buffer[256];
    pthread_t server_thread;
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("客户端启动，等待命令...\n");
    show_help();
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        // 读取命令
        if (fgets(cmd_buffer, sizeof(cmd_buffer), stdin) == NULL) {
            continue;
        }
        
        // 移除换行符
        cmd_buffer[strcspn(cmd_buffer, "\n")] = 0;
        
        // 处理命令
        if (strcmp(cmd_buffer, "connect") == 0) {
            if (connected) {
                printf("已经连接到服务器\n");
                continue;
            }
            
            // 创建连接参数
            ConnectionParams *params = malloc(sizeof(ConnectionParams));
            if (!params) {
                perror("内存分配失败");
                continue;
            }
            
            // 默认使用空IP，表示自动发现
            params->server_ip[0] = '\0';
            params->server_port = PORT;
            
            // 创建连接线程
            pthread_t conn_thread;
            if (pthread_create(&conn_thread, NULL, connection_thread, params) != 0) {
                perror("创建连接线程失败");
                free(params);
                continue;
            }
            
            pthread_detach(conn_thread);
            printf("正在后台连接服务器...\n");
            
        } else if (strcmp(cmd_buffer, "connect") > 0 && strncmp(cmd_buffer, "connect ", 8) == 0) {
            // 支持手动指定服务器地址，格式: connect IP [PORT]
            if (connected) {
                printf("已经连接到服务器，请先断开连接\n");
                continue;
            }
            
            char ip_str[INET_ADDRSTRLEN] = {0};
            int port = PORT;
            
            // 解析命令行参数
            char *token = strtok(cmd_buffer + 8, " ");
            if (token) {
                strncpy(ip_str, token, INET_ADDRSTRLEN - 1);
                token = strtok(NULL, " ");
                if (token) {
                    port = atoi(token);
                    if (port <= 0 || port > 65535) {
                        printf("无效的端口号，使用默认端口 %d\n", PORT);
                        port = PORT;
                    }
                }
            }
            
            // 创建连接参数
            ConnectionParams *params = malloc(sizeof(ConnectionParams));
            if (!params) {
                perror("内存分配失败");
                continue;
            }
            
            strcpy(params->server_ip, ip_str);
            params->server_port = port;
            
            // 创建连接线程
            pthread_t conn_thread;
            if (pthread_create(&conn_thread, NULL, connection_thread, params) != 0) {
                perror("创建连接线程失败");
                free(params);
                continue;
            }
            
            pthread_detach(conn_thread);
            printf("正在后台连接到 %s:%d...\n", ip_str, port);
            
        } else if (strcmp(cmd_buffer, "disconnect") == 0) {
            if (!connected) {
                printf("未连接到服务器\n");
                continue;
            }
            
            printf("断开与服务器的连接\n");
            connected = 0;
            close(server_sock);
            server_sock = -1;
            
        } else if (strcmp(cmd_buffer, "status") == 0) {
            if (connected) {
                printf("当前已连接到服务器\n");
            } else {
                printf("当前未连接到服务器\n");
            }
        } else if (strcmp(cmd_buffer, "help") == 0) {
            show_help();
            
        } else if (strcmp(cmd_buffer, "exit") == 0) {
            printf("退出程序\n");
            if (connected) {
                connected = 0;
                close(server_sock);
            }
            break;
            
        } else if (strlen(cmd_buffer) > 0) {
            printf("未知命令: %s\n", cmd_buffer);
            show_help();
        }
    }
    
    return 0;
} 