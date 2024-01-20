#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <pthread.h>
#include <signal.h>

#include "helper.h"

int listenSocket, clientSocket;
struct sockaddr_in serverAddr, clientAddr;
struct epoll_event ev;
struct signalfd_siginfo fdsi;
int sfd;
int epfd;
sigset_t mask;
pthread_mutex_t active_clients = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

pthread_t threads[MAX_THREADS];
pthread_t signalsThread;
pthread_t mainThread;
int nr_active_clients;
int terminated;

void graceful_termination()
{
    for(int i = 0; i < MAX_THREADS; i++)
        pthread_join(threads[i], NULL);
    close(listenSocket);
    pthread_cancel(mainThread);
}

void checkSignal(int signo)
{
     if (signo == SIGINT) {
        graceful_termination();
    }

    else if (signo == SIGTERM) {
        graceful_termination();
    }
}

void* listen_for_signals_thread_function(void* arg)
{
    int listening = 1;
    while(listening)
    {
        struct epoll_event ret_ev[10];
        int nr = epoll_wait(epfd, ret_ev, 10, -1);
        for(int i = 0; i < nr; i++)
        {
            if((ret_ev[i].data.fd == sfd))
            {
                ssize_t s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
                checkSignal(fdsi.ssi_signo);
                listening = 0;
            }
            else if ((ret_ev[i].data.fd == STDIN_FILENO)) 
            {
                char buf[1024];
                read(STDIN_FILENO, buf, 1024);
                printf("Server said: %s\n", buf);
                if(strcmp(buf, "quit\n") == 0)
                {
                    graceful_termination();
                    listening = 0;
                }
            }
        }
    }
}

void* handle_connection(void* cSocket)
{
    pthread_mutex_lock(&active_clients);
    nr_active_clients++;
    pthread_mutex_unlock(&active_clients);
    int* clientSocket = (int*)cSocket;
    char buff[1024];
    memset(buff, 0, 1024);
    recv(*clientSocket, buff, 1024, 0);
    printf("Client said: %s\n", buff);
    //TODO
    //procesare cereri
    pthread_mutex_lock(&active_clients);
    nr_active_clients--;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&active_clients);
    close(*clientSocket);
}

int main()
{
    mainThread = pthread_self();
    printf("PID OF SERVER IS %d\n", getpid());
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    sfd = signalfd(-1, &mask, 0);
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);
    bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 10);
    epfd = epoll_create(2);
    int flags = fcntl(epfd, F_GETFL, 0);
    fcntl(epfd, F_SETFL, flags | O_NONBLOCK);
    /*
    ev.data.fd = listenSocket;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenSocket, &ev);
    */
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);
    pthread_create(&signalsThread, NULL, listen_for_signals_thread_function, NULL);
    while(!terminated)
    {
        int clientAddrLength = sizeof(clientAddr);
        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLength);
        if(nr_active_clients < MAX_CLIENTS)
        {
            printf("New connection from client socket!\n");
            pthread_create(&threads[nr_active_clients], NULL, handle_connection, &clientSocket);
        }
        else
        {
            send(clientSocket, MAX_CLIENTS_REACHED, sizeof(MAX_CLIENTS_REACHED), 0);
        }
    }
    return 0;
}
