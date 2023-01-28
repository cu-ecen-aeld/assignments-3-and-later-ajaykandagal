#include <stdio.h>
#include <string.h>

void print_usage()
{
    printf("Total number of arguements should be 2\n");
    printf("The order of arguements should be:\n");
    printf("\t1) File directory path\n");
    printf("\t2) String to be written in to the specified file directory path\n");
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        printf("Error: Invalid number of arguements\n");
        print_usage();
        return 1;
    }

    char* writefile = argv[1];
    char* writestr = argv[2];

    FILE *fd;
    fd = fopen(writefile, "w");

    if (fd == NULL) {
        printf("Error: Failed to create \"%s\" file\n", writefile);
        return 1;
    }

    int written_len = fprintf(fd, "%s", writestr);

    if (written_len != strlen(writestr))
        printf("Error: Failed to write \"%s\" string into \"%s\" file\n", writestr, writefile);
    
    fclose(fd);

    return 0;
}