#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h> 
#include <string.h>
#include <time.h>
#include <signal.h>
// Mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
//
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 6789
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
// Struct de truyen vao thread xu ly
typedef struct {
    int pid;
    char name[50];
    int cpu_min, cpu_max, mem_min, mem_max;
    unsigned long total_memory;
    long ticks_per_second;
} thread_arg_t;

/*lay total memory tu /proc/meminfo*/ 
unsigned long get_total_memory() {
    char line[256];
    unsigned long mem_total = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        perror("Error opening /proc/meminfo");
        return 0;
    }    
    while (fgets(line, sizeof(line), fp)) {
        /*tim toi phan MemoTotal*/
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
            break;
        }
    }
    fclose(fp);
    return mem_total;
}

/*lay data tu /proc/[pid]/stat*/
void get_process_stat(int pid, unsigned long *utime, unsigned long *stime, long *rss) {
    char path[64];
    FILE *fp;
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("PID : %d\n",pid);
        perror("Error opening /proc/[pid]/stat");
        return;
    }
    fscanf(fp, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*ld %*ld %*ld %*ld %*ld %*ld %*llu %*llu %ld",
           utime, stime, rss);
    fclose(fp);
}

// Ham kiem tra process
void* monitor_process(void *arg) {
    thread_arg_t *args = (thread_arg_t*)arg;
    if (args == NULL) {
        perror("Invalid argument passed to thread");
        return NULL;
    }

    int pid = args->pid;
    char name[50];
    strcpy(name, args->name); // Sao chép tên
    int cpu_min = args->cpu_min;
    int cpu_max = args->cpu_max;
    int mem_min = args->mem_min;
    int mem_max = args->mem_max;
    unsigned long total_memory = args->total_memory;
    long ticks_per_second = args->ticks_per_second;

    unsigned long prev_utime = 0, prev_stime = 0, utime, stime;
    long rss;
    double cpu_usage, memory_usage;

    /*time hien tai va time tiep theo vao file log*/
    time_t current_time, next_write_time;
    time(&current_time);
    next_write_time = current_time + 5; // sau 5s
    int count = 0;
    int check = 0;

    while (1) {
        count++;
        get_process_stat(pid, &utime, &stime, &rss);
        /* Bo qua cac lan doc dau do time khoi tao bang 0*/
        if (prev_utime != 0 && prev_stime != 0) {
            double delta_time = (utime + stime - prev_utime - prev_stime) / (double)ticks_per_second;
            cpu_usage = 100.0 * delta_time; 
            /*RSS*/  
            double rss_bytes = rss * getpagesize();
            /*total mem => byte*/
            double total_memory_bytes = total_memory * 1024;
            memory_usage = 100.0 * (rss_bytes / total_memory_bytes);

            time(&current_time);
            if (current_time >= next_write_time) {
                if (cpu_usage >= cpu_max || memory_usage >= mem_max) {
                    check++;  
                    printf("\n Alarm 3 time will print to the File\n");   
                    if(check >=3){
                        pthread_mutex_lock(&file_mutex);
                        printf("PID : %d saved to the file\n",pid);
                        /*check xem da co pid do trong file chua*/
                        FILE *file = fopen("process_monitor.log", "r");
                        if (file == NULL) {
                            perror("Error opening log file for reading");
                        } else {
                            char line[256];
                            int pid_found = 0;
                            while (fgets(line, sizeof(line), file)) {
                                int logged_pid;
                                if (sscanf(line, "PID: %d,", &logged_pid) == 1 && logged_pid == pid) {
                                    pid_found = 1;
                                    break;
                                }
                            }
                            fclose(file);

                            // Nếu không có
                            if (!pid_found) {
                                file = fopen("process_monitor.log", "a");
                                if (file == NULL) {
                                    perror("Error opening log file for appending");
                                } else {
                                    fprintf(file, "PID: %d, CPU Usage: %.2f%%, Memory Usage: %.2f%%, Name: %s\n", pid, cpu_usage, memory_usage, name);
                                    fclose(file);
                                }
                            }
                        }
                        // Unlock
                        pthread_mutex_unlock(&file_mutex);
                    } 
                }

                // Update the next write time
                next_write_time = current_time + 3;
            }
        }

        prev_utime = utime;
        prev_stime = stime;
        sleep(1); // 
        if (count >= 20) break;
    }
    free(args);
    return NULL;
}


void send_file_to_server(const char *file_path, const char *server_ip, int server_port) {
    int sockfd;
    struct sockaddr_in server_addr;
    Packet packet;
    FILE *file;
    int bytes_read;
    uint32_t offset = 0;

    // Tạo socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Lỗi tạo socket");
        exit(EXIT_FAILURE);
    }

    // Cấu hình địa chỉ server
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Kết nối tới server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Lỗi kết nối tới server");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

     // Gửi dòng đầu tiên là thời gian hiện tại
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char time_str[256];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S\n", timeinfo);
    if (send(sockfd, time_str, strlen(time_str), 0) < 0) 
    {
        perror("Lỗi gửi thời gian");
    }
    // Mở file để đọc
    file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Lỗi mở file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Đọc và gửi từng phần của file
    while ((bytes_read = fread(packet.data, 1, BUFFER_SIZE, file)) > 0) {
        packet.start_byte = 0x02;
        packet.message_type = 0x02; // Dữ liệu
        packet.offset = offset;
        packet.data_size = bytes_read;  // Ghi kích thước thực của dữ liệu
        packet.checksum = calculate_checksum(packet.data, bytes_read);
        packet.end_byte = 0x03;

        // Gửi gói tin tới server
        if (send(sockfd, &packet, sizeof(Packet), 0) < 0) {
            perror("Lỗi gửi dữ liệu");
            break;
        }

        offset += bytes_read;
    }

    if (ferror(file)) {
        perror("Lỗi đọc file");
    }

    fclose(file);
    close(sockfd);
    printf("Đã gửi file %s tới server\n", file_path);
}

long get_file_size(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}
void restart_process() {
    FILE *file = fopen("process_monitor.log", "r");
    if (file == NULL) {
        perror("Error opening log file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        int pid;
        if (sscanf(line, "PID: %d,", &pid) == 1) {
            char exe_path[256];
            snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);
            
            char real_exe_path[256];
            ssize_t len = readlink(exe_path, real_exe_path, sizeof(real_exe_path) - 1);
            if (len != -1) {
                real_exe_path[len] = '\0'; // Null-terminate the path

                // kill
                if (kill(pid, SIGTERM) == 0) {
                    
                    sleep(1);
                    // Restart the process 
                    printf("Restarting process PID: %d with executable: %s\n", pid, real_exe_path);
                    if (fork() == 0) { // Fork a new process
                        execl(real_exe_path, real_exe_path, (char *)NULL); 
                        perror("Error restarting process");
                        exit(1); // Exit child process if exec fails
                    }
                } else {
                    perror("Error sending SIGTERM to process");
                }
            } else {
                perror("Error reading executable path");
            }
        }
    }

    fclose(file);
}
int find_process(const char name[50]) {
    DIR *dir;
    struct dirent *ent;
    int found_pid = 0;  // Variable to store the found PID

    // Open the /proc directory
    if ((dir = opendir("/proc")) != NULL) {
        // Iterate through directory entries
        while ((ent = readdir(dir)) != NULL) {
            char path[256];
            char cmd_name[256];

            // Check if the entry name is a number (PID)
            if (isdigit(*ent->d_name)) {
                int pid = atoi(ent->d_name);
                // Create path to the /proc/[PID]/comm file
                snprintf(path, sizeof(path), "/proc/%d/comm", pid);

                // Open the /proc/[PID]/comm file
                FILE *cmd_file = fopen(path, "r");
                if (cmd_file != NULL) {
                    // Read the process name
                    if (fgets(cmd_name, sizeof(cmd_name), cmd_file) != NULL) {
                        // Remove newline character from the end
                        cmd_name[strcspn(cmd_name, "\n")] = 0;
                        // Compare the process name with the provided name
                        if (strcmp(cmd_name, name) == 0) {
                            found_pid = pid;  // Store the found PID
                            fclose(cmd_file); // Close the file
                            break;           // Exit the loop once found
                        }
                    } else {
                        perror("Lỗi khi đọc tên tiến trình");
                    }
                    fclose(cmd_file); // Ensure file is closed
                } else {
                    perror("Lỗi khi mở tệp /proc/[PID]/comm");
                }
            }
        }
        closedir(dir); // Close the directory after processing all entries
    } else {
        perror("Lỗi khi mở thư mục /proc");
    }

    return found_pid; // Return the found PID or 0 if not found
}


int main() {
    DIR *dir;
    struct dirent *ent;
    unsigned long total_memory = get_total_memory(); // KB
    long ticks_per_second = sysconf(_SC_CLK_TCK); //ticks sys /giây
   
    while (1) {
        FILE *file = fopen("process_monitor.log", "w");
        if (file != NULL) {
            fclose(file);
        }
        pthread_t threads[1024]; // Mảng lưu trữ ID luồng
        int thread_count = 0; // Đếm số lượng luồng
         // Mở file để đọc
        char line[256];   // Mảng để chứa từng dòng đọc từ file 
        FILE *file1 = fopen("input.txt", "r");
        if (file1 == NULL) {
            printf("Không thể mở file!\n");
            return 1;
        }
        while (fgets(line, sizeof(line), file1)) 
        {   
            char name[50];    // Mảng để chứa tên process
            int cpu_min, cpu_max, mem_min, mem_max;
            if (sscanf(line, "NAME: %[^,], CPU Usage: %d%%-%d%%, Memory Usage: %d%%-%d%%", 
                   name, &cpu_min, &cpu_max, &mem_min, &mem_max) != 5) 
            {
                printf("Lỗi khi đọc dòng: %s\n", line);
            }
            else
            {   
                printf("Tên: %s\n", name);
                printf("Sử dụng CPU: %d%% - %d%%\n", cpu_min, cpu_max);
                printf("Sử dụng bộ nhớ: %d%% - %d%%\n", mem_min, mem_max);
                printf("@@@@@@@!");
                if (find_process(name) == 0) {
                    if (fork() == 0) {
                        // Tiến trình con: Tạo đường dẫn đầy đủ cho chương trình
                        char program_path[256];
                        snprintf(program_path, sizeof(program_path), "./%s", name);
                        
                        // Chạy chương trình
                        execlp(program_path, program_path, NULL);
                        perror("Chạy chương trình thất bại");
                        exit(1);
                    }
                }
                int pid =find_process(name);
                printf("PID = %d",pid);
                //int pid=5440;
                printf("@@@@@@@\n");
                thread_arg_t *args = malloc(sizeof(thread_arg_t));
                if (args == NULL) 
                {
                    perror("Không thể cấp phát bộ nhớ cho args");
                    continue;
                }
                args->pid = pid;
                strcpy(args->name,name);
                args->cpu_min = cpu_min;
                args->cpu_max = cpu_max;
                args->mem_min = mem_min;
                args->mem_max = mem_max;
                args->total_memory = total_memory;
                args->ticks_per_second = ticks_per_second;
                pthread_create(&threads[thread_count], NULL, monitor_process, (void*)args);
                //printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
                thread_count++;
            }
        }
        fclose(file1);
        // Chờ tất cả các luồng hoàn thành
        for (int i = 0; i < thread_count; i++) 
        {
            pthread_join(threads[i], NULL);
        }
        printf("\n Numthread:  %d\n",thread_count);

        if(get_file_size("process_monitor.log"))
        {
            send_file_to_server("process_monitor.log",SERVER_IP, SERVER_PORT);
            restart_process();
        }
        break;
        
        sleep(1);
    }

    return 0;
}
