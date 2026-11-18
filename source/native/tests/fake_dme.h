/* A DME behind the fake DSI, answering 22 <DID> reads on its own thread. Host only.
 *
 * A "request" is one tunnel send (DSI request 0x02), numbered from 1 since fake_dme_start. A hex prefix is
 * uppercase hex with no spaces, such as "224201". Call fake_dsi_reset, then fake_dme_start, then setters. */
#ifndef FAKE_DME_H
#define FAKE_DME_H

#include <stddef.h>
#include <stdint.h>
#include "fake_dsi.h"

enum fake_handshake { FAKE_UP, FAKE_NO_TARGET, FAKE_NO_SERVER };

void fake_dme_start(enum fake_handshake h);
void fake_dme_stop(void);                                    /* safe with nothing started */
void fake_dme_plain(uint16_t did, const uint8_t *data, size_t n);       /* 22 did -> 62 did data */
void fake_dme_raw(const char *hex_request_prefix, const uint8_t *reply, size_t n);   /* verbatim reply */
void fake_dme_refuse(const char *hex_prefix, uint8_t nrc, int times);   /* 0 times = always */
void fake_dme_pending(int n);                                /* send n inner 7F 22 78 before each reply */
void fake_dme_silent_window(int from_request, int n_requests);          /* n_requests < 0 = forever */
void fake_dme_tunnel_refused(int on);                        /* outer 7F 31 31 */
void fake_dme_drop_link_after(int n_requests);               /* the link is lost at request n, once */
void fake_dme_hold_ms(int ms);       /* each reply, and the DSI thread's stop, take this long; 0 = 1 ms */
int  fake_dme_count(const char *hex_prefix);
int  fake_dme_overlaps(void);                                /* a request arrived before the last reply */

void fake_dme_give_client(void);                             /* responseMOSTClient */
void fake_dme_give_target_up(void);                          /* informationConnect listing key 0x06F0 */
void fake_dme_give_registry_without_key(void);
void fake_dme_hs_up(void);                                   /* connect, client, wait for register, target */
void fake_dme_hs_no_target(void);                            /* the same, then a registry without the key */
void fake_dme_set_handshake(void (*hs)(void));               /* NULL: no handshake at all */
void fake_dme_run_hook(void *app);                           /* the handshake, then park until stopped */
void fake_dme_deliver_tunnel_reply(uint16_t fkt, const uint8_t *inner, size_t ilen);
void fake_dme_deliver_payload(uint16_t fkt, const uint8_t *payload, size_t n);

#endif
