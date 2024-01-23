#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/sendfile.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "helper.h"

typedef struct fisier
{
    int nr_octeti_cale;
    char cale[1024];
    int dimensiune_fisier;
    int operatie; //1 pentru read si 2 pentru write
}fisier;

int listenSocket, clientSocket;
struct sockaddr_in serverAddr, clientAddr;
struct epoll_event ev;
struct signalfd_siginfo fdsi;
struct fisier files[50];
int sfd;
int epfd;
sigset_t mask;
pthread_mutex_t active_clients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t terminatedMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t filesMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t threads[MAX_THREADS];
pthread_t signalsThread;
pthread_t mainThread;
int nr_active_clients;
int terminated;
int listening = 1;
int nr_files;

void getFiles()
{
    FILE* f = fopen("fisiere.txt", "r");
    while(!feof(f))
    {
        fgets(files[nr_files].cale, 1024, f);
        if(files[nr_files].cale[strlen(files[nr_files].cale) - 1] == '\n')
            files[nr_files].cale[strlen(files[nr_files].cale) - 1] = '\0';
        files[nr_files].nr_octeti_cale = strlen(files[nr_files].cale);
        int fd = open(files[nr_files].cale, O_RDONLY);
        struct stat status;
        fstat(fd, &status);
        files[nr_files].dimensiune_fisier = status.st_size;
        files[nr_files].operatie = 0;
        nr_files++;
        close(fd);
    }
    fclose(f);
}

void graceful_termination()
{
    for(int i = 0; i < MAX_THREADS; i++)
        pthread_join(threads[i], NULL);
    close(listenSocket);
    pthread_mutex_lock(&terminatedMutex);
    terminated = 1;
    pthread_mutex_unlock(&terminatedMutex);
    pthread_cancel(mainThread);
}

void checkSignal(int signo)
{
     if (signo == SIGINT) {
        graceful_termination();
        listening = 0;
    }

    else if (signo == SIGTERM) {
        graceful_termination();
        listening = 0;
    }
}

void* listen_for_signals_thread_function(void* arg)
{
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
    pthread_exit(NULL);
}

void* handle_connection(void* cSocket)
{
    pthread_mutex_lock(&active_clients);
    nr_active_clients++;
    pthread_mutex_unlock(&active_clients);
    int clientSocket = *(int*)cSocket;
    char buff[1024];
    memset(buff, 0, 1024);
    recv(clientSocket, buff, 1024, 0);
    printf("Client said: %s\n", buff);
    //TODO
    //procesare cereri
    char* save_ptr;
    if(buff[0] != '\n')
    {
        if(strcmp(buff, "LIST") == 0)
        {
            uint32_t nr_octeti_raspuns = 0;
            int response = RESPONSE_SUCCES;
            send(clientSocket, &response, sizeof(int), 0);
            for(int i = 0; i < nr_files; i++)
            {
                nr_octeti_raspuns += files[i].nr_octeti_cale;
            }
            send(clientSocket, &nr_octeti_raspuns, sizeof(uint32_t), 0);
            for(int i = 0; i < nr_files; i++)
            {
                send(clientSocket, files[i].cale, files[i].nr_octeti_cale, 0);
                sleep(0.1); //feeling unsafe with this shit, might delete it later
                //but if it works, it does good for now
            }
        }
        else if(strcmp(buff, "DOWNLOAD") == 0)
        {
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char buffer[1024];
            recv(clientSocket, buffer, 1024, 0);
            pthread_mutex_lock(&filesMutex);
            int fd = -1;
            for(int i = 0; i < nr_files; i++)
            {
                if(strcmp(buffer, files[i].cale) == 0)
                {
                    fd = open(buffer, O_RDONLY);
                    break;
                }
            }
            if(fd < 0)
            {
                int response = RESPONSE_FILE_NOT_FOUND;
                send(clientSocket, &response, sizeof(RESPONSE_FILE_NOT_FOUND), 0);
            }
            else
            {
                int response = RESPONSE_SUCCES;
                send(clientSocket, &response, sizeof(RESPONSE_SUCCES), 0);
                uint32_t nr_octeti_raspuns;
                struct stat status;
                fstat(fd, &status);
                nr_octeti_raspuns = status.st_size;
                send(clientSocket, &nr_octeti_raspuns, sizeof(uint32_t), 0);
                sendfile(clientSocket, fd, 0, nr_octeti_raspuns);
            }
            close(fd);
            pthread_mutex_unlock(&filesMutex);
        }
        else if(strcmp(buff, "UPLOAD") == 0)
        {
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char buffer[1024];
            recv(clientSocket, buffer, 1024, 0);
            //TODO
        }
        else
        {
            int response = RESPONSE_UNKNOWN_OPERATION;
            send(clientSocket, &response, sizeof(int), 0);
        }
    }

    pthread_mutex_lock(&active_clients);
    nr_active_clients--;
    pthread_mutex_unlock(&active_clients);
    close(clientSocket);
}

int main()
{
    getFiles();
    int clientAddrLength = sizeof(clientAddr);
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
    pthread_detach(signalsThread);
    while(!terminated)
    {
        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLength);
        if(nr_active_clients < MAX_CLIENTS)
        {
            printf("New connection from client socket!\n");
            pthread_create(&threads[nr_active_clients], NULL, handle_connection, &clientSocket);
        }
        else
        {
            int response = RESPONSE_SERVER_BUSY;
            send(clientSocket, &response, sizeof(response), 0);
            close(clientSocket);
        }
    }
    close(listenSocket);
    return 0;
}