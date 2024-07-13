#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>


#define SUCCESS 0
#define FAILURE 1



int main(int argc, char* argv[])
{
    FILE* file;
    const char* file_path;
    const char* string_to_write;
    int ret = SUCCESS;

    openlog("writer", LOG_PERROR | LOG_PID, LOG_USER);

    if (argc != 3)
    {
        syslog(LOG_ERR, "Usage: %s <file_path> <string>", argv[0]);
        ret = FAILURE;
        goto exit;
    }

    file_path = argv[1];
    string_to_write = argv[2];

    // open the file
    file = fopen(file_path, "w");
    if (file == NULL)
    {
        syslog(LOG_ERR, "Error opening file %s: %s", file_path, strerror(errno));
        ret = FAILURE;
        goto exit;
    }

    // write to the file
    if (fprintf(file, "%s", string_to_write) < 0)
    {
        syslog(LOG_ERR, "Error writing to file %s: %s", file_path, strerror(errno));
        ret = FAILURE;
        goto close_file;
    }

    // successful write message
    syslog(LOG_DEBUG, "Writing %s to %s", string_to_write, file_path);

close_file:
    // close file
    if (fclose(file) != 0)
    {
        syslog(LOG_ERR, "Error closing file %s: %s", file_path, strerror(errno));
        ret = FAILURE;
    }

exit:
    // close log
    closelog();
    return ret;
}
