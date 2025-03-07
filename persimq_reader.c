#include <stdint.h>
#include <inttypes.h> // printf() definitions for stdint
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
/*#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
*/
#include <sys/stat.h>
#include "persimq.h"

const char APP_VERSION[] = "1.0";

#define PRINT_DEBUG_OFF     (0)
#define PRINT_DEBUG_ON      (1)
#define PRINT_DEBUG_VERBOSE (2)

static int debug_output = PRINT_DEBUG_OFF;
static int print_max = 10;
static char filename[255] = "";
static bool queue_clear = false;

int main(int argc, char *argv[])
{
    // - Check for parameters -
    while (--argc > 0) {
        if (!strcmp(argv[argc], "-v") || !strcmp(argv[argc], "-V")) {
            printf("libpersimq queue reader by Kirill Raguzin.\n");
            printf("AVP Technology LLC, Moscow, 2024.\n");
            printf("Version: %s\n", APP_VERSION);
            return EXIT_SUCCESS;
        } else if (!strcmp(argv[argc], "-h") || !strcmp(argv[argc], "-H") || !strcmp(argv[argc], "-?")) {
            printf("persimq reader %s - libpersimq queue reader. \n", APP_VERSION);
            printf("Reads a persimq queue file and prints out up to -n messages.");
            printf("Available options:\n");
            printf("-f or -F : select queue storage file (mandatory)\n");
            printf("-n or -N : the maximum amout of messages to print out (default: 10)\n");
            printf("-e or -E : extract all messages from the queue\n");
            printf("-d       : show debug messages\n");
            printf("-D       : show verbose debug messages (-d is ignored when -D is set)\n");
            printf("-h or -H or -?   : show this text\n");
            return EXIT_SUCCESS;
        } else if (!strcmp(argv[argc], "-d")) {
            if (!debug_output) debug_output = PRINT_DEBUG_ON;
        } else if (!strcmp(argv[argc], "-D")) {
            debug_output = PRINT_DEBUG_VERBOSE;
        } else if (!strcmp(argv[argc], "-e") || !strcmp(argv[argc], "-E")) {
            queue_clear = true;
        } else if (!strncmp(argv[argc], "-n", 2) || !strncmp(argv[argc], "-N", 2)) {
            if (sscanf(&argv[argc][2], "%d", &print_max) != 1) {
                fprintf(stderr, "Incorrect -n parameter format!\n");
                fflush(stderr);
                return EXIT_FAILURE;
            }
            if (print_max < 0) {
                fprintf(stderr, "Incorrect -n parameter value!\n");
                fflush(stderr);
                return EXIT_FAILURE;
            }
        } else if (!strncmp(argv[argc], "-f", 2) || !strncmp(argv[argc], "-F", 2)) {
            size_t input_len = strlen(&argv[argc][2]);
            if (input_len < 1) {
                fprintf(stderr, "File name is too short!\n");
                fflush(stderr);
                return EXIT_FAILURE;
            } else if (input_len > (sizeof(filename)-1)) {
                fprintf(stderr, "File name is too long!\n");
                fflush(stderr);
                return EXIT_FAILURE;
            } else {
                strcpy(filename, &argv[argc][2]);
            }
        } else {
            fprintf(stderr, "Unknown option \"%s\"!\n", argv[argc]);
            fflush(stderr);
            return EXIT_FAILURE;
        }
    }

    if (strlen(filename) < 1) {
        fprintf(stderr, "File name must br provided! See -h for more info.\n");
        fflush(stderr);
        return EXIT_FAILURE;
    }
    struct stat st;
    if (stat(filename, &st)) {
        perror("File access error");
        return EXIT_FAILURE;
    }
    printf("--- File size: %" PRIu64 " bytes. ---\n", (uint64_t)st.st_size);
    fflush(stdout);

    PERSIMQ_set_debug_verbosity((debug_output > PRINT_DEBUG_ON) ? PERSIMQ_VERBOSITY_DEBUG :
                                debug_output ? PERSIMQ_VERBOSITY_INFO:
                                PERSIMQ_VERBOSITY_ERRORS_ONLY);
    T_PERSIMQ mq;

    if (!PERSIMQ_open(&mq, filename, st.st_size)) {
        perror("PERSIMQ_open error"); exit(EXIT_FAILURE);
    }
    uint8_t buf[256];
    int message_counter = 0;
    while (!PERSIMQ_is_empty(&mq)) {
        size_t mes_size = 0;
        if (!PERSIMQ_get(&mq, buf, sizeof(buf), &mes_size)) {
            perror("PERSIMQ_get error"); exit(EXIT_FAILURE);
        }
        printf("Message %d: [ ", ++message_counter);
        for (size_t i = 0; i < mes_size; i++) {
            printf("0x%02" PRIX8, buf[i]);
            if (i < (mes_size-1)) printf(", ");
        }
        printf(" ]\n");
        if (!PERSIMQ_pop(&mq)) {
            perror("PERSIMQ_pop error"); exit(EXIT_FAILURE);
        }
    }

    /*
    size_t total_size = 0;
    uint64_t read_count = 0;
    if (!PERSIMQ_get_all(&mq, buf, sizeof(buf), 4,
                    &total_size, &read_count)) {
        perror("PERSIMQ_get_all error"); exit(EXIT_FAILURE);
    }
    printf("+++ PERSIMQ_get_all() got %" PRIu64 " messages, %" PRIu64 " bytes total.\n", read_count, (uint64_t)total_size);
    */

    if (queue_clear) {
        if (!PERSIMQ_close(&mq)) {
            perror("PERSIMQ_close error"); exit(EXIT_FAILURE);
        }
    } else {
        if (!PERSIMQ_drop(&mq)) {
            perror("PERSIMQ_drop error"); exit(EXIT_FAILURE);
        }
    }

    printf("--- Processing complete! ---\n");
    fflush(stdout);
    return 0;
}
