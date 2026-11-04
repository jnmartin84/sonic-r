/*
 * pthread.h - minimal pthreads over SDL threads for nxdk.
 *
 * Only net/network.c uses pthreads: one receive thread that polls net_recv()
 * and a mutex around its packet queue. PTHREAD_MUTEX_INITIALIZER must work as
 * a static initialiser, so the mutex is an SDL spinlock (an int, 0 = free).
 */
#ifndef XBOX_PTHREAD_H
#define XBOX_PTHREAD_H

#include <SDL.h>
#include <stdlib.h>

typedef SDL_SpinLock pthread_mutex_t;
typedef SDL_Thread  *pthread_t;

#define PTHREAD_MUTEX_INITIALIZER 0

static inline int pthread_mutex_lock(pthread_mutex_t *m)   { SDL_AtomicLock(m); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { SDL_AtomicUnlock(m); return 0; }

typedef struct {
    void *(*fn)(void *);
    void *arg;
} xb_pthread_start_t;

static int SDLCALL xb_pthread_tramp(void *p)
{
    xb_pthread_start_t s = *(xb_pthread_start_t *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}

static inline int pthread_create(pthread_t *t, const void *attr,
                                 void *(*fn)(void *), void *arg)
{
    (void)attr;
    xb_pthread_start_t *s = (xb_pthread_start_t *)malloc(sizeof(*s));
    if (!s) return -1;
    s->fn = fn;
    s->arg = arg;
    *t = SDL_CreateThread(xb_pthread_tramp, "net", s);
    if (!*t) {
        free(s);
        return -1;
    }
    return 0;
}

static inline int pthread_join(pthread_t t, void **ret)
{
    if (ret) *ret = NULL;
    if (t) SDL_WaitThread(t, NULL);
    return 0;
}

#endif /* XBOX_PTHREAD_H */
