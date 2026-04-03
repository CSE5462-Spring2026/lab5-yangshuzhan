/*Shuzhan Yang
2026/3/11*/
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include "cJSON.h"
#define MAXPEERS 50

struct FileInfo {
    char filename[100];
    char fullFileHash[65];
    int numberOfChunks;  // <-- 新增：用来存储分块数量
    char clientIP[MAXPEERS][INET_ADDRSTRLEN];
    int clientPort[MAXPEERS];
    int numberOfPeers;
    struct FileInfo *next;
};

// 全局链表头指针
struct FileInfo *head = NULL;

// 1. 查找文件节点 (保持不变)
struct FileInfo* find_file(const char* hash) {
    struct FileInfo* current = head;
    while (current != NULL) {
        if (strcmp(current->fullFileHash, hash) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// 2. 注册或更新文件信息 (新增了 chunks 参数)
void register_file(const char* filename, const char* hash, int chunks, const char* ip, int port) {
    struct FileInfo* existing_file = find_file(hash);

    if (existing_file != NULL) {
        // 去重检查
        for (int i = 0; i < existing_file->numberOfPeers; i++) {
            if (strcmp(existing_file->clientIP[i], ip) == 0 && existing_file->clientPort[i] == port) {
                return; // 已经存在，忽略
            }
        }
        // 追加客户端
        if (existing_file->numberOfPeers < MAXPEERS) {
            int index = existing_file->numberOfPeers;
            strcpy(existing_file->clientIP[index], ip);
            existing_file->clientPort[index] = port;
            existing_file->numberOfPeers++;
        }
    } else {
        // 创建新节点
        struct FileInfo* new_node = (struct FileInfo*)malloc(sizeof(struct FileInfo));
        if (new_node == NULL) return;

        strncpy(new_node->filename, filename, sizeof(new_node->filename) - 1);
        new_node->filename[sizeof(new_node->filename) - 1] = '\0';
        
        strncpy(new_node->fullFileHash, hash, sizeof(new_node->fullFileHash) - 1);
        new_node->fullFileHash[sizeof(new_node->fullFileHash) - 1] = '\0';

        new_node->numberOfChunks = chunks; // <-- 新增：保存分块数量

        strcpy(new_node->clientIP[0], ip);
        new_node->clientPort[0] = port;
        new_node->numberOfPeers = 1;

        new_node->next = head;
        head = new_node;
    }
}

// 3. 打印当前所有服务器记录 (完全按照老师的模板格式)
void print_all_files() {
    struct FileInfo* current = head;
    printf("Stored File Information:\n");
    while (current != NULL) {
        printf("Filename: %s\n", current->filename);
        printf("    Full Hash: %s\n", current->fullFileHash);
        printf("    Number of Chunks %d\n", current->numberOfChunks);
        for (int i = 0; i < current->numberOfPeers; i++) {
            printf("    Client IP: %s, Client Port: %d\n", current->clientIP[i], current->clientPort[i]);
        }
        current = current->next;
    }
}

// 4. 解析 JSON 并传参
void format_message(char *json_string, const char *client_ip, int client_port) {
    cJSON *root = cJSON_Parse(json_string); 
    if (!root) {
        printf("Error: Invalid JSON format.\n");
        return;
    }

    if (cJSON_IsArray(root)) {
        int array_size = cJSON_GetArraySize(root);

        for (int i = 0; i < array_size; i++) {
            cJSON *file_item = cJSON_GetArrayItem(root, i);
            
            cJSON *name_obj = cJSON_GetObjectItemCaseSensitive(file_item, "filename");
            cJSON *hash_obj = cJSON_GetObjectItemCaseSensitive(file_item, "fullFileHash");
            cJSON *chunks_obj = cJSON_GetObjectItemCaseSensitive(file_item, "numberOfChunks"); // <-- 提取分块数量

            // 检查三个核心字段是否存在且类型正确
            if (cJSON_IsString(name_obj) && (name_obj->valuestring != NULL) &&
                cJSON_IsString(hash_obj) && (hash_obj->valuestring != NULL) &&
                cJSON_IsNumber(chunks_obj)) { // <-- 必须是数字
                
                // 传参增加 chunks_obj->valueint
                register_file(name_obj->valuestring, hash_obj->valuestring, chunks_obj->valueint, client_ip, client_port);
            } 
        }
    } 
    else if (cJSON_IsObject(root)) {
        cJSON *name_obj = cJSON_GetObjectItemCaseSensitive(root, "filename");
        cJSON *hash_obj = cJSON_GetObjectItemCaseSensitive(root, "fullFileHash");
        cJSON *chunks_obj = cJSON_GetObjectItemCaseSensitive(root, "numberOfChunks");

        if (cJSON_IsString(name_obj) && cJSON_IsString(hash_obj) && cJSON_IsNumber(chunks_obj)) {
            register_file(name_obj->valuestring, hash_obj->valuestring, chunks_obj->valueint, client_ip, client_port);
        }
    }

    cJSON_Delete(root); 
}
int main(){
    int socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1; // 1 means on
    struct sockaddr_in src;
    socklen_t len = sizeof(src);

    struct sockaddr_in addr;
    struct ip_mreq mreq;
    memset(&addr, 0, sizeof(addr));      // initialize
    addr.sin_family = AF_INET;            // IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // address

    char mcast_ip[32];
    int port;

    printf("Enter multicast IP+port: ");
    int result = scanf("%31s%d", mcast_ip, &port);
    if (result != 2) { // handle exception
        printf("Error: Invalid input format.\n");
    } else {
        printf("Success: %s:%d\n", mcast_ip, port);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(mcast_ip); // multicast address 
    addr.sin_port = htons(port);         // port number

    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    bind(socketfd, (struct sockaddr*)&addr, sizeof(addr));
    setsockopt(socketfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); // join
    char buf[65536];  // 64 KB cache

    while (1) {
        int n = recvfrom(socketfd, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr*)&src, &len);
        if (n < 0) {
            perror("recvfrom");
            break;
        }
        // ... 在 recvfrom 之后 ...
        buf[n] = 0; 
        
        // 从 src 提取发送方的 IP 和端口
        char *client_ip = inet_ntoa(src.sin_addr);
        int client_port = ntohs(src.sin_port);
        
        printf("Received data from Client %s:%d\n", client_ip, client_port);
        
        // 把 IP 和 port 传进去
        format_message(buf, client_ip, client_port);
        
        // 处理完之后，打印一下当前的服务器状态
        print_all_files();
    }
}