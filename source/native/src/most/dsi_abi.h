/* DSI has no public header. The layout and prototypes below are reconstructed from callers of
 * libsys_dsi_base. The library is opened at run time (dsi_bind.c), so the worker never needs it to start. */
#ifndef MOST_DSI_ABI_H
#define MOST_DSI_ABI_H

#include <stdint.h>

#define DSI_APP_BYTES 460
#define DSI_STREAM_BYTES 64
#define DSI_APP_ON_CONNECTED    0xCC   /* void (*)(void *app) */
#define DSI_APP_ON_DISCONNECTED 0xD0   /* void (*)(void *app) */
#define DSI_APP_ON_RESPONSE     0xD4   /* void (*)(void *app, void *stream, const void *event) */
#define DSI_APP_ON_EXIT_LOOP    0xDC   /* void (*)(void *app) */
#define DSI_EVENT_TYPE 4               /* u32; 0x201 invalid; 0x204,0x205 rejected */
#define DSI_EVENT_ID   8               /* u32 */
#define DSI_STREAM_ERROR 0x14          /* int; non-zero after a failed read or write */

struct most_dsi {
    void (*Init)(void);
    int (*ClientInit)(void *app, const char *ifname, const char *role, int major, int minor);
    int (*ClientRun)(void *app);
    int (*ClientStop)(void *app, int unused);
    void (*ClientFree)(void *app);
    int (*ClientSendRegister)(void *app, const uint32_t *ids, int n, uint32_t session);
    int (*ClientSetNotification)(void *app, uint32_t attr);
    int (*ClientSendRequest)(void *app, uint32_t id, uint32_t session, void *stream);
    uint32_t (*CreateId)(void);
    void (*Open)(void *stream);
    void (*Close)(void *stream);
    int (*Write)(void *stream, const void *p, int size, int align);
    int (*Read)(void *stream, void *p, int size, int align);
    int (*WriteString2)(void *stream, const char **s);
};

extern struct most_dsi most_dsi;
/* Fills most_dsi once. 0 when every function was found; -1 (and an empty table) otherwise. */
int most_dsi_bind(void);

#ifdef MOST_HOST
/* Host builds link a fake DSI with these names, and a hook that decides whether binding succeeds. */
int most_fake_bind(void);
void fake_dsi_set_cb(void *app, unsigned off, void *fn);
void DSIInit(void);
int DSIClientInit(void *app, const char *ifname, const char *role, int major, int minor);
int DSIClientRun(void *app);
int DSIClientStop(void *app, int unused);
void DSIClientFree(void *app);
int DSIClientSendRegister(void *app, const uint32_t *ids, int n, uint32_t session);
int DSIClientSetNotification(void *app, uint32_t attr);
int DSIClientSendRequest(void *app, uint32_t id, uint32_t session, void *stream);
uint32_t DSICreateId(void);
void DSIOpen(void *stream);
void DSIClose(void *stream);
int DSIWrite(void *stream, const void *p, int size, int align);
int DSIRead(void *stream, void *p, int size, int align);
int DSIWriteString2(void *stream, const char **s);
#endif

#endif
