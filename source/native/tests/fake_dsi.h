/* Host stand-in for the DSI library: records writes and requests, replays scripted events. */
#ifndef FAKE_DSI_H
#define FAKE_DSI_H

#include <stddef.h>
#include <stdint.h>

#define FAKE_MAX_WRITES 16
#define FAKE_MAX_REQS   1024
#define FAKE_WRITE_BYTES 64

typedef struct { int size, align; uint8_t bytes[FAKE_WRITE_BYTES]; } fake_write_t;
typedef struct { uint32_t id, session; int n; fake_write_t w[FAKE_MAX_WRITES]; } fake_req_t;

extern fake_req_t fake_req[FAKE_MAX_REQS];
extern int fake_nreq;
extern uint32_t fake_reg_ids[16];
extern int fake_reg_n;
extern uint32_t fake_reg_session;
extern uint32_t fake_notify[8];
extern int fake_nnotify;
extern char fake_role[64];
extern int fake_major, fake_minor;
extern int fake_stop_calls, fake_free_calls, fake_opens, fake_closes;
extern int fake_fail_write_at;     /* nth DSIWrite of a stream fails (1-based); 0 = never */
extern int fake_send_rc, fake_run_rc, fake_reg_rc, fake_notify_rc;  /* return values; default 0 */
extern unsigned fake_cb_offs[8];   /* offsets passed to the callback setter, in order */
extern int fake_ncb;
extern int fake_cb_off_ok;         /* callbacks were stored only at the four documented offsets */
extern int fake_client_inits;      /* DSIClientInit calls since the last fake_dsi_reset */
extern int fake_stop_nreq;         /* fake_nreq at the first DSIClientStop; -1 if none */
extern int fake_run_blocks_stops, fake_run_blocks_alarm;   /* the mask of the thread in the last DSIClientRun */

void fake_dsi_reset(void);
void fake_dsi_set_run_hook(void (*hook)(void *app));
/* Called after recording each request, on the sender's thread and with the DSI client's send lock held. */
void fake_dsi_set_send_hook(void (*hook)(const fake_req_t *r));
int  fake_dsi_req_count(uint32_t id);   /* recorded requests with this id */
void fake_dsi_connect(void);
void fake_dsi_disconnect(void);
/* Calls the stored response callback with an event of this type and id, and a stream that reads payload. */
void fake_dsi_deliver(uint32_t type, uint32_t id, const void *payload, size_t n);

#endif
