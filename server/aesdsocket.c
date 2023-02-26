/**
 * @file    aesdsocket.c
 * @brief   This program creates a socket and listens on Port 9000. It connects to a 
 *          client, receives data until '\n' character is found and then writes the 
 *          received data to the file "/var/tmp/aesdsocketdata". It reads all the 
 *          data from the file and then sends it back to the client.
 *          
 *          Note: Runs in daemon mode when -d is passed.
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

#define SERVER_PORT         (9000)
#define MAX_BACKLOGS        (3)
#define BUFFER_MAX_SIZE     (1024)
#define SOCK_DATA_FILE      ("/var/tmp/aesdsocketdata")

int process_read_data(char *buffer, int buff_len);
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
        printf("Error: %s : Failed to open %s file\n", strerror(errno), SOCK_DATA_FILE);
        syslog(LOG_ERR, "Error: %s : Failed to open %s file\n", strerror(errno), SOCK_DATA_FILE);
        exit_cleanup();
        return -1;
    }

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Check if socket is created successfully
    if (server_fd < 0)
    {
        printf("Error: %s : Failed to create socket\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to create socket\n", strerror(errno));
        exit_cleanup();
        return -1;
    }

    // Set socket options for reusing address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        printf("Error: %s : Failed to set socket options\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to set socket options\n", strerror(errno));
        exit_cleanup();
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        printf("Error: %s : Failed to bind on port %d\n", strerror(errno), SERVER_PORT);
        syslog(LOG_ERR, "Error: %s : Failed to bind  on port %d\n", strerror(errno), SERVER_PORT);
        exit_cleanup();
        return -1;
    }

    if (listen(server_fd, MAX_BACKLOGS))
    {
        printf("Error: %s : Failed to start listening on port %d\n", strerror(errno), SERVER_PORT);
        syslog(LOG_ERR, "Error: %s : Failed to start listening  on port %d\n", strerror(errno), SERVER_PORT);
        exit_cleanup();
        return -1;
    }
    
    printf("Listening on port %d...\n", SERVER_PORT);
    syslog(LOG_INFO, "Listening on port %d...\n", SERVER_PORT);

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
            printf("Error: %s : Failed to connect to client\n", strerror(errno));
            syslog(LOG_ERR, "Error: %s : Failed to connect to client\n", strerror(errno));
            goto close_client;
        }
        else
        {
            printf("Accepted connection from %s\n", client_addr_str);
            syslog(LOG_INFO, "Accepted connection from %s", client_addr_str);
        }

        while (true)   // loop for a connection session
        {
            while (true)   // loop for a read session until \n is found
            {
                buffer_len = read(client_fd, buffer, BUFFER_MAX_SIZE);

                if (buffer_len == 0)
                {
                    goto close_client;
                }
                else if(buffer_len < 0)
                {
                    printf("Error: %s : Error while getting data from the client\n", strerror(errno));
                    syslog(LOG_ERR, "Error: %s : Error while getting data from the client\n", strerror(errno));
                    goto close_client;
                }

                int ret_data = process_read_data(buffer, buffer_len);

                if (ret_data == -1)
                {
                    printf("Error: Failed to malloc for read buffer\n");
                    syslog(LOG_ERR, "Error: Failed to malloc for read buffer\n");
                    goto close_client;
                }
                else if (ret_data > 0)
                {
                    total_recv_bytes += ret_data;
                    break;
                }
                else;
            }
            printf("Received all bytes from the client\n");
            syslog(LOG_INFO, "Received all bytes from the client\n");

            int ret_data = get_write_data(&write_malloc_buffer, total_recv_bytes, file_fd);

            if (ret_data == -1)
            {
                printf("Error: Failed to malloc for write buffer\n");
                syslog(LOG_ERR, "Error: Failed to malloc for write buffer\n");
                goto close_client;
            }
            else if (ret_data != total_recv_bytes)
            {
                printf("Error: Failed to read all data from the file\n");
                syslog(LOG_ERR, "Error: Failed to read all data from the file\n");
                exit_cleanup();
                return -1;
            }
            else
            {
                printf("Successfully read all bytes from the file\n");
            }

            buffer_len = write(client_fd, write_malloc_buffer, total_recv_bytes);

            if (buffer_len == total_recv_bytes)
            {
                printf("Sent all bytes to the client\n");
                syslog(LOG_INFO, "Sent all bytes to the client\n");
            }
            else if (buffer_len < 0)
            {
                printf("Error: %s : Detected error while writing on socket\n", strerror(errno));
                syslog(LOG_ERR, "Error: %s : Detected error while writing on socket\n", strerror(errno));
            }
            else
            {
                printf("Error: Failed to send all bytes to client\n");
                syslog(LOG_ERR, "Error: Failed to send all bytes to client\n");
                goto close_client; 
            }
        }

    close_client:
        if (client_fd > 0)
        {
            close(client_fd);
            printf("Connection Closed from %s\n", client_addr_str);
            syslog(LOG_INFO, "Closed connection from %s", client_addr_str);
        }
    }

    exit_cleanup();
    return ret_status;
}

/**
 * @brief   The function process the data read from the client. It mallocs buff_len
 *          size and copy data from the read buffer. If '\n' is found in the buffer
 *          then entire string will be copied to the file.
 * 
 * @param   
 *  *buffer     A char array contains data read from the client
 *  buff_len    Number of bytes present in the *buffer array.
 * 
 * @return  Returns number of bytes written to the file on success. Returns 0 if no
 *          bytes were written to the file. Returns -1 when malloc fails.
*/
int process_read_data(char *buffer, int buff_len)
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
        if (write(file_fd, malloc_buffer, malloc_buffer_len) < 0)
        {
            printf("Error: %s : Detected error while writing to the file\n", strerror(errno));
            syslog(LOG_ERR, "Error: %s : Detected error while writing to the file\n", strerror(errno));
            ret_len = 0;
        }
        free(malloc_buffer);
        malloc_buffer = NULL;
        malloc_buffer_len = 0;

        return ret_len;
    }

    return 0;
}

/**
 * @brief   The function process the data read from the client. It mallocs buff_len
 *          size and copy data from the read buffer. If '\n' is found in the buffer
 *          then entire string will be copied to the file.
 * 
 * @param   
 *  *buffer     A char array contains data read from the client
 *  buff_len    Number of bytes present in the *buffer array.
 * 
 * @return  Returns number of bytes written to the file on success. Returns -1 when 
 *          malloc fails.
*/
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

    if (read_ret < 0)
    {
        printf("Error: %s : Detected error while reading from the file\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Detected error while reading from the file\n", strerror(errno));
        read_ret = 0;
    }

    return read_ret;
}

/**
 * @brief   Closes all the open files and sockets. Deletes the file which was 
 *          opened for writing socket data.
 * 
 * @param   none
 * 
 * @return  void
*/
void exit_cleanup()
{
    if (write_malloc_buffer)
        free(write_malloc_buffer);

    if (file_fd > 0)
        close(file_fd);

    if (server_fd > 0)
        close(server_fd);

    remove(SOCK_DATA_FILE);

    closelog();
}

/**
 * @brief   Called when SIGINT or SIGTERM are received.
 * 
 * @param   none
 * 
 * @return  void
*/
void sig_int_term_handler()
{
    printf("Exiting...\n");
    syslog(LOG_INFO, "Exiting...\n");
    exit_cleanup();
    exit(EXIT_SUCCESS);
}

/**
 * @brief   Makes the process to run as daemon when -d is passed
 *          while launching aesdsocket application.
 * 
 * @param   none
 * 
 * @return  void
*/
void become_daemon()
{
    pid_t pid;
    
    pid = fork();
    
    if (pid < 0)
    {
        printf("Error while creating child process\n");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
    
     // Terminate the parent process
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    // On success make the child process session leader
    if (setsid() < 0) {
        printf("Error: %s : Failed to make child process as session leader\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to make child process as session leader\n", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    int devNull = open("/dev/null", O_RDWR);

    if(devNull < 0) {
        printf("Error: %s : Failed to open '/dev/null'\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to open '/dev/null'\n", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    if(dup2(devNull, STDOUT_FILENO) < 0) {
        printf("Error: %s : Failed to redirect to '/dev/null'\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to redirect to '/dev/null'\n", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
    
    // Change the working directory to the root directory
    if (chdir("/"))
    {
        printf("Error: %s : Failed to switch to root directory\n", strerror(errno));
        syslog(LOG_ERR, "Error: %s : Failed to switch to root directory\n", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief   Prints out correct usage of application command when
 *          user makes mistake.
 * 
 * @param   none
 * 
 * @return  void
*/
void print_usage()
{
    printf("Total number of arguements should 1 or less\n");
    printf("The order of arguements should be:\n");
    printf("\t1) To run the process as daemon\n");
    printf("Usgae: aesdsocket -d\n");
}