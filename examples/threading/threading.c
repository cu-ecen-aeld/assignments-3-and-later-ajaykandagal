#include "threading.h"
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)
// Typecasts miliseconds to useconds_t and converts to microseconds
#define MSEC_TO_USEC(msec) (((useconds_t)(msec)) * 1000)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    int status;

    // Typecast thread_param to struct thread_data pointer
    struct thread_data *thread_data1 = (struct thread_data*) thread_param;

    // Sleep for wait_to_obtain_ms to obtain mutex
    status = usleep(thread_data1->wait_to_obtain_us);

    if (status != 0) {
        ERROR_LOG("Failed to call usleep()\n");
        thread_data1->thread_complete_success = false;
    }

    // Obtain mutex
    status = pthread_mutex_lock(thread_data1->mutex);

    if (status != 0) {
        ERROR_LOG("Failed to obtain mutex\n");
        thread_data1->thread_complete_success = false;
    }

    // Sleep for wait_to_release_ms to release mutex
    status = usleep(thread_data1->wait_to_release_us);

     if (status != 0) {
        ERROR_LOG("Failed to call usleep\n");
        thread_data1->thread_complete_success = false;
    }

    // Release mutex
    status = pthread_mutex_unlock(thread_data1->mutex);

    if (status != 0) {
        ERROR_LOG("Failed to release mutex\n");
        thread_data1->thread_complete_success = false;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // Allocate memory to thread_data using malloc
    struct thread_data *thread_data1 = (struct thread_data*) malloc (sizeof(struct thread_data));

    if (thread_data1 == NULL)
    {
        ERROR_LOG("Could not allocate memory to struct thread_data\n");
        return false;
    }

    // Initialize thread_data structure
    thread_data1->mutex = mutex;
    thread_data1->wait_to_obtain_us = MSEC_TO_USEC(wait_to_obtain_ms);
    thread_data1->wait_to_release_us = MSEC_TO_USEC(wait_to_release_ms);
    thread_data1->thread_complete_success = true;

    // Create thread and pass thread_data1 as parameter
    int status = pthread_create(thread, NULL, threadfunc, (void*)thread_data1);

    if (status != 0)
    {
        ERROR_LOG("Could not create the thread\n");
        return false;
    }

    return true;
}

