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
// Mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

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

// Struct de truyen vao thread xu ly
typedef struct {
    int pid;
    unsigned long total_memory;
    long ticks_per_second;
} thread_arg_t;

// Ham kiem tra process
void* monitor_process(void *arg) {
    thread_arg_t *args = (thread_arg_t*)arg;
    if (args == NULL) {
        perror("Invalid argument passed to thread");
        return NULL;
    }

    int pid = args->pid;
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
                if (cpu_usage >= 90.0 || memory_usage >= 20.0) {
                    /*lock file*/
                    pthread_mutex_lock(&file_mutex);

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
                                fprintf(file, "PID: %d, CPU Usage: %.2f%%, Memory Usage: %.2f%%\n", pid, cpu_usage, memory_usage);
                                fclose(file);
                            }
                        }
                    }
                    // Unlock
                    pthread_mutex_unlock(&file_mutex);
                }

                // Update the next write time
                next_write_time = current_time + 5;
            }
        }

        prev_utime = utime;
        prev_stime = stime;

        sleep(1); // 
        if (count >= 20) break;
    }

    return NULL;
}
void send_file_to_server(const char *file_path) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];
    FILE *file;

    // Tạo socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Không thể tạo socket");
        return;
    }

    // Cấu hình server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6789);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Kết nối đến server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Không thể kết nối đến server");
        close(sockfd);
        return;
    }

    // Mở file và gửi nội dung
    file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Không thể mở file để gửi");
        close(sockfd);
        return;
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
    sleep(2);
    // Gửi nội dung file
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sockfd, buffer, bytes_read, 0) < 0) {
            perror("Lỗi gửi dữ liệu file");
            break;
        }
    }

    fclose(file);
    close(sockfd);
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

        if ((dir = opendir("/proc")) != NULL) 
        {
            while ((ent = readdir(dir)) != NULL) 
            {
                if (isdigit(*ent->d_name)) 
                {
                    int pid = atoi(ent->d_name);

                    thread_arg_t *args = malloc(sizeof(thread_arg_t));
                    if (args == NULL) 
                    {
                        perror("Không thể cấp phát bộ nhớ cho args");
                        continue;
                    }
                    args->pid = pid;
                    args->total_memory = total_memory;
                    args->ticks_per_second = ticks_per_second;

                    // Tạo luồng và truyền bản sao args
                    pthread_create(&threads[thread_count], NULL, monitor_process, (void*)args);
                    //printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
                    thread_count++;
                }
               
            }
             printf("############################");
            closedir(dir);
        } else 
        {
            perror("Lỗi khi mở thư mục /proc");
            return 1;
        }

        // Chờ tất cả các luồng hoàn thành
        for (int i = 0; i < thread_count; i++) 
        {
            pthread_join(threads[i], NULL);
        }
        printf("@@@@@@@@@@@@@@@@@@@@@@@@@ %d",thread_count);

        if(get_file_size("process_monitor.log"))
        {
            send_file_to_server("process_monitor.log");
            restart_process();
        }
        
        
        sleep(1);
    }

    return 0;
}
