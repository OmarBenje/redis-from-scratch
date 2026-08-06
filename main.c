#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0); // ou 0 = protocole par defaut, donc TCP
    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }
    return 0;
}

