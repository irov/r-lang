#ifndef R_STD_TLS_NATIVE_H
#define R_STD_TLS_NATIVE_H

/*
 * The native provider of std.tls (Library R-SLIB-TLS-0001..0005): a TLS engine over Mbed TLS 3.6
 * that exchanges records through memory. The R part of std.tls imports these functions through
 * the checked C boundary (Core R-FFI-0028..0044) and moves the records over a byte stream.
 *
 * A configuration and a session are opaque objects. Every function may be called from any
 * thread; a session serializes its calls with its own lock, and a configuration is frozen by
 * its first session and read only afterwards. A function that fails returns a negative status
 * and stores the Mbed TLS error code in *native when native is not NULL.
 */

#include <stddef.h>
#include <stdint.h>

#define R_STD_TLS_NATIVE_OK INT32_C(0)
/* The session needs records from the peer before it can continue. */
#define R_STD_TLS_NATIVE_WANT_INPUT INT32_C(1)
/* The peer ended the session with close_notify. */
#define R_STD_TLS_NATIVE_CLOSED INT32_C(2)

#define R_STD_TLS_NATIVE_ALLOCATION INT32_C(-1)
#define R_STD_TLS_NATIVE_INVALID_CERTIFICATE INT32_C(-2)
#define R_STD_TLS_NATIVE_INVALID_KEY INT32_C(-3)
#define R_STD_TLS_NATIVE_UNTRUSTED_CERTIFICATE INT32_C(-4)
#define R_STD_TLS_NATIVE_NAME_MISMATCH INT32_C(-5)
#define R_STD_TLS_NATIVE_EXPIRED_CERTIFICATE INT32_C(-6)
#define R_STD_TLS_NATIVE_HANDSHAKE_FAILED INT32_C(-7)
#define R_STD_TLS_NATIVE_PEER_ALERT INT32_C(-8)
#define R_STD_TLS_NATIVE_PROTOCOL_ERROR INT32_C(-9)
#define R_STD_TLS_NATIVE_INVALID_ARGUMENT INT32_C(-10)
#define R_STD_TLS_NATIVE_FROZEN INT32_C(-11)

/* A new configuration for a client (server == 0) or a server; NULL when memory is exhausted. */
void *r_std_tls_native_config_create(int32_t server);
/* Releases one reference; the configuration ends with the last of it and of its sessions. */
void r_std_tls_native_config_release(void *config);
/* Adds the certificates of a PEM text or of one DER certificate to the trusted authorities. */
int32_t r_std_tls_native_config_add_authority(void *config,
                                              const uint8_t *data,
                                              size_t length,
                                              int64_t *native);
/* Sets the certificate chain (PEM, or one DER certificate) and its private key (PEM or DER). */
int32_t r_std_tls_native_config_set_identity(void *config,
                                             const uint8_t *chain,
                                             size_t chain_length,
                                             const uint8_t *key,
                                             size_t key_length,
                                             int64_t *native);
/* Adds an application protocol name for ALPN, 1 to 255 bytes, at most 8 names. */
int32_t r_std_tls_native_config_add_protocol(void *config, const uint8_t *name, size_t length);
/* Requires (nonzero) or skips (zero) the verification of the peer certificate chain. */
int32_t r_std_tls_native_config_set_verification(void *config, int32_t required);

/* A session of the configuration; a client session names the server it expects. */
void *r_std_tls_native_session_create(void *config,
                                      const uint8_t *server_name,
                                      size_t server_name_length,
                                      int32_t *status,
                                      int64_t *native);
void r_std_tls_native_session_release(void *session);
/* Appends records received from the peer. */
int32_t r_std_tls_native_session_feed(void *session, const uint8_t *data, size_t length);
/* Moves up to capacity bytes of records for the peer into target and returns their count. */
size_t r_std_tls_native_session_take(void *session, uint8_t *target, size_t capacity);
/* The count of record bytes waiting for the peer. */
size_t r_std_tls_native_session_pending(void *session);
/* Advances the handshake: OK when complete, WANT_INPUT, or a failure. */
int32_t r_std_tls_native_session_handshake(void *session, int64_t *native);
/* Reads application data: OK with *count > 0, WANT_INPUT, CLOSED or a failure. */
int32_t r_std_tls_native_session_read(void *session,
                                      uint8_t *target,
                                      size_t capacity,
                                      size_t *count,
                                      int64_t *native);
/* Encrypts all of data into records for the peer. */
int32_t r_std_tls_native_session_write(void *session,
                                       const uint8_t *data,
                                       size_t length,
                                       int64_t *native);
/* Queues close_notify for the peer. */
int32_t r_std_tls_native_session_close(void *session, int64_t *native);
/* The negotiated ALPN name and its length in *length, NULL and zero for none. The name is one
   of the protocols of the configuration, which the session keeps alive, so it stays valid and
   unchanged until the session is released. */
const uint8_t *r_std_tls_native_session_protocol(void *session, size_t *length);
/* 12 for TLS 1.2, 13 for TLS 1.3, zero before the handshake completes. */
int32_t r_std_tls_native_session_version(void *session);

#endif
