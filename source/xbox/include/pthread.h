/*
 * pthread.h - no-op shim for nxdk.
 *
 * Only net/network.c uses pthreads, to run a receive thread that polls
 * net_recv(). With the stub transport (src/xbox_net_stub.c) that thread has
 * nothing to do, so thread creation reports failure and the mutex calls do
 * nothing. If a real lwIP transport is added, replace this with a thin wrapper
 * over SDL_CreateThread / SDL_mutex.
 */
#ifndef XBOX_PTHREAD_H
#define XBOX_PTHREAD_H

typedef int pthread_mutex_t;
typedef int pthread_t;

#define PTHREAD_MUTEX_INITIALIZER 0

static inline int pthread_mutex_lock(pthread_mutex_t *m)   { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }

static inline int pthread_create(pthread_t *t, const void *attr,
                                 void *(*fn)(void *), void *arg)
{
    (void)t; (void)attr; (void)fn; (void)arg;
    return -1;
}

static inline int pthread_join(pthread_t t, void **ret)
{
    (void)t; (void)ret;
    return 0;
}

#endif /* XBOX_PTHREAD_H */
