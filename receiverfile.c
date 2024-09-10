#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

#define PORT 6789
#define BUFFER_SIZE 1024

typedef struct {
    uint8_t start_byte;
    uint8_t message_type;
    uint32_t offset;
    uint32_t data_size;  // Kích thước thực sự của dữ liệu
    char data[BUFFER_SIZE];
    uint32_t checksum;
    uint8_t end_byte;
} Packet;

uint32_t calculate_checksum(char *data, int length) {
    uint32_t checksum = 0;
    for (int i = 0; i < length; i++) {
        checksum += (uint8_t)data[i];
    }
    return checksum;
}

void save_received_file(int client_socket) {
    Packet packet;
    int bytes_received;
    FILE *received_file = NULL;
    char file_name[256];
    uint32_t expected_offset = 0;

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

    // Nhận các packet từ client
    while ((bytes_received = recv(client_socket, &packet, sizeof(Packet), 0)) > 0) {
        // Kiểm tra Start byte và End byte
        if (packet.start_byte != 0x02 || packet.end_byte != 0x03) {
            printf("Lỗi bản tin: Start/End byte không hợp lệ\n");
            continue;
        }

        // Kiểm tra checksum
        uint32_t received_checksum = packet.checksum;
        uint32_t calculated_checksum = calculate_checksum(packet.data, packet.data_size);
        if (received_checksum != calculated_checksum) {
            printf("Lỗi: Checksum không khớp\n");
            continue;
        }

        // Kiểm tra offset
        if (packet.offset != expected_offset) {
            printf("Lỗi: Offset không đúng (expected: %d, received: %d)\n", expected_offset, packet.offset);
            continue;
        }

        // Ghi dữ liệu vào file (chỉ ghi đúng số lượng byte nhận được)
        fwrite(packet.data, sizeof(char), packet.data_size, received_file);
        expected_offset += packet.data_size;
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
