#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>

#define PORT 6379
#define BACKLOG 128 // combien de connexions le noyau garde en attente pendant que ton programme est occupé ailleurs
#define TAILLE_BUFFER 4096

int main(void) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(PORT);

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

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    socklen_t addr_len = sizeof(address);

    int client_fd = accept(listen_fd, (struct sockaddr *)&address, &addr_len);
    if (client_fd == -1) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    char buffer[TAILLE_BUFFER];

    while (1) {
        ssize_t rc = read(client_fd, buffer, TAILLE_BUFFER);
        if (rc > 0) {
            printf("reçu %zd octets\n", rc);
            char *fin;
            long n = strtol(buffer + 1, &fin, 10);
            printf("%ld\n", n);

            for (int i = 0; i < n; i++) {
                char *fin3;
                long longueur = strtol(fin + 3, &fin3, 10);
                fin = fin3 + (int) longueur + 2;
                printf("%.*s\n", (int) longueur, fin3 + 2);


            }
        }
        else if (rc == 0) {
            break;
        }
        else {
            perror("read");
            break;
        }
    }

    close(listen_fd);
    close(client_fd);
    return 0;
}

