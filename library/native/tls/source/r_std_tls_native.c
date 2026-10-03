#include "r_std_tls_native.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/psa_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define R_STD_TLS_NATIVE_PROTOCOLS 8U

typedef struct RStdTlsNativeConfig {
    atomic_size_t references;
    pthread_mutex_t lock;
    bool frozen;
    bool server;
    bool verification;
    bool has_identity;
    mbedtls_ssl_config ssl;
    mbedtls_x509_crt authorities;
    bool has_authorities;
    mbedtls_x509_crt chain;
    mbedtls_pk_context key;
    size_t protocol_count;
    char protocol_names[R_STD_TLS_NATIVE_PROTOCOLS][256];
    const char *protocols[R_STD_TLS_NATIVE_PROTOCOLS + 1U];
} RStdTlsNativeConfig;

typedef struct RStdTlsNativeBuffer {
    uint8_t *data;
    size_t start;
    size_t length;
    size_t capacity;
} RStdTlsNativeBuffer;

typedef struct RStdTlsNativeSession {
    pthread_mutex_t lock;
    RStdTlsNativeConfig *config;
    mbedtls_ssl_context ssl;
    RStdTlsNativeBuffer input;
    RStdTlsNativeBuffer output;
    bool output_failed;
    int32_t failure;
    int64_t failure_native;
    bool handshake_done;
} RStdTlsNativeSession;

/* The objects of the provider come from the hosted allocator of the runtime, like every other
   allocation of the standard library; Mbed TLS allocates its own state with the C library.
   Outside a hosted program, as in the tests of the provider, an allocator of its own serves. */
static RRuntimeAllocator r_std_tls_native_local_allocator;
static pthread_once_t r_std_tls_native_allocator_once = PTHREAD_ONCE_INIT;

static void r_std_tls_native_allocator_start(void) {
    r_runtime_allocator_initialize(&r_std_tls_native_local_allocator);
}

static RRuntimeAllocator *r_std_tls_native_allocator(void) {
    RRuntimeAllocator *hosted = r_runtime_hosted_allocator();
    if (hosted != NULL) {
        return hosted;
    }
    (void)pthread_once(&r_std_tls_native_allocator_once, r_std_tls_native_allocator_start);
    return &r_std_tls_native_local_allocator;
}

static void *r_std_tls_native_allocate(size_t size) {
    void *allocation = NULL;
    if (r_runtime_allocator_allocate(
            r_std_tls_native_allocator(), size, alignof(max_align_t), &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    return allocation;
}

static void *r_std_tls_native_zeroed(size_t size) {
    void *allocation = r_std_tls_native_allocate(size);
    if (allocation != NULL) {
        (void)memset(allocation, 0, size);
    }
    return allocation;
}

static void *r_std_tls_native_grow(void *allocation, size_t old_size, size_t new_size) {
    void *result = NULL;
    if (allocation == NULL) {
        return r_std_tls_native_allocate(new_size);
    }
    if (r_runtime_allocator_reallocate(r_std_tls_native_allocator(),
                                       allocation,
                                       old_size,
                                       new_size,
                                       alignof(max_align_t),
                                       &result) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    return result;
}

static void r_std_tls_native_free(void *allocation) {
    if (allocation != NULL) {
        r_runtime_allocator_deallocate(allocation, alignof(max_align_t));
    }
}

static pthread_once_t r_std_tls_native_once = PTHREAD_ONCE_INIT;
static psa_status_t r_std_tls_native_psa_status = PSA_ERROR_GENERIC_ERROR;

static void r_std_tls_native_start(void) {
    r_std_tls_native_psa_status = psa_crypto_init();
}

static bool r_std_tls_native_ready(void) {
    return (pthread_once(&r_std_tls_native_once, r_std_tls_native_start) == 0) &&
           (r_std_tls_native_psa_status == PSA_SUCCESS);
}

static void r_std_tls_native_store(int64_t *native, int value) {
    if (native != NULL) {
        *native = (int64_t)value;
    }
}

static bool r_std_tls_native_reserve(RStdTlsNativeBuffer *buffer, size_t extra) {
    if (buffer->start != 0U) {
        (void)memmove(buffer->data, buffer->data + buffer->start, buffer->length);
        buffer->start = 0U;
    }
    if (extra > SIZE_MAX - buffer->length) {
        return false;
    }
    if (buffer->length + extra > buffer->capacity) {
        size_t capacity = buffer->capacity != 0U ? buffer->capacity : 4096U;
        while (capacity < buffer->length + extra) {
            if (capacity > SIZE_MAX / 2U) {
                return false;
            }
            capacity *= 2U;
        }
        uint8_t *data = r_std_tls_native_grow(buffer->data, buffer->capacity, capacity);
        if (data == NULL) {
            return false;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    return true;
}

static int r_std_tls_native_send(void *context, const unsigned char *data, size_t length) {
    RStdTlsNativeSession *session = context;
    if ((length > (size_t)INT32_MAX) || !r_std_tls_native_reserve(&session->output, length)) {
        session->output_failed = true;
        return MBEDTLS_ERR_SSL_ALLOC_FAILED;
    }
    (void)memcpy(session->output.data + session->output.length, data, length);
    session->output.length += length;
    return (int)length;
}

static int r_std_tls_native_receive(void *context, unsigned char *data, size_t length) {
    RStdTlsNativeSession *session = context;
    if (session->input.length == 0U) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (length > session->input.length) {
        length = session->input.length;
    }
    if (length > (size_t)INT32_MAX) {
        length = (size_t)INT32_MAX;
    }
    (void)memcpy(data, session->input.data + session->input.start, length);
    session->input.start += length;
    session->input.length -= length;
    if (session->input.length == 0U) {
        session->input.start = 0U;
    }
    return (int)length;
}

/* A PEM text is parsed with its terminating zero; any other content is one DER object. */
static bool r_std_tls_native_is_pem(const uint8_t *data, size_t length) {
    static const char marker[] = "-----BEGIN ";
    const size_t marker_length = sizeof(marker) - 1U;
    for (size_t index = 0U; index + marker_length <= length; ++index) {
        if (memcmp(data + index, marker, marker_length) == 0) {
            return true;
        }
    }
    return false;
}

static uint8_t *r_std_tls_native_terminated(const uint8_t *data, size_t length) {
    if (length == SIZE_MAX) {
        return NULL;
    }
    uint8_t *copy = r_std_tls_native_allocate(length + 1U);
    if (copy != NULL) {
        if (length != 0U) {
            (void)memcpy(copy, data, length);
        }
        copy[length] = 0U;
    }
    return copy;
}

static int32_t r_std_tls_native_parse_certificates(mbedtls_x509_crt *chain,
                                                   const uint8_t *data,
                                                   size_t length,
                                                   int64_t *native) {
    int result;
    if ((data == NULL) || (length == 0U)) {
        return R_STD_TLS_NATIVE_INVALID_CERTIFICATE;
    }
    if (r_std_tls_native_is_pem(data, length)) {
        uint8_t *copy = r_std_tls_native_terminated(data, length);
        if (copy == NULL) {
            return R_STD_TLS_NATIVE_ALLOCATION;
        }
        result = mbedtls_x509_crt_parse(chain, copy, length + 1U);
        r_std_tls_native_free(copy);
    } else {
        result = mbedtls_x509_crt_parse_der(chain, data, length);
    }
    if (result == MBEDTLS_ERR_X509_ALLOC_FAILED) {
        r_std_tls_native_store(native, result);
        return R_STD_TLS_NATIVE_ALLOCATION;
    }
    if (result != 0) {
        r_std_tls_native_store(native, result < 0 ? result : MBEDTLS_ERR_X509_INVALID_FORMAT);
        return R_STD_TLS_NATIVE_INVALID_CERTIFICATE;
    }
    return R_STD_TLS_NATIVE_OK;
}

void *r_std_tls_native_config_create(int32_t server) {
    if (!r_std_tls_native_ready()) {
        return NULL;
    }
    RStdTlsNativeConfig *config = r_std_tls_native_zeroed(sizeof(*config));
    if (config == NULL) {
        return NULL;
    }
    if (pthread_mutex_init(&config->lock, NULL) != 0) {
        r_std_tls_native_free(config);
        return NULL;
    }
    atomic_init(&config->references, 1U);
    config->server = server != 0;
    config->verification = !config->server;
    mbedtls_ssl_config_init(&config->ssl);
    mbedtls_x509_crt_init(&config->authorities);
    mbedtls_x509_crt_init(&config->chain);
    mbedtls_pk_init(&config->key);
    if (mbedtls_ssl_config_defaults(&config->ssl,
                                    config->server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        r_std_tls_native_config_release(config);
        return NULL;
    }
    mbedtls_ssl_conf_rng(&config->ssl, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    return config;
}

void r_std_tls_native_config_release(void *handle) {
    RStdTlsNativeConfig *config = handle;
    if ((config == NULL) || (atomic_fetch_sub(&config->references, 1U) != 1U)) {
        return;
    }
    mbedtls_ssl_config_free(&config->ssl);
    mbedtls_x509_crt_free(&config->authorities);
    mbedtls_x509_crt_free(&config->chain);
    mbedtls_pk_free(&config->key);
    (void)pthread_mutex_destroy(&config->lock);
    r_std_tls_native_free(config);
}

int32_t r_std_tls_native_config_add_authority(void *handle,
                                              const uint8_t *data,
                                              size_t length,
                                              int64_t *native) {
    RStdTlsNativeConfig *config = handle;
    int32_t status;
    (void)pthread_mutex_lock(&config->lock);
    if (config->frozen) {
        status = R_STD_TLS_NATIVE_FROZEN;
    } else {
        status = r_std_tls_native_parse_certificates(&config->authorities, data, length, native);
        config->has_authorities = config->has_authorities || (status == R_STD_TLS_NATIVE_OK);
    }
    (void)pthread_mutex_unlock(&config->lock);
    return status;
}

int32_t r_std_tls_native_config_set_identity(void *handle,
                                             const uint8_t *chain,
                                             size_t chain_length,
                                             const uint8_t *key,
                                             size_t key_length,
                                             int64_t *native) {
    RStdTlsNativeConfig *config = handle;
    mbedtls_x509_crt parsed_chain;
    mbedtls_pk_context parsed_key;
    int32_t status = R_STD_TLS_NATIVE_OK;
    int result;
    mbedtls_x509_crt_init(&parsed_chain);
    mbedtls_pk_init(&parsed_key);
    (void)pthread_mutex_lock(&config->lock);
    if (config->frozen) {
        status = R_STD_TLS_NATIVE_FROZEN;
        goto cleanup;
    }
    status = r_std_tls_native_parse_certificates(&parsed_chain, chain, chain_length, native);
    if (status != R_STD_TLS_NATIVE_OK) {
        goto cleanup;
    }
    if ((key == NULL) || (key_length == 0U)) {
        status = R_STD_TLS_NATIVE_INVALID_KEY;
        goto cleanup;
    }
    if (r_std_tls_native_is_pem(key, key_length)) {
        uint8_t *copy = r_std_tls_native_terminated(key, key_length);
        if (copy == NULL) {
            status = R_STD_TLS_NATIVE_ALLOCATION;
            goto cleanup;
        }
        result = mbedtls_pk_parse_key(&parsed_key,
                                      copy,
                                      key_length + 1U,
                                      NULL,
                                      0U,
                                      mbedtls_psa_get_random,
                                      MBEDTLS_PSA_RANDOM_STATE);
        mbedtls_platform_zeroize(copy, key_length + 1U);
        r_std_tls_native_free(copy);
    } else {
        result = mbedtls_pk_parse_key(&parsed_key,
                                      key,
                                      key_length,
                                      NULL,
                                      0U,
                                      mbedtls_psa_get_random,
                                      MBEDTLS_PSA_RANDOM_STATE);
    }
    if (result == 0) {
        result = mbedtls_pk_check_pair(
            &parsed_chain.pk, &parsed_key, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    }
    if (result != 0) {
        r_std_tls_native_store(native, result);
        status = (result == MBEDTLS_ERR_PK_ALLOC_FAILED) ? R_STD_TLS_NATIVE_ALLOCATION
                                                         : R_STD_TLS_NATIVE_INVALID_KEY;
        goto cleanup;
    }
    mbedtls_x509_crt_free(&config->chain);
    mbedtls_pk_free(&config->key);
    config->chain = parsed_chain;
    config->key = parsed_key;
    mbedtls_x509_crt_init(&parsed_chain);
    mbedtls_pk_init(&parsed_key);
    config->has_identity = true;
cleanup:
    (void)pthread_mutex_unlock(&config->lock);
    mbedtls_x509_crt_free(&parsed_chain);
    mbedtls_pk_free(&parsed_key);
    return status;
}

int32_t r_std_tls_native_config_add_protocol(void *handle, const uint8_t *name, size_t length) {
    RStdTlsNativeConfig *config = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    if ((name == NULL) || (length == 0U) || (length > 255U) || (memchr(name, 0, length) != NULL)) {
        return R_STD_TLS_NATIVE_INVALID_ARGUMENT;
    }
    (void)pthread_mutex_lock(&config->lock);
    if (config->frozen) {
        status = R_STD_TLS_NATIVE_FROZEN;
    } else if (config->protocol_count == R_STD_TLS_NATIVE_PROTOCOLS) {
        status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
    } else {
        char *slot = config->protocol_names[config->protocol_count];
        (void)memcpy(slot, name, length);
        slot[length] = '\0';
        config->protocols[config->protocol_count] = slot;
        config->protocol_count += 1U;
        config->protocols[config->protocol_count] = NULL;
    }
    (void)pthread_mutex_unlock(&config->lock);
    return status;
}

int32_t r_std_tls_native_config_set_verification(void *handle, int32_t required) {
    RStdTlsNativeConfig *config = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    (void)pthread_mutex_lock(&config->lock);
    if (config->frozen) {
        status = R_STD_TLS_NATIVE_FROZEN;
    } else {
        config->verification = required != 0;
    }
    (void)pthread_mutex_unlock(&config->lock);
    return status;
}

/* The first session applies the settings; the configuration is read only afterwards. */
static int32_t r_std_tls_native_freeze(RStdTlsNativeConfig *config, int64_t *native) {
    int32_t status = R_STD_TLS_NATIVE_OK;
    (void)pthread_mutex_lock(&config->lock);
    if (!config->frozen) {
        if (config->server && !config->has_identity) {
            status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
            goto cleanup;
        }
        mbedtls_ssl_conf_authmode(&config->ssl,
                                  config->verification ? MBEDTLS_SSL_VERIFY_REQUIRED
                                                       : MBEDTLS_SSL_VERIFY_NONE);
        if (config->has_authorities) {
            mbedtls_ssl_conf_ca_chain(&config->ssl, &config->authorities, NULL);
        }
        if (config->has_identity) {
            int result = mbedtls_ssl_conf_own_cert(&config->ssl, &config->chain, &config->key);
            if (result != 0) {
                r_std_tls_native_store(native, result);
                status = R_STD_TLS_NATIVE_ALLOCATION;
                goto cleanup;
            }
        }
        if (config->protocol_count != 0U) {
            int result = mbedtls_ssl_conf_alpn_protocols(&config->ssl, config->protocols);
            if (result != 0) {
                r_std_tls_native_store(native, result);
                status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
                goto cleanup;
            }
        }
        config->frozen = true;
    }
cleanup:
    (void)pthread_mutex_unlock(&config->lock);
    return status;
}

void *r_std_tls_native_session_create(void *handle,
                                      const uint8_t *server_name,
                                      size_t server_name_length,
                                      int32_t *status,
                                      int64_t *native) {
    RStdTlsNativeConfig *config = handle;
    RStdTlsNativeSession *session;
    int result;
    *status = r_std_tls_native_freeze(config, native);
    if (*status != R_STD_TLS_NATIVE_OK) {
        return NULL;
    }
    if (!config->server && config->verification && (server_name_length == 0U)) {
        *status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
        return NULL;
    }
    if ((server_name_length > 255U) ||
        ((server_name_length != 0U) && (memchr(server_name, 0, server_name_length) != NULL))) {
        *status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
        return NULL;
    }
    session = r_std_tls_native_zeroed(sizeof(*session));
    if ((session == NULL) || (pthread_mutex_init(&session->lock, NULL) != 0)) {
        r_std_tls_native_free(session);
        *status = R_STD_TLS_NATIVE_ALLOCATION;
        return NULL;
    }
    mbedtls_ssl_init(&session->ssl);
    atomic_fetch_add(&config->references, 1U);
    session->config = config;
    result = mbedtls_ssl_setup(&session->ssl, &config->ssl);
    if ((result == 0) && !config->server && (server_name_length != 0U)) {
        char name[256];
        (void)memcpy(name, server_name, server_name_length);
        name[server_name_length] = '\0';
        result = mbedtls_ssl_set_hostname(&session->ssl, name);
    }
    if (result != 0) {
        r_std_tls_native_store(native, result);
        *status = (result == MBEDTLS_ERR_SSL_ALLOC_FAILED) ? R_STD_TLS_NATIVE_ALLOCATION
                                                           : R_STD_TLS_NATIVE_INVALID_ARGUMENT;
        r_std_tls_native_session_release(session);
        return NULL;
    }
    mbedtls_ssl_set_bio(
        &session->ssl, session, r_std_tls_native_send, r_std_tls_native_receive, NULL);
    return session;
}

void r_std_tls_native_session_release(void *handle) {
    RStdTlsNativeSession *session = handle;
    if (session == NULL) {
        return;
    }
    mbedtls_ssl_free(&session->ssl);
    r_std_tls_native_free(session->input.data);
    r_std_tls_native_free(session->output.data);
    r_std_tls_native_config_release(session->config);
    (void)pthread_mutex_destroy(&session->lock);
    r_std_tls_native_free(session);
}

int32_t r_std_tls_native_session_feed(void *handle, const uint8_t *data, size_t length) {
    RStdTlsNativeSession *session = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    if (length == 0U) {
        return status;
    }
    (void)pthread_mutex_lock(&session->lock);
    if (!r_std_tls_native_reserve(&session->input, length)) {
        status = R_STD_TLS_NATIVE_ALLOCATION;
    } else {
        (void)memcpy(session->input.data + session->input.length, data, length);
        session->input.length += length;
    }
    (void)pthread_mutex_unlock(&session->lock);
    return status;
}

size_t r_std_tls_native_session_take(void *handle, uint8_t *target, size_t capacity) {
    RStdTlsNativeSession *session = handle;
    size_t count;
    (void)pthread_mutex_lock(&session->lock);
    count = session->output.length < capacity ? session->output.length : capacity;
    if (count != 0U) {
        (void)memcpy(target, session->output.data + session->output.start, count);
        session->output.start += count;
        session->output.length -= count;
        if (session->output.length == 0U) {
            session->output.start = 0U;
        }
    }
    (void)pthread_mutex_unlock(&session->lock);
    return count;
}

size_t r_std_tls_native_session_pending(void *handle) {
    RStdTlsNativeSession *session = handle;
    size_t count;
    (void)pthread_mutex_lock(&session->lock);
    count = session->output.length;
    (void)pthread_mutex_unlock(&session->lock);
    return count;
}

/* Maps a failure of Mbed TLS to the status of std.tls and keeps it for later calls. */
static int32_t r_std_tls_native_fail(RStdTlsNativeSession *session, int result, bool handshake) {
    int32_t status;
    if (session->output_failed || (result == MBEDTLS_ERR_SSL_ALLOC_FAILED) ||
        (result == MBEDTLS_ERR_X509_ALLOC_FAILED) || (result == MBEDTLS_ERR_PK_ALLOC_FAILED)) {
        status = R_STD_TLS_NATIVE_ALLOCATION;
    } else if (result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        const uint32_t flags = mbedtls_ssl_get_verify_result(&session->ssl);
        if ((flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0U) {
            status = R_STD_TLS_NATIVE_NAME_MISMATCH;
        } else if ((flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) != 0U) {
            status = R_STD_TLS_NATIVE_EXPIRED_CERTIFICATE;
        } else {
            status = R_STD_TLS_NATIVE_UNTRUSTED_CERTIFICATE;
        }
    } else if (result == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
        status = R_STD_TLS_NATIVE_PEER_ALERT;
    } else {
        status = handshake ? R_STD_TLS_NATIVE_HANDSHAKE_FAILED : R_STD_TLS_NATIVE_PROTOCOL_ERROR;
    }
    session->failure = status;
    session->failure_native = (int64_t)result;
    return status;
}

int32_t r_std_tls_native_session_handshake(void *handle, int64_t *native) {
    RStdTlsNativeSession *session = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    (void)pthread_mutex_lock(&session->lock);
    if (session->failure != R_STD_TLS_NATIVE_OK) {
        status = session->failure;
        r_std_tls_native_store(native, (int)session->failure_native);
    } else if (!session->handshake_done) {
        const int result = mbedtls_ssl_handshake(&session->ssl);
        if (result == 0) {
            session->handshake_done = true;
        } else if ((result == MBEDTLS_ERR_SSL_WANT_READ) ||
                   (result == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            status = R_STD_TLS_NATIVE_WANT_INPUT;
        } else {
            status = r_std_tls_native_fail(session, result, true);
            r_std_tls_native_store(native, result);
        }
    }
    (void)pthread_mutex_unlock(&session->lock);
    return status;
}

int32_t r_std_tls_native_session_read(
    void *handle, uint8_t *target, size_t capacity, size_t *count, int64_t *native) {
    RStdTlsNativeSession *session = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    *count = 0U;
    if (capacity == 0U) {
        return status;
    }
    if (capacity > (size_t)INT32_MAX) {
        capacity = (size_t)INT32_MAX;
    }
    (void)pthread_mutex_lock(&session->lock);
    if (session->failure != R_STD_TLS_NATIVE_OK) {
        status = session->failure;
        r_std_tls_native_store(native, (int)session->failure_native);
    } else if (!session->handshake_done) {
        status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
    } else {
        for (;;) {
            const int result = mbedtls_ssl_read(&session->ssl, target, capacity);
            if (result > 0) {
                *count = (size_t)result;
            } else if ((result == MBEDTLS_ERR_SSL_WANT_READ) ||
                       (result == MBEDTLS_ERR_SSL_WANT_WRITE)) {
                status = R_STD_TLS_NATIVE_WANT_INPUT;
            } else if (result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            } else if ((result == 0) || (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
                status = R_STD_TLS_NATIVE_CLOSED;
            } else {
                status = r_std_tls_native_fail(session, result, false);
                r_std_tls_native_store(native, result);
            }
            break;
        }
    }
    (void)pthread_mutex_unlock(&session->lock);
    return status;
}

int32_t
r_std_tls_native_session_write(void *handle, const uint8_t *data, size_t length, int64_t *native) {
    RStdTlsNativeSession *session = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    size_t written = 0U;
    (void)pthread_mutex_lock(&session->lock);
    if (session->failure != R_STD_TLS_NATIVE_OK) {
        status = session->failure;
        r_std_tls_native_store(native, (int)session->failure_native);
    } else if (!session->handshake_done) {
        status = R_STD_TLS_NATIVE_INVALID_ARGUMENT;
    } else {
        while (written < length) {
            size_t chunk = length - written;
            if (chunk > 16384U) {
                chunk = 16384U;
            }
            const int result = mbedtls_ssl_write(&session->ssl, data + written, chunk);
            if (result > 0) {
                written += (size_t)result;
            } else {
                status = r_std_tls_native_fail(session, result, false);
                r_std_tls_native_store(native, result);
                break;
            }
        }
    }
    (void)pthread_mutex_unlock(&session->lock);
    return status;
}

int32_t r_std_tls_native_session_close(void *handle, int64_t *native) {
    RStdTlsNativeSession *session = handle;
    int32_t status = R_STD_TLS_NATIVE_OK;
    (void)pthread_mutex_lock(&session->lock);
    if (session->failure != R_STD_TLS_NATIVE_OK) {
        status = session->failure;
        r_std_tls_native_store(native, (int)session->failure_native);
    } else if (session->handshake_done) {
        const int result = mbedtls_ssl_close_notify(&session->ssl);
        if ((result != 0) && (result != MBEDTLS_ERR_SSL_WANT_WRITE)) {
            status = r_std_tls_native_fail(session, result, false);
            r_std_tls_native_store(native, result);
        }
    }
    (void)pthread_mutex_unlock(&session->lock);
    return status;
}

const uint8_t *r_std_tls_native_session_protocol(void *handle, size_t *length) {
    RStdTlsNativeSession *session = handle;
    (void)pthread_mutex_lock(&session->lock);
    /* Mbed TLS selects an entry of the protocol list of the configuration, protocol_names. */
    const char *name =
        session->handshake_done ? mbedtls_ssl_get_alpn_protocol(&session->ssl) : NULL;
    (void)pthread_mutex_unlock(&session->lock);
    *length = name != NULL ? strlen(name) : 0U;
    return (const uint8_t *)name;
}

int32_t r_std_tls_native_session_version(void *handle) {
    RStdTlsNativeSession *session = handle;
    int32_t version = 0;
    (void)pthread_mutex_lock(&session->lock);
    if (session->handshake_done) {
        const mbedtls_ssl_protocol_version number = mbedtls_ssl_get_version_number(&session->ssl);
        version = number == MBEDTLS_SSL_VERSION_TLS1_3   ? 13
                  : number == MBEDTLS_SSL_VERSION_TLS1_2 ? 12
                                                         : 0;
    }
    (void)pthread_mutex_unlock(&session->lock);
    return version;
}
