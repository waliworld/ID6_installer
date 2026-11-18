/* The MOST UDS tunnel: one request to an ECU in, its UDS reply out. One DSI client per process; the DSI
 * loop runs on its own thread, the calls below on one caller thread. */
#ifndef MOST_LINK_H
#define MOST_LINK_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/* MOST_REPLY:   *resp holds the ECU's final reply, positive or negative.
   MOST_TIMEOUT: the tunnel is up but the ECU stayed silent (or pending past the 5 s cap).
   MOST_CLOSED:  DSI is down or closed; close and give up on this link.
   MOST_NOROUTE: the tunnel refused this request (outer 7F 31); the next request starts clean. */
enum most_rc { MOST_REPLY = 0, MOST_TIMEOUT = -1, MOST_CLOSED = -2, MOST_NOROUTE = -3 };

/* MOST_UP:         informationConnect seen for key 0x06F0.
   MOST_NO_TARGET:  the wait timed out after a registry without the key.
   MOST_NO_SERVER:  no client, a dropped link, or a timeout with no registry.
   MOST_NO_LIBRARY: the DSI library or one of its functions is missing; nothing was started. */
enum most_open_rc { MOST_UP = 0, MOST_NO_TARGET = -1, MOST_NO_SERVER = -2, MOST_NO_LIBRARY = -3 };

#define MOST_ECU_DME 0x12
#define MOST_INNER_MAX 512   /* longest inner reply taken off the wire */

/* Waits for the MOST client, registers the key, then waits for the target, each phase bounded by
   timeout_ms. timeout_ms is also most_request's wait. A failed open leaves the close to the caller. */
enum most_open_rc most_open(int timeout_ms);
/* Optional: once *running reads 0, most_open and most_request_wait stop waiting within 100 ms, as a
   timeout. NULL, the default, waits the full time. */
void most_set_running(const volatile sig_atomic_t *running);
/* uds_len is 1..16. An inner reply over min(resp_cap, 512) bytes is dropped as malformed, so that request
   times out. Waits timeout_ms for the first reply; a pending reply extends that up to the 5 s cap. */
enum most_rc most_request_wait(uint8_t ecu, const uint8_t *uds, size_t uds_len,
                               uint8_t *resp, size_t resp_cap, size_t *resp_len, unsigned timeout_ms);
/* most_request_wait with the open's timeout and a reply cap of 64. */
enum most_rc most_request(uint8_t ecu, const uint8_t *uds, size_t uds_len,
                          uint8_t *resp, size_t resp_cap, size_t *resp_len);
/* Unregisters the target, then the client, stops the DSI loop and joins its thread. The join is bounded
   (3 s); the unregisters are library sends without a timeout, so callers bound this with alarm().
   Safe twice and with nothing open. 0 when no DSI thread is left; -1 when it never stopped, and then
   later opens are refused. */
int most_close(void);

#endif
