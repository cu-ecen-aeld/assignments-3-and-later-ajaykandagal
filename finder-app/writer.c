#include <stdio.h>
#include <string.h>
#include <syslog.h>

void print_usage()
{
    syslog(LOG_DEBUG, "Total number of arguements should be 2");
    syslog(LOG_DEBUG, "The order of arguements should be:");
    syslog(LOG_DEBUG, "  1) File directory path");
    syslog(LOG_DEBUG, "  2) String to be written in to the specified file directory path");
}

int main(int argc, char** argv)
{
    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguements: %d", (argc - 1));
        print_usage();
        return 1;
    }

    char* writefile = argv[1];
    char* writestr = argv[2];

    FILE *fd;
    fd = fopen(writefile, "w");

    if (fd == NULL) {
        syslog(LOG_ERR, "Failed to create %s", writefile);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    int written_len = fprintf(fd, "%s", writestr);

    if (written_len != strlen(writestr))
        syslog(LOG_ERR, "Failed to write %s to %s", writestr, writefile);
    
    fclose(fd);

    return 0;
}