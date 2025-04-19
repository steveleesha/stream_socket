#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "cJSON.h"

#define PORT 5566
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define COMMAND_INTERVAL 5  // Send command every 5 seconds

typedef struct {
    int socket;
    char rtsp_url[256];
    char reason[256];
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;

void send_command(int client_socket) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", "check_status");
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    char *json_str = cJSON_PrintUnformatted(root);
    send(client_socket, json_str, strlen(json_str), 0);
    
    free(json_str);
    cJSON_Delete(root);
}

void handle_client_message(int client_socket, char *buffer) {
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
        char *json_str = cJSON_Print(root);
        printf("%s\n", json_str);
        free(json_str);

        cJSON *reason = cJSON_GetObjectItem(root, "reason");
        cJSON *rtsp_url = cJSON_GetObjectItem(root, "rtsp_url");
        
        if (reason && rtsp_url) {
            // Update client info
            for (int i = 0; i < client_count; i++) {
                if (clients[i].socket == client_socket) {
                    strcpy(clients[i].reason, reason->valuestring);
                    strcpy(clients[i].rtsp_url, rtsp_url->valuestring);
                    printf("Updated client %d info:\n", i);
                    printf("Reason: %s\n", clients[i].reason);
                    printf("RTSP URL: %s\n", clients[i].rtsp_url);
                    break;
                }
            }
        }
        cJSON_Delete(root);
    }
}

int main() {
    int server_socket;
    struct sockaddr_in server_addr;
    fd_set readfds;
    int max_sd;
    struct timeval tv;
    
    // Create socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        return 1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 3) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("Server listening on port %d...\n", PORT);
    
    // Initialize client array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = 0;
    }
    
    while (1) {
        // Clear the socket set
        FD_ZERO(&readfds);
        
        // Add server socket to set
        FD_SET(server_socket, &readfds);
        max_sd = server_socket;
        
        // Add child sockets to set
        for (int i = 0; i < client_count; i++) {
            int sd = clients[i].socket;
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }
        
        // Set timeout
        tv.tv_sec = COMMAND_INTERVAL;
        tv.tv_usec = 0;
        
        // Wait for activity
        int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0) {
            perror("Select error");
            continue;
        }
        
        // If something happened on the server socket, it's a new connection
        if (FD_ISSET(server_socket, &readfds)) {
            int new_socket;
            struct sockaddr_in address;
            int addrlen = sizeof(address);
            
            if ((new_socket = accept(server_socket, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept failed");
                continue;
            }
            
            printf("New connection from %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            
            // Add new socket to array of clients
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket == 0) {
                    clients[i].socket = new_socket;
                    client_count++;
                    break;
                }
            }
        }
        
        // Check for data from clients
        for (int i = 0; i < client_count; i++) {
            int sd = clients[i].socket;
            if (FD_ISSET(sd, &readfds)) {
                char buffer[BUFFER_SIZE] = {0};
                int bytes_read = recv(sd, buffer, BUFFER_SIZE, 0);
                
                if (bytes_read <= 0) {
                    // Client disconnected
                    printf("Client disconnected\n");
                    close(sd);
                    clients[i].socket = 0;
                    client_count--;
                } else {
                    handle_client_message(sd, buffer);
                }
            }
        }
        
        // Send periodic commands to all connected clients
        for (int i = 0; i < client_count; i++) {
            if (clients[i].socket != 0) {
                send_command(clients[i].socket);
            }
        }
    }
    
    close(server_socket);
    return 0;
} 