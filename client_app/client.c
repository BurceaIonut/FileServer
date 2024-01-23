#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)
        <= 0) {
        printf(
            "\nInvalid address/ Address not supported \n");
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
        if(strcmp(args, "LIST") == 0)
        {
            send(client_fd, args, strlen(args), 0);
            int val;
            uint32_t nr;
            valread = recv(client_fd, &val, sizeof(int), 0);
            printf("0x%x\n", val);
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
        else if(strcmp(args, "DOWNLOAD") == 0)
        {
            send(client_fd, args, 1024, 0);
            args = strtok(NULL, " \n;");
            char* filename = (char*)malloc((strlen(args) + 1)*sizeof(char));
            memcpy(filename, args, strlen(args));
            filename[strlen(args)]= '\0';
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
                char* content = (char*)malloc(nr * sizeof(char));//might be unsafe
                recv(client_fd, content, nr, 0);
                printf("%s\n", content);
                int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                write(fd, content, nr);
                close(fd);
                free(content);
            }
            free(filename);
        }
        else
        {
            send(client_fd, args, 1024, 0);
            int nr;
            recv(client_fd, &nr, sizeof(int), 0);
            printf("0x%x\n", nr);
        }
    }
    
    close(client_fd);
    return 0;
}