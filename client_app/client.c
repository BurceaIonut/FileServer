#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#define PORT 8080
 
int main(int argc, char const* argv[])
{
    int status, valread, client_fd;
    struct sockaddr_in serv_addr;
    char request[1024];
    char buffer[1024] = { 0 };
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
 
    if ((status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    fgets(request, 1024, stdin);
    if(request[0] != '\n')
    {
        char* args = strtok(request, " \n;");
        send(client_fd, args, 1024, 0);
        int ack;
        recv(client_fd, &ack, sizeof(int), 0);
        if(ack == 0xFF)
        {
            recv(client_fd, &ack, sizeof(int), 0);
            printf("0x%x\n", ack);
        }
        else
        {
            if(strcmp(args, "LIST") == 0)
            {
                int val;
                uint32_t nr;
                valread = recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
                if(val == 0)
                {
                    valread = recv(client_fd, &nr, sizeof(uint32_t), 0);
                    printf("%d\n", nr);
                    while(nr>0)
                    {
                        int nr_biti = recv(client_fd, buffer, nr, 0);
                        printf("%s \n", buffer);
                        nr = nr - nr_biti;
                        memset(buffer, 0, 1024);
                        if(nr_biti == 0)
                            break;
                    }
                }
            }
            else if(strcmp(args, "DOWNLOAD") == 0)
            {
                args = strtok(NULL, " \n;");
                char* filename = (char*)malloc((strlen(args) + 1)*sizeof(char));
                memcpy(filename, args, strlen(args));
                filename[strlen(args)]= '\0';
                char* tokenized_filename = strrchr(filename, '/');
                if (tokenized_filename != NULL) {
                    tokenized_filename++;
                } else {
                    tokenized_filename = filename;
                }
                int nr_octeti_cale = strlen(args);
                send(client_fd, &nr_octeti_cale, sizeof(int), 0);
                send(client_fd, args, 1024, 0);
                int val;
                uint32_t nr;
                valread = recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
                if(val == 0)
                {
                    valread = recv(client_fd, &nr, sizeof(uint32_t), 0);
                    printf("%d\n", nr);
                    char* content = (char*)malloc(nr * sizeof(char));
                    recv(client_fd, content, nr, 0);
                    printf("%s\n", content);
                    int fd = open(tokenized_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    write(fd, content, nr);
                    close(fd);
                    free(content);
                }
                free(filename);
            }
            else if(strcmp(args, "UPLOAD") == 0)
            {
                //de verificat daca clientul trimite un fisier existent pe statia lui
                args = strtok(NULL, " \n;");
                char* filename = (char*)malloc((strlen(args) + 1)*sizeof(char));
                memcpy(filename, args, strlen(args));
                filename[strlen(args)]= '\0';
                int nr_octeti_cale = strlen(args);
                send(client_fd, &nr_octeti_cale, sizeof(int), 0);
                send(client_fd, args, 1024, 0);
                int fd = open(args, O_RDONLY);
                struct stat status;
                fstat(fd, &status);
                uint32_t nr_octeti_continut = status.st_size;
                send(client_fd, &nr_octeti_continut, sizeof(uint32_t), 0);
                sendfile(client_fd, fd, 0, nr_octeti_continut);
                int response;
                recv(client_fd, &response, sizeof(int), 0);
                printf("0x%x\n", response);
            }
            else if(strcmp(args, "DELETE") == 0)
            {
                args = strtok(NULL, " \n;");
                int nr_octeti_cale = strlen(args);
                send(client_fd, &nr_octeti_cale, sizeof(int), 0);
                send(client_fd, args, 1024, 0);
                int val;
                valread = recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
            }
            else if(strcmp(args, "MOVE") == 0)
            {
                args = strtok(NULL, " \n;");
                int nr_octeti_cale_sursa = strlen(args);
                send(client_fd, &nr_octeti_cale_sursa, sizeof(int), 0);
                send(client_fd, args, nr_octeti_cale_sursa, 0);
                args = strtok(NULL, " \n;");
                int nr_octeti_cale_destinatie = strlen(args);
                send(client_fd, &nr_octeti_cale_destinatie, sizeof(int), 0);
                send(client_fd, args, nr_octeti_cale_destinatie, 0);
                int val;
                valread = recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
            }
            else if(strcmp(args, "UPDATE") == 0)
            {
                args = strtok(NULL, " \n;");
                int nr_octeti_cale = strlen(args);
                send(client_fd, &nr_octeti_cale, sizeof(int), 0);
                send(client_fd, args, nr_octeti_cale, 0);
                args = strtok(NULL, " \n;");
                int octet_start = atoi(args);
                send(client_fd, &octet_start, sizeof(int), 0);
                args = strtok(NULL, " \n;");
                int dimensiune = atoi(args);
                send(client_fd, &dimensiune, sizeof(int), 0);
                args = strtok(NULL, " \n;");
                send(client_fd, args, dimensiune, 0);
                int val;
                valread = recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
            }
            else
            {
                int val;
                recv(client_fd, &val, sizeof(int), 0);
                printf("0x%x\n", val);
            }
        }
    }
    close(client_fd);
    return 0;
}