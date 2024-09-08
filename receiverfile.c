#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 6789
#define BUFFER_SIZE 1024

void save_received_file(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_received;
    FILE *received_file;
    char file_name[256];
    
    // Nhận tên file từ client
    bytes_received = recv(client_socket, file_name, sizeof(file_name), 0);
    if (bytes_received <= 0) {
        perror("Lỗi nhận tên file");
        return;
    }
    file_name[bytes_received] = '\0'; // Kết thúc chuỗi
    
    // Tạo file mới để lưu dữ liệu
    received_file = fopen(file_name, "wb");
    if (received_file == NULL) {
        perror("Lỗi mở file để ghi");
        return;
    }

    // Nhận dữ liệu file từ client và ghi vào file
    while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, sizeof(char), bytes_received, received_file);
    }

    if (bytes_received < 0) {
        perror("Lỗi khi nhận dữ liệu từ client");
    }

    printf("Đã nhận file %s thành công\n", file_name);
    fclose(received_file);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Tạo socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Lỗi tạo socket");
        exit(EXIT_FAILURE);
    }

    // Cấu hình địa chỉ server
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Gán địa chỉ cho socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Lỗi bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Lắng nghe kết nối
    if (listen(server_socket, 5) == -1) {
        perror("Lỗi listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server đang lắng nghe trên cổng %d...\n", PORT);

    while (1) {
        // Chấp nhận kết nối từ client
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket == -1) {
            perror("Lỗi accept");
            continue;
        }

        printf("Kết nối từ %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Nhận và lưu file từ client
        save_received_file(client_socket);

        // Đóng kết nối với client
        close(client_socket);
    }

    // Đóng socket server (nếu thoát khỏi vòng lặp)
    close(server_socket);
    return 0;
}
