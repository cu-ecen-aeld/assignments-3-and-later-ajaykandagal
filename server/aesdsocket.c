/**
 * @file    aesdsocket.c
 * @brief   This program creates a socket and listens on Port 9000.
 *          It connects to a client, receives data until '\n' character
 *          is found and then writes the received data to the file
 *          "/var/tmp/aesdsocketdata". It reads all the data from the
 *          file and then sends it back to the client.
 * 
 * @author  Ajay Kandagal <ajka9053@colorado.edu>
 * @date    Feb 25th 2023
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define STDOUT_ENABLED      (0)

#if STDOUT_ENABLED
#define LOG_PRINTF          printf
#else
#define LOG_PRINTF(...)
#endif

#define SERVER_PORT         (9000)
#define MAX_BACKLOGS        (3)
#define BUFFER_MAX_SIZE     (1024)
#define SOCK_DATA_FILE      ("/var/tmp/aesdsocketdata")

int process_read_data(char *buffer, int buff_len, int file_fd);
int get_write_data(char **malloc_buffer, int total_size, int file_fd);
void become_daemon();
void sig_int_term_handler();
void print_usage();
void exit_cleanup();

int file_fd;
int server_fd;
char *write_malloc_buffer = NULL;

int main(int argc, char** argv)
{
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    char client_addr_str[INET_ADDRSTRLEN];
    int opt = 1;

    char buffer[BUFFER_MAX_SIZE];
    int buffer_len = 0;

    int total_recv_bytes = 0;

    int ret_status = 0;
    int run_as_daemon = 0;

    memset(&client_addr, 0, sizeof(client_addr));
    memset(&client_addr_len, 0, sizeof(client_addr_len));

    if (argc <= 2) {
        if (argc == 2) {
            if (!strcmp(argv[1],"-d")) {
               run_as_daemon = 1;
               printf("The process will be run as daemon\n");
            }
            else {
                print_usage();
                return -1;
            }
        }
    }
    else {
        print_usage();
        return -1;
    }

    openlog(NULL, 0, LOG_USER);

    signal(SIGINT, sig_int_term_handler);
    signal(SIGTERM, sig_int_term_handler);

    // file_ptr = fopen("/var/tmp/aesdsocketdata", "w+");
    file_fd = open(SOCK_DATA_FILE, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);

    if (file_fd < 0)
    {
        printf("Failed to open %s file\n", SOCK_DATA_FILE);
        exit_cleanup();
        return -1;
    }

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Check if socket is created successfully
    if (server_fd < 0)
    {
        printf("Failed to create socket\n");
        exit_cleanup();
        return -1;
    }
    else
    {
        printf("Created scoket\n");
    }

    // Set socket options for reusing address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        printf("Failed to set socket options\n");
        exit_cleanup();
        return -1;
    }
    else
    {
        printf("Successfully set socket options\n");
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        printf("Failed to bind\n");
        exit_cleanup();
        return -1;
    }
    else
    {
        printf("Successfully binded\n");
    }

    if (listen(server_fd, MAX_BACKLOGS))
    {
        printf("Failed to listen\n");
        exit_cleanup();
        return -1;
    }
    else
    {
        printf("listening...\n");
    }

    if (run_as_daemon)
        become_daemon();

    while (true)       // loop for new connection
    {
        // Accept the incoming connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        // Get ip address of client in string
        inet_ntop(AF_INET, &client_addr.sin_addr, client_addr_str, sizeof(client_addr_str));

        if (client_fd < 0)
        {
            printf("Failed to connect to client\n");
            goto close_client;
        }
        else
        {
            syslog(LOG_INFO, "Accepted connection from %s", client_addr_str);
            printf("Accepted connection from %s\n", client_addr_str);
        }

        while (true)   // loop for a connection session
        {
            while (true)   // loop for a read session
            {
                buffer_len = read(client_fd, buffer, BUFFER_MAX_SIZE);

                if (buffer_len == 0)
                {
                    goto close_client;
                }
                else if(buffer_len < 0)
                {
                    LOG_PRINTF("Error getting data from client\n");
                    goto close_client;
                }

                int ret_data = process_read_data(buffer, buffer_len, file_fd);

                if (ret_data == -1)
                {
                    LOG_PRINTF("Malloc error during read\n");
                    goto close_client;
                }
                else if (ret_data > 0)
                {
                    total_recv_bytes += ret_data;
                    break;
                }
                else;
            }
            LOG_PRINTF("Read bytes till /\n from client\n");

            int ret_data = get_write_data(&write_malloc_buffer, total_recv_bytes, file_fd);

            if (ret_data == -1)
            {
                LOG_PRINTF("Malloc error during write\n");
                goto close_client;
            }
            else if (ret_data != total_recv_bytes)
            {
                LOG_PRINTF("Failed to read all the data from the file\n");
                goto close_client;
            }
            else
            {
                LOG_PRINTF("Successfully read all bytes from the file\n");
            }

            buffer_len = write(client_fd, write_malloc_buffer, total_recv_bytes);

            if (buffer_len == total_recv_bytes)
            {
                LOG_PRINTF("Sent all bytes to client!\n");
            }
            else
            {
                LOG_PRINTF("Error while sending data to client\n");
                goto close_client; 
            }
        }

    close_client:
        if (client_fd > 0)
        {
            close(client_fd);
            syslog(LOG_INFO, "Closed connection from %s", client_addr_str);
            printf("Connection Closed from %s\n", client_addr_str);
        }
    }

    exit_cleanup();
    return ret_status;
}

int process_read_data(char *buffer, int buff_len, int file_fd)
{
    static char *malloc_buffer = NULL;
    static int malloc_buffer_len = 0;
    int index = 0;

    // Find the delimeter '\n' in the received buffer
    for (index = 0; index < buff_len && buffer[index] != '\n'; index++)
        ;

    if (malloc_buffer == NULL)
        malloc_buffer = (char *)malloc(sizeof(char) * (index == buff_len ? buff_len : index + 1));
    else
        malloc_buffer = (char *)realloc(malloc_buffer, (malloc_buffer_len + (index == buff_len ? buff_len : index + 1)));

    if (malloc_buffer == NULL)
    {
        return -1;
    }

    // copy data including \n and update malloc buffer length
    memcpy(malloc_buffer + malloc_buffer_len, buffer, (index == buff_len ? buff_len : index + 1));
    malloc_buffer_len += (index == buff_len ? buff_len : index + 1);

    int ret_len = malloc_buffer_len;

    if (index < buff_len)
    {
        write(file_fd, malloc_buffer, malloc_buffer_len);
        free(malloc_buffer);
        malloc_buffer = NULL;
        malloc_buffer_len = 0;

        return ret_len;
    }

    return 0;
}

int get_write_data(char **malloc_buffer, int total_size, int file_fd)
{
    if (*malloc_buffer != NULL)
        free(*malloc_buffer);

    *malloc_buffer = (char *)malloc(sizeof(char) * total_size);

    if (*malloc_buffer == NULL)
    {
        return -1;
    }

    lseek(file_fd, 0, SEEK_SET);

    int read_ret = read(file_fd, *malloc_buffer, total_size);

    return read_ret;
}

void exit_cleanup()
{
    if (write_malloc_buffer)
        free(write_malloc_buffer);

    if (file_fd > 0)
        close(file_fd);

    if (server_fd > 0)
        close(server_fd);

    if (remove(SOCK_DATA_FILE))
        printf("Error while deleting %s file\n", SOCK_DATA_FILE);
}

void sig_int_term_handler()
{
    printf("Exiting...\n");
    exit_cleanup();
    exit(EXIT_SUCCESS);
}

void become_daemon()
{
    pid_t pid;
    
    pid = fork();
    
    if (pid < 0) {
        printf("Error while creating child process\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
    
     // Terminate the parent process
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // On success make the child process session leader
    if (setsid() < 0) {
        printf("Error while making child process as session leader\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    int devNull = open("/dev/null", O_RDWR);

    if(devNull < 0){
        printf("Error while opening '/dev/null'\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    if(dup2(devNull, STDOUT_FILENO) < 0) {
        printf("Error in dup2\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
    
    // Change the working directory to the root directory
    if (chdir("/"))
    {
        printf("Error while changing to root dir\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
}

void print_usage()
{
    printf("Total number of arguements should 1 or less\n");
    printf("The order of arguements should be:\n");
    printf("\t1) To run the process as daemon\n");
    printf("Usgae: aesdsocket -d\n");
}