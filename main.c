#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/signalfd.h>

#define MAX_EVENTS 10

pthread_mutex_t mutex;
pthread_cond_t cond;
int active_connections = 0;
int server_socket;
int epoll_fd;

void *handle_connection(void *arg) {
    int client_socket = *((int *)arg);
    char buffer[1024];
    ssize_t bytes_received;

    pthread_mutex_lock(&mutex);
    active_connections++;
    pthread_mutex_unlock(&mutex);

    // Logica de tratare a conexiunii
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        // Procesarea datelor primite
        send(client_socket, buffer, bytes_received, 0);
    }

    pthread_mutex_lock(&mutex);
    active_connections--;
    pthread_cond_signal(&cond);  // Anunțăm că o conexiune s-a încheiat
    pthread_mutex_unlock(&mutex);

    // Închiderea socket-ului clientului
    close(client_socket);
    free(arg);
    pthread_exit(NULL);
}

void graceful_termination() {
    // Închiderea socket-ului serverului
    close(server_socket);

    // Așteptarea închiderii tuturor conexiunilor active
    pthread_mutex_lock(&mutex);
    while (active_connections > 0) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    // Eliberarea resurselor mutex și variabile de condiție
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    // Închiderea descriptorului epoll
    close(epoll_fd);

    exit(EXIT_SUCCESS);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct epoll_event ev, events[MAX_EVENTS];
    pthread_t thread_id;

    // Inițializarea mutex-ului și a variabilei de condiție
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // Crearea socket-ului
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    // Inițializarea structurii server_addr
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(12345);

    // Legarea (bind) socket-ului la adresa și portul specificate
    bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // Ascultarea pentru conexiuni
    listen(server_socket, 10);

    // Crearea unui descriptor epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // Adăugarea socket-ului serverului în setul epoll
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
        perror("epoll_ctl: server_socket");
        exit(EXIT_FAILURE);
    }

    // Crearea unui descriptor signalfd pentru semnalele SIGTERM și Ctrl+C
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
    int sfd = signalfd(-1, &mask, 0);
    if (sfd == -1) {
        perror("signalfd");
        exit(EXIT_FAILURE);
    }

    // Adăugarea descriptorului signalfd în setul epoll
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        perror("epoll_ctl: signalfd");
        exit(EXIT_FAILURE);
    }

    // Așteptarea evenimentelor folosind epoll
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_socket) {
                // Acceptarea unei noi conexiuni
                int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

                // Adăugarea socket-ului clientului în setul epoll
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_socket;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                    perror("epoll_ctl: client_socket");
                    exit(EXIT_FAILURE);
                }

                // Crearea unui nou thread pentru tratarea conexiunii
                int *client_sock_ptr = (int *)malloc(sizeof(int));
                *client_sock_ptr = client_socket;
                pthread_create(&thread_id, NULL, handle_connection, (void *)client_sock_ptr);
                pthread_detach(thread_id);  // Detașăm thread-ul pentru a elibera resursele atunci când se termină
            } else if (events[n].data.fd == sfd) {
                // Primirea semnalului SIGTERM sau Ctrl+C
                graceful_termination();
            } else {
                // Nu mai este nevoie să tratăm datele aici, deoarece vor fi gestionate în thread-uri separate
            }
        }
    }

    return 0;
}
