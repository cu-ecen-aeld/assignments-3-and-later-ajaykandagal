/**
 * @file    aesdsocket.c
 * @brief   This program creates a socket and listens on Port 9000. It creates a
 *          new for each new connection. The thereads receives data from client
 *          until '\n' character is found and then writes the received data to 
 *          the file "/var/tmp/aesdsocketdata". Then all the bytes from the file 
 *          are read and sent it back to the client.
 * 
 *          The timer handler will add time-stamp to "/var/tmp/aesdsocketdata"
 *          file every 10 seconds.
 *
 *          Note: Runs in daemon mode when -d is passed.
 *
 * @author  Ajay Kandagal <ajka9053@colorado.edu>
 * @date    Mar 5th 2023
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
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>


#define SERVER_PORT         (9000)
#define MAX_BACKLOGS        (3)
#define BUFFER_MAX_SIZE     (1024)
#define SOCK_DATA_FILE      ("/var/tmp/aesdsocketdata")


// Macro from https://raw.githubusercontent.com/freebsd/freebsd/stable/10/sys/sys/queue.h
#define SLIST_FOREACH_SAFE(var, head, field, tvar)        \
    for ((var) = SLIST_FIRST((head));                     \
         (var) && ((tvar) = SLIST_NEXT((var), field), 1); \
         (var) = (tvar))


void *connection_handler(void *client_data);
int sock_read(int client_fd, char **malloc_buffer, int *malloc_buffer_len);
int file_read(int file_fd, char **malloc_buffer, int *malloc_buffer_len);
void become_daemon();
void print_usage();
void exit_cleanup();
void sig_int_term_handler();
void sig_alarm_handler();


struct client_node_t
{
    int sock_fd;
    struct sockaddr_in addr;
    socklen_t addr_len;
    pthread_t thread_id;
    char *malloc_buffer;
    bool completed;
    SLIST_ENTRY(client_node_t) client_list;
};

// Singly linked list to keep track of all the threads created
SLIST_HEAD(client_list_head_t, client_node_t);
struct client_list_head_t client_list_head;

int file_fd;
pthread_mutex_t file_lock;

int server_fd;
int sig_exit_status = 0;


int main(int argc, char **argv)
{
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int opt = 1;
    int ret_status = 0;
    int run_as_daemon = 0;

    memset(&client_addr, 0, sizeof(client_addr));
    memset(&client_addr_len, 0, sizeof(client_addr_len));

    if (argc <= 2)
    {
        if (argc == 2)
        {
            if (!strcmp(argv[1], "-d"))
            {
                run_as_daemon = 1;
                printf("aesdsocket will run as daemon\n");
            }
            else
            {
                print_usage();
                return -1;
            }
        }
    }
    else
    {
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
        printf("Error while opening %s file: %s\n", SOCK_DATA_FILE, strerror(errno));
        syslog(LOG_ERR, "Error while opening %s file: %s", SOCK_DATA_FILE, strerror(errno));
        exit_cleanup();
        return -1;
    }

    ret_status = pthread_mutex_init(&file_lock, NULL);

    if (ret_status != 0)
    {
        perror("Mutex init has failed");
        exit_cleanup();
        return -1;
    }

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Check if socket is created successfully
    if (server_fd < 0)
    {
        perror("Failed to create socket");
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        exit_cleanup();
        return -1;
    }

    // Set socket options for reusing address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Failed to set socket options");
        syslog(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
        exit_cleanup();
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        printf("Failed to bind on port %d: %s\n", SERVER_PORT, strerror(errno));
        syslog(LOG_ERR, "Failed to bind  on port %d: %s", SERVER_PORT, strerror(errno));
        exit_cleanup();
        return -1;
    }

    if (listen(server_fd, MAX_BACKLOGS))
    {
        printf("Failed to start listening on port %d: %s\n", SERVER_PORT, strerror(errno));
        syslog(LOG_ERR, "Failed to start listening  on port %d: %s", SERVER_PORT, strerror(errno));
        exit_cleanup();
        return -1;
    }

    printf("Listening on port %d...\n", SERVER_PORT);
    syslog(LOG_INFO, "Listening on port %d...", SERVER_PORT);

    if (run_as_daemon)
        become_daemon();

    signal(SIGALRM, sig_alarm_handler);
    // Set to generate SIGALRM signal every 10 seconds
    alarm(10);

    struct client_node_t *client_node;
    struct client_node_t *tmp_client_node;
    SLIST_INIT(&client_list_head);

    while (true)
    {
        // Accept the incoming connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd < 0)
        {
            if (sig_exit_status)
                break;
            
            perror("Failed to connect to client");
            syslog(LOG_ERR, "Failed to connect to client: %s", strerror(errno));
        }
        else
        {
            // Store client data in client node struct
            client_node = malloc(sizeof(struct client_node_t));
            client_node->sock_fd = client_fd;
            client_node->addr = client_addr;
            client_node->addr_len = client_addr_len;
            client_node->malloc_buffer = NULL;
            client_node->completed = 0;
            SLIST_INSERT_HEAD(&client_list_head, client_node, client_list);

            // Create a new thread for the connection
            ret_status = pthread_create(&client_node->thread_id, NULL, connection_handler, (void *)client_node);

            if (ret_status < 0)
            {
                perror("Error while creating the thread");
                syslog(LOG_ERR, "Error while creating the thread: %s\n", strerror(errno));

                // Delete client node data from list if fails
                SLIST_REMOVE(&client_list_head, client_node, client_node_t, client_list);

                close(client_node->sock_fd);
                free(client_node);
            }
            
            // Join completed threads, remove the corresponding node from list and free the node
            SLIST_FOREACH_SAFE(client_node, &client_list_head, client_list, tmp_client_node)
            {
                if (client_node->completed)
                {
                    pthread_join(client_node->thread_id, NULL);
                    SLIST_REMOVE(&client_list_head, client_node, client_node_t, client_list);
                    free(client_node);
                }
            }
        }
    }

    // Join all the threads
    SLIST_FOREACH(client_node, &client_list_head, client_list)
    {
        pthread_join(client_node->thread_id, NULL);
    }

    // Delete all the threads' data
    while (!SLIST_EMPTY(&client_list_head))
    {
        client_node = SLIST_FIRST(&client_list_head);
        SLIST_REMOVE_HEAD(&client_list_head, client_list);
        free(client_node);
    }

    pthread_mutex_destroy(&file_lock);

    exit_cleanup();
    
    return 0;
}

/**
 * @brief   Receives data from client until '\n' character is found and then 
 *          writes the received data to the file "/var/tmp/aesdsocketdata". 
 *          Then all the bytes from the file are read and sent it back to the
 *          client. After sending data to the client, closes the connection
 *          and exits the thread execution.
 *
 * @param   client_data: Contains all the info related to the client such as
 *          client fd, addr, thread completed status etc.
 *
 * @return  void
 */
void *connection_handler(void *client_data)
{
    struct client_node_t *client_node = (struct client_node_t *)client_data;

    char client_addr_str[INET_ADDRSTRLEN];

    // Get ip address of client in string
    inet_ntop(AF_INET, &client_node->addr.sin_addr, client_addr_str, sizeof(client_addr_str));

    printf("Accepted connection from %s\n", client_addr_str);
    syslog(LOG_INFO, "Accepted connection from %s", client_addr_str);

    int malloc_buffer_len = 0;
    int ret_status;

    while (true && !sig_exit_status) // loop for a read session until \n is found
    {
        ret_status = sock_read(client_node->sock_fd, &client_node->malloc_buffer, &malloc_buffer_len);

        if (ret_status == 0)
            continue;

        if (ret_status < 0)
            goto close_client;

        pthread_mutex_lock(&file_lock);
        ret_status = write(file_fd, client_node->malloc_buffer, malloc_buffer_len);
        pthread_mutex_unlock(&file_lock);

        if (ret_status < 0)
        {
            perror("Error while writing to the file");
            syslog(LOG_ERR, "Error while writing to the file: %s", strerror(errno));
            goto close_client;
        }

        break;
    }

    if (sig_exit_status)
        goto close_client;

    ret_status = file_read(file_fd, &client_node->malloc_buffer, &malloc_buffer_len);

    if (ret_status < 0)
        goto close_client;

    ret_status = write(client_node->sock_fd, client_node->malloc_buffer, malloc_buffer_len);

    if (ret_status < 0)
    {
        perror("Error while writing to the client");
        syslog(LOG_ERR, "Error while writing to the client: %s", strerror(errno));
    }
    else 
    {
        printf("Sent all bytes to the client\n");
        syslog(LOG_INFO, "Sent all bytes to the client");
    }

close_client:
    if (client_node->malloc_buffer)
    {
        free(client_node->malloc_buffer);
        client_node->malloc_buffer = NULL;
    }

    if (client_node->sock_fd > 0)
    {
        close(client_node->sock_fd);
        client_node->sock_fd = 0;
        printf("Connection Closed from %s\n", client_addr_str);
        syslog(LOG_INFO, "Closed connection from %s", client_addr_str);
    }

    client_node->completed = 1;

    return NULL;
}

/**
 * @brief   Reads bytes from the client socket and copies that into the 
 *          malloc buffer.
 *
 * @param   client_fd: Client file descriptor
 * @param   malloc_buffer: Dynamically allocated, all bytes read from 
 *          the socket are copied to this.
 * @param   malloc_buffer_len: Updated to the size of malloc buffer.
 *
 * @return  Returns 1 when \n is encounterd in the read buffer and returns
 *          0 when not found. Returns < 0 on error. 
 */
int sock_read(int client_fd, char **malloc_buffer, int *malloc_buffer_len)
{
    int bytes_count;
    char buffer[BUFFER_MAX_SIZE];
    int buffer_len = read(client_fd, buffer, BUFFER_MAX_SIZE);

    if (buffer_len <= 0)
    {
        perror("Error while getting data from the client");
        syslog(LOG_ERR, "Error while getting data from the client: %s\n", strerror(errno));
        return -1;
    }

    int index = 0;

    // Find the delimeter '\n' in the received buffer
    for (index = 0; index < buffer_len && buffer[index] != '\n'; index++);

    // Adjust buffer length to be allocated
    if (index < buffer_len)
        bytes_count = index + 1;
    else
        bytes_count = buffer_len;

    if (*malloc_buffer)
        *malloc_buffer = (char *)realloc(*malloc_buffer, *malloc_buffer_len + bytes_count);
    else
        *malloc_buffer = (char *)malloc(sizeof(char) * bytes_count);
        
    if (*malloc_buffer == NULL)
    {
        printf("Error while allocating memmory to buffer\n");
        syslog(LOG_ERR, "Error while allocating memmory to buffer");
        return -1;
    }

    // copy data including \n and update malloc buffer length
    memcpy(*malloc_buffer + *malloc_buffer_len, buffer, bytes_count);
    *malloc_buffer_len += bytes_count;

    if (index < buffer_len)
        return 1;
    else
        return 0;
}

/**
 * @brief   Reads all the bytes from the file and copies it to the
 *          malloc buffer.
 *
 * @param   file_fd: File descriptor
 * @param   malloc_buffer: Dynamically allocated, all bytes read
 *          from the file are copied to this.
 * @param   malloc_buffer_len: Updated to the size of malloc buffer.
 *
 * @return  Returns 0 on success else < 0 value is returned on error. 
 */
int file_read(int file_fd, char **malloc_buffer, int *malloc_buffer_len)
{
    int char_count;
    char char_data;
    *malloc_buffer_len = 0;

    pthread_mutex_lock(&file_lock);
    lseek(file_fd, 0, SEEK_SET);
    for (char_count = 0; read(file_fd, &char_data, 1) > 0; char_count++);
    pthread_mutex_unlock(&file_lock);

    if (char_count <= 0)
    {
        printf("File is empty!\n");
        syslog(LOG_ERR, "File is empty!");
        return -1;
    }

    if (*malloc_buffer)
        *malloc_buffer = (char *)realloc(*malloc_buffer, char_count);
    else
        *malloc_buffer = (char *)malloc(sizeof(char) * char_count);
        

    if (*malloc_buffer == NULL)
    {
        printf("Error while allocating memmory to buffer\n");
        syslog(LOG_ERR, "Error while allocating memmory to buffer");
        return -1;
    }

    pthread_mutex_lock(&file_lock);

    lseek(file_fd, 0, SEEK_SET);
    int ret_status = read(file_fd, *malloc_buffer, char_count);

    pthread_mutex_unlock(&file_lock);

    if (ret_status < -0)
    {
        perror("Error while reading data from the file");
        syslog(LOG_ERR, "Error while reading data from the file: %s", strerror(errno));

        return -1;
    }
    else if (ret_status != char_count)
    {
        printf("Failed to read all data from the file\n");
        syslog(LOG_ERR, "Failed to read all data from the file\n");
        return -1;
    }
    else
    {
        *malloc_buffer_len = char_count;
        return 0;
    }
}

/**
 * @brief   Closes all the open files, syslog and server socket. Deletes 
 *          the file which was opened for writing socket data.
 *
 * @param   void
 *
 * @return  void
 */
void exit_cleanup(void)
{
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
 * @param   void
 *
 * @return  void
 */
void sig_int_term_handler(void)
{
    printf("Exiting...\n");
    syslog(LOG_INFO, "Exiting...\n");
    sig_exit_status = 1;
    close(server_fd);
}

/**
 * @brief   Signal handler function for SIGALRM, will be triggered
 *          every 10 seconds. Logs the time-stamp data to the 
 *          /var/tmp/aesdsocketdata file.
 *
 * @param   void
 *
 * @return  void
 */
void sig_alarm_handler(void)
{
    time_t raw_time;
    struct tm *time_st;
    char buffer[100];
    char timestamp[80] = "timestamp:time\n";

    time(&raw_time);

    time_st = localtime(&raw_time);

    strftime(timestamp,80,"%x - %H:%M:%S", time_st);

    sprintf(buffer, "timestamp:%s\n", timestamp);

    pthread_mutex_lock(&file_lock);
    write(file_fd, buffer, strlen(buffer));
    pthread_mutex_unlock(&file_lock);

    alarm(10);
}

/**
 * @brief   Prints out correct usage of application command when
 *          user makes mistake.
 *
 * @param   void
 *
 * @return  void
 */
void print_usage(void)
{
    printf("Total number of arguements should 1 or less\n");
    printf("The order of arguements should be:\n");
    printf("\t1) To run the process as daemon\n");
    printf("Usgae: aesdsocket -d\n");
}

/**
 * @brief   Makes the process to run as daemon when -d is passed
 *          while launching aesdsocket application.
 *
 * @param   void
 *
 * @return  void
 */
void become_daemon(void)
{
    pid_t pid;

    pid = fork();

    if (pid < 0)
    {
        perror("Error while creating child process");
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    // Terminate the parent process
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // On success make the child process session leader
    if (setsid() < 0)
    {
        perror("Failed to make child process as session leader");
        syslog(LOG_ERR, "Failed to make child process as session leader: %s", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    int devNull = open("/dev/null", O_RDWR);

    if (devNull < 0)
    {
        perror("Failed to open '/dev/null'");
        syslog(LOG_ERR, "Failed to open '/dev/null': %s", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    if (dup2(devNull, STDOUT_FILENO) < 0)
    {
        perror("Failed to redirect to '/dev/null'");
        syslog(LOG_ERR, "Failed to redirect to '/dev/null': %s\n", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }

    // Change the working directory to the root directory
    if (chdir("/"))
    {
        perror("Failed to switch to root directory");
        syslog(LOG_ERR, "Failed to switch to root directory: %s", strerror(errno));
        exit_cleanup();
        exit(EXIT_FAILURE);
    }
}