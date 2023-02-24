#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#define SERVER_PORT     (9000)
#define MAX_BACKLOGS    (3)
#define BUFFER_MAX_SIZE (20000)

void print_buffer(char *buffer, int len);

int main()
{
    int server_fd;
    int client_fd;
    FILE *file_ptr;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    char buffer[BUFFER_MAX_SIZE];
    int buffer_len;
    int opt = 1;

    file_ptr = fopen("/var/tmp/aesdsocketdata", "w+");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        printf("Failed to create socket\n");
        return -1;
    }
    else
    {
        printf("Created scoket!\n");
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        printf("Failed to set socket options");
        return -1;
    }
    else
    {
        printf("Successfully set socket options!\n");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)))
    {
        printf("Failed to bind\n");
        return -1;
    }
    else
    {
        printf("Successfully binded!\n");
    }

    for (int i = 0; i < 5; i++) {
        if (listen(server_fd, MAX_BACKLOGS))
        {
            printf("Failed to listen\n");
            return -1;
        }
        else
        {
            printf("listening...\n");
        }

        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);

        if (client_fd < 0)
        {
            printf("Failed to connect to client\n");
            return -1;
        }
        else
        {
            printf("Connected to client!\n");
        }

        buffer_len = read(client_fd, buffer, BUFFER_MAX_SIZE);
        print_buffer(buffer, buffer_len);
        fwrite(buffer, buffer_len, 1, file_ptr);
        
        buffer_len = 0;
        fseek(file_ptr, 0, SEEK_SET);
        while(fread(&buffer[buffer_len++], 1, 1, file_ptr) > 0);
        buffer_len = write(client_fd, buffer, buffer_len - 1);

        close(client_fd);
    }
    fclose(file_ptr);
    shutdown(server_fd, SHUT_RDWR);
    
    return 0;
}

void print_buffer(char *buffer, int len)
{
    printf("%d : ", len);

    for (int i = 0; i < len; i++)
    {
        printf("%c", buffer[i]);
    }
}