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
    int operatie;
}fisier;

char* pool_fisiere;
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

void log_operation(char* operation)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char* saveptr;
    char* token = __strtok_r(operation, " ", &saveptr);
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0777);
    int year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;
    int day = tm.tm_mday;
    int hour = tm.tm_hour;
    char delimiter = ',';
    char date_delimiter = ':';
    char new_line = '\n';
    char time_info[1024];
    sprintf(time_info, "%d-%d-%d, %d, ", day, month, year, hour);
    write(fd, time_info, strlen(time_info));
    while(token != NULL)
    {
        write(fd, token, strlen(token));
        token = __strtok_r(NULL, " ", &saveptr);
        if(token != NULL)
            write(fd, &delimiter, sizeof(char));
    }
    write(fd, &new_line, sizeof(char));
    close(fd);
}

void getFiles()
{
    FILE* f = fopen(pool_fisiere, "r");
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
    char* save_ptr;
    if(buff[0] != '\0')
    {
        int ack = ACK_OK;
        send(clientSocket, &ack, sizeof(int), 0);
        if(strcmp(buff, "LIST") == 0)
        {
            char logged_operation[1024];
            strcat(logged_operation, buff);
            log_operation(logged_operation);
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
            char logged_operation[1024];
            strcat(logged_operation, buff);
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char buffer[1024];
            recv(clientSocket, buffer, 1024, 0);
            strcat(logged_operation, " ");
            strcat(logged_operation, buffer);
            log_operation(logged_operation);
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
            char logged_operation[1024];
            strcat(logged_operation, buff);
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char cale[1024];
            recv(clientSocket, cale, 1024, 0);
            strcat(logged_operation, cale);
            log_operation(logged_operation);
            int nr_octeti_continut;
            recv(clientSocket, &nr_octeti_continut, sizeof(int), 0);
            char* content = (char*)malloc((nr_octeti_continut+1) * sizeof(char));
            recv(clientSocket, content, nr_octeti_continut, 0);
            int fd = -1;
            pthread_mutex_lock(&filesMutex);
            fd = open(cale, O_WRONLY);
            if(fd == -1)
            {
                //TODO de facut sa fie thread-safe
                char* copy = strdup(cale);
                char* lastSlash = strrchr(copy, '/');
                if(lastSlash !=  NULL)
                {
                    *lastSlash = '\0';
                    char* token = strtok(copy, "/");
                    char cale_construita[1024] = "";
                    while(token)
                    {
                        strcat(cale_construita, token);
                        strcat(cale_construita, "/");
                        mkdir(cale_construita, 0777);
                        token = strtok(NULL, "/");
                    }
                    free(copy);
                }
                fd = open(cale, O_WRONLY | O_CREAT, 0644);
                write(fd, content, nr_octeti_continut);
                close(fd);
                int response = RESPONSE_SUCCES;
                send(clientSocket, &response, sizeof(int), 0);
                nr_files++;
                fd = open(pool_fisiere, O_WRONLY | O_APPEND);
                strcpy(cale + 1, cale);
                cale[0] = '\n';
                write(fd, cale, nr_octeti_cale + 1);
                fsync(fd);
                close(fd);
                strcpy(files[nr_files - 1].cale, cale + 1);
                files[nr_files - 1].dimensiune_fisier = nr_octeti_continut;
                files[nr_files - 1].nr_octeti_cale = nr_octeti_cale;
                files[nr_files - 1].operatie = 0;
            }
            else
            {
                int response = RESPONSE_PERMISSION_DENIED;
                send(clientSocket, &response, sizeof(int), 0);
            }
            pthread_mutex_unlock(&filesMutex);
            free(content);
        }
        else if(strcmp(buff, "DELETE") == 0)
        {
            char logged_operation[1024];
            strcat(logged_operation, buff);
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char buffer[1024];
            recv(clientSocket, buffer, 1024, 0);
            strcat(logged_operation, " ");
            strcat(logged_operation, buffer);
            log_operation(logged_operation);
            int fd = -1;
            pthread_mutex_lock(&filesMutex);
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
                for(int i = 0; i < nr_files; i++)
                {
                    if(strcmp(files[i].cale, buffer) == 0)
                    {
                        for(int j = i; j < nr_files - 1; j++)
                        {
                            files[j] = files[j + 1];
                        }
                        nr_files--;
                        break;
                    }
                }
                int update = open(pool_fisiere, O_WRONLY | O_TRUNC);
                for(int i = 0; i < nr_files; i++)
                {
                    if(i != nr_files - 1)
                    {
                        write(update, files[i].cale, files[i].nr_octeti_cale);
                        write(update, "\n", 1);
                    }
                    else write(update, files[i].cale, files[i].nr_octeti_cale);
                }
                close(update);
                remove(buffer);
                unlink(buffer);
                close(fd);
            }
            pthread_mutex_unlock(&filesMutex);
        }
        else if(strcmp(buff, "MOVE") == 0)
        {
            char logged_operation[1024];
            strcat(logged_operation, buff);
            int nr_octeti_cale_sursa;
            recv(clientSocket, &nr_octeti_cale_sursa, sizeof(int), 0);
            char cale_sursa[1024], cale_destinatie[1024];
            recv(clientSocket, cale_sursa, nr_octeti_cale_sursa, 0);
            strcat(logged_operation, " ");
            strcat(logged_operation, cale_sursa);
            log_operation(logged_operation);
            int nr_octeti_cale_destinatie;
            recv(clientSocket, &nr_octeti_cale_destinatie, sizeof(int), 0);
            recv(clientSocket, cale_destinatie, nr_octeti_cale_destinatie, 0);
            int fd = -1;
            pthread_mutex_lock(&filesMutex);
            for(int i = 0; i < nr_files; i++)
            {
                if(strcmp(cale_sursa, files[i].cale) == 0)
                {
                    fd = open(cale_sursa, O_RDONLY);
                    break;
                }
            }
            if(fd == -1)
            {
                int response = RESPONSE_FILE_NOT_FOUND;
                send(clientSocket, &response, sizeof(RESPONSE_FILE_NOT_FOUND), 0);
            }
            else
            {
                char* copy = strdup(cale_destinatie);
                char* lastSlash = strrchr(copy, '/');
                if(lastSlash !=  NULL)
                {
                    *lastSlash = '\0';
                    char* token = strtok(copy, "/");//TODO sa fie thread-safe
                    char cale_construita[1024] = "";
                    while(token)
                    {
                        strcat(cale_construita, token);
                        strcat(cale_construita, "/");
                        mkdir(cale_construita, 0777);
                        token = strtok(NULL, "/");
                    }
                    free(copy);
                }
                int fd_move = open(cale_destinatie, O_WRONLY);
                if(fd_move != -1)
                {
                    int response = RESPONSE_PERMISSION_DENIED;
                    send(clientSocket, &response, sizeof(int), 0);
                }
                else
                {
                    int response = RESPONSE_SUCCES;
                    send(clientSocket, &response, sizeof(int), 0);
                    close(fd_move);
                    fd_move = open(cale_destinatie, O_WRONLY | O_CREAT, 0777);
                    struct stat status;
                    fstat(fd, &status);
                    sendfile(fd_move, fd, 0, status.st_size);
                    close(fd_move);
                    remove(cale_sursa);
                    unlink(cale_sursa);
                    for(int i = 0; i < nr_files; i++)
                    {
                        if(strcmp(files[i].cale, cale_sursa) == 0)
                        {
                            memset(files[i].cale, 0, files[i].nr_octeti_cale);
                            strcpy(files[i].cale, cale_destinatie);
                            files[i].nr_octeti_cale = nr_octeti_cale_destinatie;
                            break;
                        }
                    }
                    int update = open(pool_fisiere, O_WRONLY | O_TRUNC);
                    for(int i = 0; i < nr_files; i++)
                    {
                        if(i != nr_files - 1)
                        {
                            write(update, files[i].cale, files[i].nr_octeti_cale);
                            write(update, "\n", 1);
                        }
                        else write(update, files[i].cale, files[i].nr_octeti_cale);
                    }
                    close(update);
                }

            }
            pthread_mutex_unlock(&filesMutex);
        }
        else if(strcmp(buff, "UPDATE") == 0)
        {
            char logged_operation[1024];
            strcat(logged_operation, buff);
            int nr_octeti_cale;
            recv(clientSocket, &nr_octeti_cale, sizeof(int), 0);
            char cale[1024];
            recv(clientSocket, cale, nr_octeti_cale, 0);
            strcat(logged_operation, " ");
            strcat(logged_operation, cale);
            log_operation(logged_operation);
            int octet_start;
            recv(clientSocket, &octet_start, sizeof(int), 0);
            int dimensiune;
            recv(clientSocket, &dimensiune, sizeof(int), 0);
            char caractere_noi[1024];
            recv(clientSocket, caractere_noi, dimensiune, 0);
            int fd = -1;
            pthread_mutex_lock(&filesMutex);
            for(int i = 0; i < nr_files; i++)
            {
                if(strcmp(cale, files[i].cale) == 0)
                {
                    fd = open(cale, O_WRONLY);
                    break;
                }
            }
            if(fd == -1)
            {
                int response = RESPONSE_FILE_NOT_FOUND;
                send(clientSocket, &response, sizeof(RESPONSE_FILE_NOT_FOUND), 0);
            }
            else
            {
                lseek(fd, octet_start, SEEK_SET);
                write(fd, caractere_noi, dimensiune);
                close(fd);
                int response = RESPONSE_SUCCES;
                send(clientSocket, &response, sizeof(RESPONSE_SUCCES), 0);
            }
            pthread_mutex_unlock(&filesMutex);
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

int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        perror("Dati fisierul cu caile disponibile catre clienti!\n");
        exit(1);
    }
    pool_fisiere = argv[1];
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
    serverAddr.sin_port = htons(PORT);
    bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 10);
    epfd = epoll_create(2);
    int flags = fcntl(epfd, F_GETFL, 0);
    fcntl(epfd, F_SETFL, flags | O_NONBLOCK);
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
            int ack = ACK;
            send(clientSocket, &ack, sizeof(int), 0);
            int response = RESPONSE_SERVER_BUSY;
            send(clientSocket, &response, sizeof(response), 0);
            close(clientSocket);
        }
    }
    close(listenSocket);
    return 0;
}