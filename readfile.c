#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE *file;
    char line[256];   // Mảng để chứa từng dòng đọc từ file
    char name[50];    // Mảng để chứa tên process
    int cpu_min, cpu_max, mem_min, mem_max;

    // Mở file để đọc
    file = fopen("input.txt", "r");
    if (file == NULL) {
        printf("Không thể mở file!\n");
        return 1;
    }

    // Đọc từng dòng từ file
    while (fgets(line, sizeof(line), file)) {
        // Sử dụng sscanf để lấy các giá trị từ dòng đọc được
        if (sscanf(line, "NAME: %[^,], CPU Usage: %d%%-%d%%, Memory Usage: %d%%-%d%%", 
                   name, &cpu_min, &cpu_max, &mem_min, &mem_max) == 5) {
            // In ra các giá trị đã lấy được
            printf("Tên: %s\n", name);
            printf("Sử dụng CPU: %d%% - %d%%\n", cpu_min, cpu_max);
            printf("Sử dụng bộ nhớ: %d%% - %d%%\n", mem_min, mem_max);
        } else {
            printf("Lỗi khi đọc dòng: %s\n", line);
        }
    }

    // Đóng file
    fclose(file);

    return 0;
}
