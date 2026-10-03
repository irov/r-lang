#include "io_internal.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static pthread_mutex_t stream_position_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stream_position_test_condition = PTHREAD_COND_INITIALIZER;
static _Bool stream_position_test_armed;
static _Bool stream_position_test_reached;
static _Bool stream_position_test_released;
/* Unarmed hooks stay off the hot path: the hint is read without the mutex, and the mutex
   state stays authoritative once it is set. */
static _Atomic _Bool stream_position_test_hint;

void r_runtime_darwin_io_testing_pause_next_stream_position_barrier(void) {
    if (pthread_mutex_lock(&stream_position_test_mutex) != 0) {
        abort();
    }
    if (stream_position_test_armed) {
        (void)pthread_mutex_unlock(&stream_position_test_mutex);
        abort();
    }
    stream_position_test_armed = 1;
    atomic_store_explicit(&stream_position_test_hint, 1, memory_order_release);
    stream_position_test_reached = 0;
    stream_position_test_released = 0;
    if (pthread_mutex_unlock(&stream_position_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_stream_position_barrier(void) {
    if (pthread_mutex_lock(&stream_position_test_mutex) != 0) {
        abort();
    }
    if (!stream_position_test_armed) {
        (void)pthread_mutex_unlock(&stream_position_test_mutex);
        abort();
    }
    while (!stream_position_test_reached) {
        if (pthread_cond_wait(&stream_position_test_condition, &stream_position_test_mutex) != 0) {
            (void)pthread_mutex_unlock(&stream_position_test_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&stream_position_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_release_stream_position_barrier(void) {
    if (pthread_mutex_lock(&stream_position_test_mutex) != 0) {
        abort();
    }
    if (!stream_position_test_armed || !stream_position_test_reached ||
        stream_position_test_released) {
        (void)pthread_mutex_unlock(&stream_position_test_mutex);
        abort();
    }
    stream_position_test_released = 1;
    if (pthread_cond_broadcast(&stream_position_test_condition) != 0) {
        (void)pthread_mutex_unlock(&stream_position_test_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&stream_position_test_mutex) != 0) {
        abort();
    }
}

static void testing_pause_stream_position_barrier(void) {
    if (!atomic_load_explicit(&stream_position_test_hint, memory_order_acquire)) {
        return;
    }
    if (pthread_mutex_lock(&stream_position_test_mutex) != 0) {
        abort();
    }
    if (stream_position_test_armed) {
        stream_position_test_reached = 1;
        if (pthread_cond_broadcast(&stream_position_test_condition) != 0) {
            (void)pthread_mutex_unlock(&stream_position_test_mutex);
            abort();
        }
        while (!stream_position_test_released) {
            if (pthread_cond_wait(&stream_position_test_condition, &stream_position_test_mutex) !=
                0) {
                (void)pthread_mutex_unlock(&stream_position_test_mutex);
                abort();
            }
        }
        stream_position_test_armed = 0;
        atomic_store_explicit(&stream_position_test_hint, 0, memory_order_release);
        stream_position_test_reached = 0;
        stream_position_test_released = 0;
    }
    if (pthread_mutex_unlock(&stream_position_test_mutex) != 0) {
        abort();
    }
}
#else
static void testing_pause_stream_position_barrier(void) {
}
#endif

void r_runtime_darwin_io_internal_testing_pause_position_entry(void) {
    testing_pause_stream_position_barrier();
}

dispatch_io_t
r_runtime_darwin_io_internal_create_operation_channel(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    dispatch_io_type_t dispatch_type =
        handle->type == R_RUNTIME_DARWIN_IO_STREAM ? DISPATCH_IO_STREAM : DISPATCH_IO_RANDOM;
    dispatch_io_t channel;

    channel = dispatch_io_create_with_io(
        dispatch_type, handle->root_channel, handle->callback_queue, ^(int error) {
          (void)error;
        });
    if (channel == NULL) {
        return NULL;
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_channel != NULL) {
        abort();
    }
    request->operation_channel = channel;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return channel;
}

void r_runtime_darwin_io_internal_schedule_stream_position_barrier(
    RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;

    if (handle->type != R_RUNTIME_DARWIN_IO_STREAM || handle->root_channel == NULL ||
        !request->stream_position_enabled || !request->stream_position_barrier_pending ||
        request->prepared_stream_position < 0) {
        abort();
    }
    dispatch_io_barrier(handle->root_channel, ^{
      int native_error = 0;
      off_t seek_result;

      testing_pause_stream_position_barrier();
      do {
          seek_result =
              lseek(handle->retained_descriptor, request->prepared_stream_position, SEEK_SET);
      } while (seek_result < 0 && errno == EINTR);
      if (seek_result < 0) {
          native_error = errno;
      }
      switch (request->operation) {
      case R_RUNTIME_DARWIN_IO_READ:
          r_runtime_darwin_io_internal_read_stream_position_barrier_done(request, native_error);
          break;
      case R_RUNTIME_DARWIN_IO_WRITE:
          r_runtime_darwin_io_internal_write_stream_position_barrier_done(request, native_error);
          break;
      case R_RUNTIME_DARWIN_IO_FLUSH:
      case R_RUNTIME_DARWIN_IO_CLOSE:
      case R_RUNTIME_DARWIN_IO_SHUTDOWN:
          abort();
      }
    });
}

void r_runtime_darwin_io_internal_close_operation_channel(RRuntimeDarwinIoRequest *request,
                                                          dispatch_io_close_flags_t flags) {
    dispatch_io_t channel = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_channel != NULL && !request->channel_released) {
        channel = request->operation_channel;
        request->operation_channel = NULL;
        request->channel_released = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (channel != NULL) {
        dispatch_io_close(channel, flags);
        dispatch_release(channel);
    }
}
