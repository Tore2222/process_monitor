#include <stdio.h>
#include <unistd.h>
int main(){
    pid_t pid=getpid();
    printf("pid :%d",pid);
    while(1);
}