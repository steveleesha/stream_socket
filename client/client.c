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

void send_initial_message(int sock) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "reason", "stream_monitoring");
    cJSON_AddStringToObject(root, "rtsp_url", "rtsp://example.com/stream");
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(sock, json_str, strlen(json_str), 0);
    
    free(json_str);
    cJSON_Delete(root);
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
            cJSON *root = cJSON_Parse(buffer);
            if (root) {
                cJSON *command = cJSON_GetObjectItem(root, "command");
                cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
                /*
                char *json_str = cJSON_Print(root);
                printf("%s\n", json_str);
                free(json_str);
                */
                if (command && timestamp) {
                    printf("Received command: %s at timestamp: %.0f\n", 
                           command->valuestring, timestamp->valuedouble);
                }
                
                cJSON_Delete(root);
            }
        } else if (bytes_read == 0) {
            printf("Server disconnected\n");
            break;
        } else {
            perror("Receive failed");
            break;
        }
    }
    
    close(sock);
    return 0;
} 