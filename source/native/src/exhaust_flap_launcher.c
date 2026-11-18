#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef EXHAUST_FLAP_DIR
#define EXHAUST_FLAP_DIR "/var/opt/hmi/data/ID5/Plugins/ExhaustFlap"
#endif

int main(int argc, char *argv[])
{
    int host_arg = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0) {
            if (host_arg || i + 1 == argc)
                return 2;
            host_arg = ++i;
        }
    }
    const char *directory = EXHAUST_FLAP_DIR;
    size_t length = strlen(directory);
    if (argc > 0 && argv[0][0] == '/') {
        directory = argv[0];
        length = (size_t)(strrchr(directory, '/') - directory);
    }
    enum { path_capacity = 256 };
    char worker[path_capacity], settings[path_capacity];
    if (length >= sizeof(worker))
        return 2;
    if (snprintf(worker, sizeof(worker), "%.*s/exhaust-flap-worker-qnx",
                 (int)length, directory) >= (int)sizeof(worker) ||
        snprintf(settings, sizeof(settings), "%.*s/gateway-ip.txt",
                 (int)length, directory) >= (int)sizeof(settings))
        return 2;

    char gateway[16] = "169.254.21.3";
    if (host_arg) {
        FILE *input = fopen(settings, "r");
        if (input) {
            char candidate[16];
            struct in_addr address;
            if (fscanf(input, "%15s", candidate) == 1 &&
                inet_pton(AF_INET, candidate, &address) == 1)
                strcpy(gateway, candidate);
            fclose(input);
        }
        argv[host_arg] = gateway;
    }
    argv[0] = worker;

    execv(worker, argv);
    perror(worker);
    return 1;
}
