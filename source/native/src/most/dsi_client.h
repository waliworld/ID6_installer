/* A small DSYSMOSTServer client, one per process. Callbacks run on the DSI thread. The send and register
 * calls may come from any thread; sends are serialized. most_dsi_bind() must have succeeded first. */
#ifndef MOST_DSI_CLIENT_H
#define MOST_DSI_CLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*connected)(void *u);
    void (*disconnected)(void *u);
    void (*most_client)(void *u, uint32_t client_id, uint16_t device_id);
    void (*registry)(void *u, const uint16_t *pairs, int n_pairs);   /* (device, key) pairs */
    void (*target_up)(void *u, uint16_t device);                     /* informationConnect for our key */
    void (*target_down)(void *u);                                    /* informationDisconnect for our key */
    void (*report)(void *u, uint16_t fkt, uint8_t op, const uint8_t *p, size_t n);
} dsi_handlers_t;

enum { MOST_LOG_E = 1, MOST_LOG_W, MOST_LOG_I, MOST_LOG_D, MOST_LOG_T };
void most_log_level(int level);      /* 0, the default, is silent */

int  dsi_client_run(const char *role, const dsi_handlers_t *h, void *u); /* blocks until stopped */
void dsi_client_stop(void);
void dsi_client_set_name(const char *name);  /* request 0x0F; at most 31 bytes; default "boostdeck" */
void dsi_client_set_target(uint16_t key, uint8_t fblock, uint8_t inst);   /* key = fblock<<8 | inst */
int  dsi_register_target(void);      /* request 0x01 */
int  dsi_unregister_target(void);    /* request 0x05 */
int  dsi_unregister_client(void);    /* request 0x10 */
/* Request 0x02 on the control channel. Needs the target up. */
int  dsi_send_fn(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const uint8_t *payload, size_t n);

#endif
