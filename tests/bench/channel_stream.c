#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/channel_stream.r: an unbounded mutex-and-condition queue between two
   threads. */
typedef struct node {
    uint32_t value;
    struct node *next;
} node;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static node *head, *tail;
static int done;
static void *produce(void *arg) {
    uint32_t count = *(uint32_t *)arg;
    for (uint32_t v = 0; v < count; v += 1) {
        node *n = malloc(sizeof *n);
        n->value = v;
        n->next = NULL;
        pthread_mutex_lock(&lock);
        if (tail)
            tail->next = n;
        else
            head = n;
        tail = n;
        pthread_cond_signal(&ready);
        pthread_mutex_unlock(&lock);
    }
    pthread_mutex_lock(&lock);
    done = 1;
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&lock);
    return NULL;
}
int main(void) {
    uint32_t count = 2000000u;
    pthread_t t;
    uint32_t total = 0;
    pthread_create(&t, NULL, produce, &count);
    for (;;) {
        pthread_mutex_lock(&lock);
        while (!head && !done)
            pthread_cond_wait(&ready, &lock);
        node *n = head;
        if (n) {
            head = n->next;
            if (!head)
                tail = NULL;
        }
        pthread_mutex_unlock(&lock);
        if (!n)
            break;
        total += n->value;
        free(n);
    }
    pthread_join(t, NULL);
    return (int)(total % 109u);
}
