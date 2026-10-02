/* The native provider of std.tls (Library R-SLIB-TLS-0001..0005): sessions of a client and a
   server exchange records in memory; the certificates are the fixtures of tests/fixtures/tls. */
#include "r_std_tls_native.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int r_tls_failures;

#define R_TLS_CHECK(condition)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            ++r_tls_failures;                                                                     \
        }                                                                                         \
    } while (0)

typedef struct RTlsFile {
    uint8_t *data;
    size_t length;
} RTlsFile;

static const char *r_tls_directory;

static RTlsFile r_tls_read(const char *name) {
    RTlsFile file = {NULL, 0U};
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/%s", r_tls_directory, name);
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    (void)fseek(stream, 0L, SEEK_END);
    const long size = ftell(stream);
    (void)fseek(stream, 0L, SEEK_SET);
    file.data = malloc((size_t)size);
    file.length = fread(file.data, 1U, (size_t)size, stream);
    (void)fclose(stream);
    return file;
}

static void *r_tls_client_config(const char *authority) {
    void *config = r_std_tls_native_config_create(0);
    RTlsFile file = r_tls_read(authority);
    int64_t native = 0;
    R_TLS_CHECK(config != NULL);
    R_TLS_CHECK(r_std_tls_native_config_add_authority(config, file.data, file.length, &native) ==
                R_STD_TLS_NATIVE_OK);
    free(file.data);
    return config;
}

static void *r_tls_server_config(const char *certificate, const char *key) {
    void *config = r_std_tls_native_config_create(1);
    RTlsFile chain = r_tls_read(certificate);
    RTlsFile secret = r_tls_read(key);
    int64_t native = 0;
    R_TLS_CHECK(config != NULL);
    R_TLS_CHECK(r_std_tls_native_config_set_identity(
                    config, chain.data, chain.length, secret.data, secret.length, &native) ==
                R_STD_TLS_NATIVE_OK);
    free(chain.data);
    free(secret.data);
    return config;
}

/* Moves every pending record of one session to the other. */
static void r_tls_pump(void *from, void *to) {
    uint8_t chunk[97];
    for (;;) {
        const size_t count = r_std_tls_native_session_take(from, chunk, sizeof(chunk));
        if (count == 0U) {
            return;
        }
        R_TLS_CHECK(r_std_tls_native_session_feed(to, chunk, count) == R_STD_TLS_NATIVE_OK);
    }
}

/* Runs both handshakes to completion or to the first failure of either side. */
static void r_tls_handshake(void *client, void *server, int32_t *client_status, int32_t *server_status) {
    int64_t native = 0;
    *client_status = R_STD_TLS_NATIVE_WANT_INPUT;
    *server_status = R_STD_TLS_NATIVE_WANT_INPUT;
    for (int round = 0; round < 32; ++round) {
        if (*client_status == R_STD_TLS_NATIVE_WANT_INPUT) {
            *client_status = r_std_tls_native_session_handshake(client, &native);
        }
        r_tls_pump(client, server);
        if (*server_status == R_STD_TLS_NATIVE_WANT_INPUT) {
            *server_status = r_std_tls_native_session_handshake(server, &native);
        }
        r_tls_pump(server, client);
        if ((*client_status != R_STD_TLS_NATIVE_WANT_INPUT) &&
            (*server_status != R_STD_TLS_NATIVE_WANT_INPUT)) {
            return;
        }
        if ((*client_status < 0) || (*server_status < 0)) {
            /* One side failed; let the other one read the alert once more. */
            if (*client_status == R_STD_TLS_NATIVE_WANT_INPUT) {
                *client_status = r_std_tls_native_session_handshake(client, &native);
            }
            if (*server_status == R_STD_TLS_NATIVE_WANT_INPUT) {
                *server_status = r_std_tls_native_session_handshake(server, &native);
            }
            return;
        }
    }
}

static void r_tls_exchange(const char *authority, const char *name, bool alpn) {
    void *client_config = r_tls_client_config(authority);
    void *server_config = r_tls_server_config("server.pem", "server_key.pem");
    int32_t status = 0;
    int64_t native = 0;
    if (alpn) {
        R_TLS_CHECK(r_std_tls_native_config_add_protocol(client_config, (const uint8_t *)"h2", 2U) == 0);
        R_TLS_CHECK(r_std_tls_native_config_add_protocol(client_config, (const uint8_t *)"http/1.1", 8U) == 0);
        R_TLS_CHECK(r_std_tls_native_config_add_protocol(server_config, (const uint8_t *)"http/1.1", 8U) == 0);
    }
    void *client = r_std_tls_native_session_create(
        client_config, (const uint8_t *)name, strlen(name), &status, &native);
    R_TLS_CHECK((client != NULL) && (status == R_STD_TLS_NATIVE_OK));
    void *server = r_std_tls_native_session_create(server_config, NULL, 0U, &status, &native);
    R_TLS_CHECK((server != NULL) && (status == R_STD_TLS_NATIVE_OK));
    /* The configurations are frozen by their sessions and released by the last of them. */
    R_TLS_CHECK(r_std_tls_native_config_set_verification(client_config, 0) == R_STD_TLS_NATIVE_FROZEN);
    r_std_tls_native_config_release(client_config);
    r_std_tls_native_config_release(server_config);
    int32_t client_status;
    int32_t server_status;
    r_tls_handshake(client, server, &client_status, &server_status);
    R_TLS_CHECK(client_status == R_STD_TLS_NATIVE_OK);
    R_TLS_CHECK(server_status == R_STD_TLS_NATIVE_OK);
    R_TLS_CHECK(r_std_tls_native_session_version(client) == 13);
    size_t protocol_length = 1U;
    const uint8_t *protocol = r_std_tls_native_session_protocol(client, &protocol_length);
    R_TLS_CHECK(alpn ? ((protocol != NULL) && (protocol_length == 8U) &&
                        (memcmp(protocol, "http/1.1", 8U) == 0))
                     : ((protocol == NULL) && (protocol_length == 0U)));
    /* The name stays where it is, in the configuration that the session keeps alive. */
    size_t again_length = 0U;
    R_TLS_CHECK(r_std_tls_native_session_protocol(client, &again_length) == protocol);
    R_TLS_CHECK(again_length == protocol_length);
    /* Application data both ways; a large write spans several records. */
    static uint8_t payload[40000];
    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)(index * 7U);
    }
    R_TLS_CHECK(r_std_tls_native_session_write(client, payload, sizeof(payload), &native) == 0);
    R_TLS_CHECK(r_std_tls_native_session_pending(client) > sizeof(payload));
    r_tls_pump(client, server);
    size_t received = 0U;
    bool same = true;
    for (int attempt = 0; (attempt < 100) && (received < sizeof(payload)); ++attempt) {
        uint8_t chunk[1000];
        size_t count = 0U;
        status = r_std_tls_native_session_read(server, chunk, sizeof(chunk), &count, &native);
        R_TLS_CHECK((status == R_STD_TLS_NATIVE_OK) || (status == R_STD_TLS_NATIVE_WANT_INPUT));
        same = same && (memcmp(chunk, payload + received, count) == 0);
        received += count;
    }
    R_TLS_CHECK((received == sizeof(payload)) && same);
    R_TLS_CHECK(r_std_tls_native_session_write(server, (const uint8_t *)"pong", 4U, &native) == 0);
    r_tls_pump(server, client);
    uint8_t reply[8];
    size_t count = 0U;
    do {
        status = r_std_tls_native_session_read(client, reply, sizeof(reply), &count, &native);
    } while ((status == R_STD_TLS_NATIVE_OK) && (count == 0U));
    R_TLS_CHECK((status == R_STD_TLS_NATIVE_OK) && (count == 4U) && (memcmp(reply, "pong", 4U) == 0));
    /* Nothing more to read, then close_notify ends the session for the peer. */
    status = r_std_tls_native_session_read(server, reply, sizeof(reply), &count, &native);
    R_TLS_CHECK((status == R_STD_TLS_NATIVE_WANT_INPUT) && (count == 0U));
    R_TLS_CHECK(r_std_tls_native_session_close(client, &native) == R_STD_TLS_NATIVE_OK);
    r_tls_pump(client, server);
    status = r_std_tls_native_session_read(server, reply, sizeof(reply), &count, &native);
    R_TLS_CHECK(status == R_STD_TLS_NATIVE_CLOSED);
    r_std_tls_native_session_release(client);
    r_std_tls_native_session_release(server);
}

static void r_tls_rejected(const char *authority, const char *certificate, const char *name, int32_t expected) {
    void *client_config = r_tls_client_config(authority);
    void *server_config = r_tls_server_config(certificate, "server_key.pem");
    int32_t status = 0;
    int64_t native = 0;
    void *client = r_std_tls_native_session_create(
        client_config, (const uint8_t *)name, strlen(name), &status, &native);
    void *server = r_std_tls_native_session_create(server_config, NULL, 0U, &status, &native);
    int32_t client_status;
    int32_t server_status;
    r_tls_handshake(client, server, &client_status, &server_status);
    R_TLS_CHECK(client_status == expected);
    /* The server sees the end of the handshake as a failure of its own. */
    R_TLS_CHECK((server_status < 0) || (server_status == R_STD_TLS_NATIVE_WANT_INPUT));
    /* A failed session keeps its failure. */
    uint8_t byte;
    size_t count;
    R_TLS_CHECK(r_std_tls_native_session_read(client, &byte, 1U, &count, &native) == expected);
    R_TLS_CHECK(native < 0);
    r_std_tls_native_session_release(client);
    r_std_tls_native_session_release(server);
    r_std_tls_native_config_release(client_config);
    r_std_tls_native_config_release(server_config);
}

static void r_tls_configuration_errors(void) {
    void *config = r_std_tls_native_config_create(0);
    int64_t native = 0;
    int32_t status = 0;
    R_TLS_CHECK(r_std_tls_native_config_add_authority(config, (const uint8_t *)"not a certificate", 17U, &native) ==
                R_STD_TLS_NATIVE_INVALID_CERTIFICATE);
    R_TLS_CHECK(r_std_tls_native_config_add_authority(config, NULL, 0U, &native) ==
                R_STD_TLS_NATIVE_INVALID_CERTIFICATE);
    R_TLS_CHECK(r_std_tls_native_config_add_protocol(config, (const uint8_t *)"", 0U) ==
                R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    for (int index = 0; index < 8; ++index) {
        R_TLS_CHECK(r_std_tls_native_config_add_protocol(config, (const uint8_t *)"x", 1U) == 0);
    }
    R_TLS_CHECK(r_std_tls_native_config_add_protocol(config, (const uint8_t *)"y", 1U) ==
                R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    /* A verifying client needs the name of its server. */
    R_TLS_CHECK(r_std_tls_native_session_create(config, NULL, 0U, &status, &native) == NULL);
    R_TLS_CHECK(status == R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    r_std_tls_native_config_release(config);
    void *server = r_std_tls_native_config_create(1);
    RTlsFile chain = r_tls_read("server.pem");
    RTlsFile other = r_tls_read("other_authority.pem");
    RTlsFile key = r_tls_read("server_key.der");
    R_TLS_CHECK(r_std_tls_native_config_set_identity(server, chain.data, chain.length, (const uint8_t *)"junk", 4U, &native) ==
                R_STD_TLS_NATIVE_INVALID_KEY);
    /* The key of another certificate does not match. */
    R_TLS_CHECK(r_std_tls_native_config_set_identity(server, other.data, other.length, key.data, key.length, &native) ==
                R_STD_TLS_NATIVE_INVALID_KEY);
    /* A server needs an identity. */
    R_TLS_CHECK(r_std_tls_native_session_create(server, NULL, 0U, &status, &native) == NULL);
    R_TLS_CHECK(status == R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    /* A DER key with its certificate. */
    R_TLS_CHECK(r_std_tls_native_config_set_identity(server, chain.data, chain.length, key.data, key.length, &native) == 0);
    free(chain.data);
    free(other.data);
    free(key.data);
    void *session = r_std_tls_native_session_create(server, NULL, 0U, &status, &native);
    R_TLS_CHECK((session != NULL) && (status == 0));
    uint8_t byte;
    size_t count;
    /* Reading and writing need a complete handshake. */
    R_TLS_CHECK(r_std_tls_native_session_read(session, &byte, 1U, &count, &native) == R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    R_TLS_CHECK(r_std_tls_native_session_write(session, &byte, 1U, &native) == R_STD_TLS_NATIVE_INVALID_ARGUMENT);
    R_TLS_CHECK(r_std_tls_native_session_version(session) == 0);
    /* Garbage instead of a ClientHello fails the handshake of the server. */
    R_TLS_CHECK(r_std_tls_native_session_feed(session, (const uint8_t *)"GET / HTTP/1.1\r\n\r\n", 18U) == 0);
    R_TLS_CHECK(r_std_tls_native_session_handshake(session, &native) < 0);
    r_std_tls_native_session_release(session);
    r_std_tls_native_config_release(server);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        (void)fprintf(stderr, "usage: r_library_tls_native_tests FIXTURE_DIRECTORY\n");
        return 2;
    }
    r_tls_directory = argv[1];
    r_tls_exchange("authority.pem", "localhost", true);
    r_tls_exchange("authority.der", "127.0.0.1", false);
    r_tls_rejected("authority.pem", "server.pem", "example.com", R_STD_TLS_NATIVE_NAME_MISMATCH);
    r_tls_rejected("other_authority.pem", "server.pem", "localhost", R_STD_TLS_NATIVE_UNTRUSTED_CERTIFICATE);
    r_tls_rejected("authority.pem", "expired.pem", "localhost", R_STD_TLS_NATIVE_EXPIRED_CERTIFICATE);
    r_tls_configuration_errors();
    if (r_tls_failures != 0) {
        (void)fprintf(stderr, "%d TLS checks failed\n", r_tls_failures);
        return 1;
    }
    (void)printf("TLS native provider: ok\n");
    return 0;
}
