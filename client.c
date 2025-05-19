#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>  // Added for getaddrinfo
#include <curl/curl.h>  // Added for fetching GitHub file
#include <time.h>   // Added for time() function

// Default connection settings
#define DEFAULT_LAN_HOST "127.0.0.1"
#define DEFAULT_LAN_PORT 8080
#define NGROK_INFO_FILE "ngrok_info.txt"
#define GITHUB_REPO "taotentanyb/finalnp2025"
#define GITHUB_RAW_URL "https://raw.githubusercontent.com/taotentanyb/finalnp2025/main/ngrok_info.txt"
#define BUFFER_SIZE 1024

// Global socket for SIGINT handler
int global_sock = -1;

// Signal handler for clean disconnection
void handle_signal(int sig) {
    if (global_sock != -1) {
        printf("\nNgắt kết nối từ máy chủ...\n");
        close(global_sock);
    }
    exit(0);
}

// Print help message
void print_help() {
    printf("\n--- Trợ giúp client Tic-Tac-Toe ---\n");
    printf("Trong lượt của bạn:\n");
    printf("  1-9: Đặt ký hiệu của bạn vào vị trí tương ứng\n");
    printf("  chat <tin nhắn>: Gửi tin nhắn cho đối thủ\n");
    printf("  quit: Ngắt kết nối khỏi trò chơi\n");
    printf("\nKhi bạn là người xem:\n");
    printf("  refresh: Cập nhật danh sách trò chơi đang diễn ra\n");
    printf("  <số>: Chọn trò chơi để xem\n");
    printf("  quit: Ngắt kết nối khỏi máy chủ\n");
    printf("-----------------------------\n\n");
}

// Function to download ngrok info from GitHub
int download_ngrok_info(char *host, int *port) {
    CURL *curl;
    FILE *fp;
    CURLcode res;
    char temp_file[] = "temp_ngrok_info.txt";
    int success = 0;

    curl = curl_easy_init();
    if (curl) {
        fp = fopen(temp_file, "w");
        if (fp) {
            // Add no-cache headers to bypass GitHub caching
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Cache-Control: no-cache, no-store");
            headers = curl_slist_append(headers, "Pragma: no-cache");
            
            curl_easy_setopt(curl, CURLOPT_URL, GITHUB_RAW_URL);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L); // Force a new connection
            curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);   // Prevent connection reuse
            
            // Add random query parameter to bypass caching
            char random_url[512];
            snprintf(random_url, sizeof(random_url), "%s?nocache=%ld", GITHUB_RAW_URL, time(NULL));
            curl_easy_setopt(curl, CURLOPT_URL, random_url);
            
            printf("Đang tải thông tin ngrok từ GitHub (%s)...\n", random_url);
            res = curl_easy_perform(curl);
            fclose(fp);
            
            curl_slist_free_all(headers);
            
            if (res == CURLE_OK) {
                FILE *info_file = fopen(temp_file, "r");
                if (info_file) {
                    if (fscanf(info_file, "%s %d", host, port) == 2) {
                        printf("Đã tải thông tin ngrok: %s:%d\n", host, *port);
                        success = 1;
                    } else {
                        printf("Lỗi định dạng file ngrok từ GitHub\n");
                    }
                    fclose(info_file);
                }
            } else {
                printf("Lỗi tải thông tin ngrok: %s\n", curl_easy_strerror(res));
            }
        }
        curl_easy_cleanup(curl);
    }
    
    // Make sure to remove temp file
    if (remove(temp_file) != 0) {
        printf("Cảnh báo: Không thể xóa file tạm %s\n", temp_file);
    }
    
    return success;
}

// Function to read local ngrok info
int read_local_ngrok_info(char *host, int *port) {
    FILE *file = fopen(NGROK_INFO_FILE, "r");
    if (file) {
        if (fscanf(file, "%s %d", host, port) == 2) {
            fclose(file);
            printf("Đã đọc thông tin ngrok từ tệp cục bộ: %s:%d\n", host, *port);
            return 1;
        }
        fclose(file);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char server_response[BUFFER_SIZE] = {0};
    char user_input[BUFFER_SIZE] = {0};
    ssize_t bytes_received;
    
    // Initialize connection variables
    char host[128] = DEFAULT_LAN_HOST;
    int port = DEFAULT_LAN_PORT;
    int connection_mode = -1;
    
    // Set up signal handler for clean disconnection
    signal(SIGINT, handle_signal);

    // Initialize curl for potential GitHub downloads
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Prompt user for connection type
    while (connection_mode == -1) {
        printf("Chọn loại kết nối:\n");
        printf("1. Kết nối LAN (cùng mạng local)\n");
        printf("2. Kết nối WAN (qua Internet)\n");
        printf("Lựa chọn của bạn (1/2): ");
        
        if (fgets(user_input, BUFFER_SIZE, stdin) == NULL) {
            printf("Lỗi đọc đầu vào, thoát chương trình.\n");
            return -1;
        }
        
        // Remove newline
        user_input[strcspn(user_input, "\n")] = 0;
        
        if (strcmp(user_input, "1") == 0) {
            connection_mode = 1; // LAN
            printf("Đã chọn kết nối LAN\n");
        } else if (strcmp(user_input, "2") == 0) {
            connection_mode = 2; // WAN
            printf("Đã chọn kết nối WAN\n");
            
            // Try to get ngrok info - first from GitHub, then from local file
            printf("Đang thử tải thông tin ngrok từ GitHub...\n");
            if (!download_ngrok_info(host, &port)) {
                printf("Không thể lấy thông tin ngrok từ GitHub, thử đọc file cục bộ...\n");
                if (!read_local_ngrok_info(host, &port)) {
                    printf("Không tìm thấy thông tin ngrok cục bộ.\n");
                    
                    // Prompt for manual entry
                    printf("Nhập host ngrok (ví dụ: 0.tcp.ap.ngrok.io): ");
                    if (fgets(host, sizeof(host), stdin) == NULL) {
                        printf("Lỗi đọc đầu vào, thoát chương trình.\n");
                        curl_global_cleanup();
                        return -1;
                    }
                    host[strcspn(host, "\n")] = 0;
                    
                    printf("Nhập port ngrok: ");
                    char port_str[10];
                    if (fgets(port_str, sizeof(port_str), stdin) == NULL) {
                        printf("Lỗi đọc đầu vào, thoát chương trình.\n");
                        curl_global_cleanup();
                        return -1;
                    }
                    port = atoi(port_str);
                }
            }
        } else {
            printf("Lựa chọn không hợp lệ, vui lòng nhập lại.\n");
        }
    }

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nLỗi tạo socket\n");
        curl_global_cleanup();
        return -1;
    }
    
    // Store socket in global variable for signal handler
    global_sock = sock;

    // Set up server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Connection based on mode
    if (connection_mode == 1) { // LAN
        printf("Đang kết nối qua LAN đến %s:%d...\n", host, port);
        
        if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
            printf("\nĐịa chỉ không hợp lệ hoặc không được hỗ trợ\n");
            close(sock);
            curl_global_cleanup();
            return -1;
        }
    } else { // WAN
        printf("Đang kết nối qua WAN đến %s:%d...\n", host, port);
        
        // Use DNS resolution for hostname
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        printf("Đang phân giải tên miền %s...\n", host);
        int status = getaddrinfo(host, NULL, &hints, &res);
        if (status != 0) {
            printf("Lỗi phân giải tên miền: %s\n", gai_strerror(status));
            close(sock);
            curl_global_cleanup();
            return -1;
        }

        // Copy the resolved IP address
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        serv_addr.sin_addr = addr->sin_addr;
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(addr->sin_addr), ip_str, INET_ADDRSTRLEN);
        printf("Đã phân giải %s thành IP: %s\n", host, ip_str);
        
        freeaddrinfo(res);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Kết nối thất bại");
        close(sock);
        curl_global_cleanup();
        return -1;
    }
    printf("Đã kết nối đến máy chủ.\n\n");
    printf("Nhập 'help' bất kỳ lúc nào để xem hướng dẫn.\n");

    // Game loop
    while (1) {
        memset(server_response, 0, BUFFER_SIZE);
        bytes_received = recv(sock, server_response, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Máy chủ đã đóng kết nối.\n");
            } else {
                perror("Lỗi nhận dữ liệu");
            }
            break;
        }
        server_response[bytes_received] = '\0';
        printf("%s", server_response); // Server messages include newlines

        // Check for game state cues from the server
        if (strstr(server_response, "YOUR_TURN") || 
            strstr(server_response, "Enter game number to spectate") ||
            strstr(server_response, "WELCOME: Enter 'play' to join")) {
            
            // The prompt is part of the message from server
            printf("> "); // Simple prompt indicator
            if (fgets(user_input, BUFFER_SIZE, stdin) == NULL) {
                printf("Lỗi đọc đầu vào hoặc EOF, thoát chương trình.\n");
                break;
            }
            
            // Check for special client-side commands
            if (strncmp(user_input, "help", 4) == 0) {
                print_help();
                printf("> "); // Re-prompt after help
                if (fgets(user_input, BUFFER_SIZE, stdin) == NULL) {
                    break;
                }
            } else if (strncmp(user_input, "quit", 4) == 0) {
                printf("Đang ngắt kết nối từ máy chủ...\n");
                break;
            }
            
            // fgets keeps the newline, server-side strcspn will remove it.
            // Or remove it here: user_input[strcspn(user_input, "\n")] = 0;
            
            if (send(sock, user_input, strlen(user_input), 0) < 0) {
                perror("Gửi dữ liệu thất bại");
                break;
            }
        } else if (strstr(server_response, "GAME_OVER:")) {
            printf("Trò chơi đã kết thúc. Cảm ơn bạn đã tham gia!\n");
            printf("Ngắt kết nối sau 5 giây...\n");
            sleep(5); // Give time to read final stats
            break;
        }
        // Other messages like BOARD:, CHAT_MSG:, WAIT:, INFO:, INVALID_MOVE:, INVALID_INPUT:, SPECTATE:
        // are just displayed, and the client waits for the next server instruction or game update.
    }

    printf("Kết nối đã đóng.\n");
    close(sock);
    global_sock = -1;
    curl_global_cleanup();
    return 0;
}
