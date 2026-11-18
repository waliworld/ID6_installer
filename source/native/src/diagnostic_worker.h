#ifndef DIAGNOSTIC_WORKER_H
#define DIAGNOSTIC_WORKER_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>

static inline pid_t launch(const char *const *args,int output) {
    pid_t child=-1;
    // /opt/sys/lib holds the DSI libraries the workers' MOST fallback opens.
    char library_path[] = "LD_LIBRARY_PATH=/lib:/usr/lib:/boot/lib:/opt/sys/lib";
    char *environment[] = {library_path, 0};
    int error;
#ifdef __QNX__
    // Explicitly pass only stdin, stdout and stderr to the diagnostic process.
    int null = open("/dev/null", O_RDWR);
    error = null < 0 ? errno : 0;
    if (!error) {
        int descriptors[] = {null, output < 0 ? null : output, null};
        struct inheritance flags;
        memset(&flags, 0, sizeof flags);
        flags.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
        sigfillset(&flags.sigdefault);
        sigemptyset(&flags.sigmask);
        child = spawn(args[0], 3, descriptors, &flags,
                           const_cast<char *const *>(args), environment);
        if (child < 0) error = errno;
        close(null);
    }
#else
    posix_spawn_file_actions_t actions;
    error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        if (!error) {
            if (output < 0) error=posix_spawn_file_actions_addopen(&actions,1,"/dev/null",O_WRONLY,0);
            else error=posix_spawn_file_actions_adddup2(&actions,output,1);
        }
        if (!error) error = posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0);
        if (!error)
            error = posix_spawn(&child, args[0], &actions, 0,
                                const_cast<char *const *>(args), environment);
        posix_spawn_file_actions_destroy(&actions);
    }
#endif
    return error ? -1 : child;
}

#endif
