#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main() {
    pid_t pid, sid;

    // Fork the parent process
    pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }

    // If we got a good PID, then we can exit the parent process.
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Change file mode mask
    umask(0);

    // Create a new session ID
    sid = setsid();
    if (sid < 0) {
        perror("Failed to create new session");
        exit(EXIT_FAILURE);
    }

    // Change the current working directory
    if (chdir("/") < 0) {
        perror("Failed to change directory to /");
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Open log file for writing
    open("/var/log/my_daemon.log", O_RDWR | O_CREAT | O_APPEND, 0600);

    // Daemon-specific code here
    while (1) {
        
        // Daemon doing some work
       // sleep(30); // Sleep for 30 seconds
    }

    //exit(EXIT_SUCCESS);
}

