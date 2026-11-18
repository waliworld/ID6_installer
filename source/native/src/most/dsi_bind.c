/* Binds the DSI functions. On the head unit the library is opened only when the worker falls back to MOST,
 * so a missing or different library leaves MOST unavailable instead of stopping the worker from loading.
 * The DSI service broker library uses libsocket functions without listing libsocket as a dependency, so the
 * worker itself must link libsocket. */
#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "dsi_abi.h"

struct most_dsi most_dsi;
static int bound;   /* 0 not tried, 1 bound, -1 failed */

#ifdef MOST_HOST
int most_dsi_bind(void)
{
    if (bound) return 0;
    if (most_fake_bind() != 0) return -1;   /* the fake may be told to bind later */
    most_dsi.Init = DSIInit;
    most_dsi.ClientInit = DSIClientInit;
    most_dsi.ClientRun = DSIClientRun;
    most_dsi.ClientStop = DSIClientStop;
    most_dsi.ClientFree = DSIClientFree;
    most_dsi.ClientSendRegister = DSIClientSendRegister;
    most_dsi.ClientSetNotification = DSIClientSetNotification;
    most_dsi.ClientSendRequest = DSIClientSendRequest;
    most_dsi.CreateId = DSICreateId;
    most_dsi.Open = DSIOpen;
    most_dsi.Close = DSIClose;
    most_dsi.Write = DSIWrite;
    most_dsi.Read = DSIRead;
    most_dsi.WriteString2 = DSIWriteString2;
    bound = 1;
    return 0;
}
#else
#include <dlfcn.h>
#include <stdio.h>

static const char *const names[] = {
    "DSIInit", "DSIClientInit", "DSIClientRun", "DSIClientStop", "DSIClientFree", "DSIClientSendRegister",
    "DSIClientSetNotification", "DSIClientSendRequest", "DSICreateId", "DSIOpen", "DSIClose", "DSIWrite",
    "DSIRead", "DSIWriteString2"
};

int most_dsi_bind(void)
{
    void *lib, *f;
    unsigned i;
    if (bound) return bound > 0 ? 0 : -1;
    bound = -1;
    lib = dlopen("libsys_dsi_base.cpp-ne.so.4", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) lib = dlopen("/opt/sys/lib/libsys_dsi_base.cpp-ne.so.4", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "MOST unavailable: %s\n", dlerror()); return -1; }
    typedef char table_matches_names[sizeof most_dsi == sizeof names ? 1 : -1];
    (void)sizeof(table_matches_names);
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        f = dlsym(lib, names[i]);
        if (!f) {
            fprintf(stderr, "MOST unavailable: no %s\n", names[i]);
            memset(&most_dsi, 0, sizeof most_dsi);
            dlclose(lib);
            return -1;
        }
        /* Function and data pointers have one size on this target; copy the bytes into the table slot. */
        memcpy((char *)&most_dsi + i * sizeof f, &f, sizeof f);
    }
    bound = 1;   /* never closed: the DSI thread may outlive any later failure */
    return 0;
}
#endif
