/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _EVENTER_EVENTER_H
#define _EVENTER_EVENTER_H

#include "mtev_defines.h"
#include "mtev_log.h"
#include "mtev_atomic.h"
#include "mtev_time.h"
#include <sys/time.h>
#include <sys/socket.h>

#define EVENTER_READ             0x01
#define EVENTER_WRITE            0x02
#define EVENTER_EXCEPTION        0x04
#define EVENTER_TIMER            0x08
#define EVENTER_ASYNCH_WORK      0x10
#define EVENTER_ASYNCH_CLEANUP   0x20
#define EVENTER_ASYNCH           (EVENTER_ASYNCH_WORK | EVENTER_ASYNCH_CLEANUP)
#define EVENTER_RECURRENT        0x80
#define EVENTER_EVIL_BRUTAL     0x100
#define EVENTER_CANCEL_DEFERRED 0x200
#define EVENTER_CANCEL_ASYNCH   0x400
#define EVENTER_CANCEL          (EVENTER_CANCEL_DEFERRED|EVENTER_CANCEL_ASYNCH)

#define EVENTER_RESERVED                  0xfff00000
#define EVENTER_CROSS_THREAD_TRIGGER      0x80000000

#define EVENTER_DEFAULT_ASYNCH_ABORT EVENTER_EVIL_BRUTAL

#define EVENTER_CHOOSE_THREAD_FOR_EVENT_FD(e) eventer_choose_owner((e)->fd+1)

/* All of these functions act like their POSIX couterparts with two
 * additional arguments.  The first is the mask they require to be active
 * to make progress in the event of an EAGAIN.  The second is a closure
 * which is the event itself.
 */
typedef int (*eventer_fd_accept_t)
            (int, struct sockaddr *, socklen_t *, int *mask, void *closure);
typedef int (*eventer_fd_read_t)
            (int, void *, size_t, int *mask, void *closure);
typedef int (*eventer_fd_write_t)
            (int, const void *, size_t, int *mask, void *closure);
typedef int (*eventer_fd_close_t)
            (int, int *mask, void *closure);

typedef struct _fd_opset {
  eventer_fd_accept_t accept;
  eventer_fd_read_t   read;
  eventer_fd_write_t  write;
  eventer_fd_close_t  close;
  const char *name;
} *eventer_fd_opset_t;

typedef struct _event *eventer_t;

typedef struct eventer_pool_t eventer_pool_t;

#include "eventer/eventer_POSIX_fd_opset.h"
#include "eventer/eventer_SSL_fd_opset.h"

typedef int (*eventer_func_t)
            (eventer_t e, int mask, void *closure, struct timeval *tv);

struct _event {
  eventer_func_t      callback;
  struct timeval      whence;
  int                 fd;
  int                 mask;
  eventer_fd_opset_t  opset;
  void               *opset_ctx;
  void               *closure;
  pthread_t           thr_owner;
  mtev_atomic32_t     refcnt;
};

/* allocating, freeing and reference counts:
   When eventer_alloc() is called, the object returned has a refcnt == 1.
   Once the event it handed into the eventer (via the eventer_add type
   functions), the eventer is then responsible for deref'ing the event.
   If another thread needs access to this event and is worried about
   the eventer firing and subsequently freeing the event, the event
   should be eventer_ref()'d before it is passed to that new thread and
   subsequently eventer_deref()'d by the new thread when it is no longer
   needed.

   use 1:
     THREAD 1

     e = eventer_alloc()
     ...
     eventer_add(e)

   use 2:
     THREAD 1                |  THREAD 2
     e = eventer_alloc()     |
     ...                     |
     eventer_ref(e)          |
     // hand e to thread 2   |  // receive e
     ...                     |  ...
     eventer_add(e)          |  ...
                             |  eventer_deref(e)
 */

/*! \fn eventer_t eventer_alloc()
    \brief Allocate an event to be injected into the eventer system.
    \return A newly allocated event.

    The allocated event has a refernce count of 1 and is attached to the
    calling thread.
*/
API_EXPORT(eventer_t) eventer_alloc();

/*! \fn void eventer_free(eventer_t e)
    \brief Dereferences the event specified.
    \param e the event to dereference.
*/
API_EXPORT(void)      eventer_free(eventer_t);

/*! \fn void eventer_ref(evneter_t e)
    \brief Add a reference to an event.
    \param e the event to reference.

    Adding a reference to an event will prevent it from being deallocated
    prematurely.  This is classic reference counting.  It is are that one
    needs to maintain an actual event past the point where the eventer
    system would normally free it.  Typically, one will allocate a new
    event and copy the contents of the old event into it allowing the
    original to be freed.
*/
API_EXPORT(void)      eventer_ref(eventer_t);

/*! \fn void eventer_deref(eventer_t e)
    \brief See eventer_free.
    \param e the event to dereference.
*/
API_EXPORT(void)      eventer_deref(eventer_t);

/*! \fn int64_t eventer_allocations_current()
    \return the number of currently allocated eventer objects.
*/
API_EXPORT(int64_t)   eventer_allocations_current();

/*! \fn int64_t eventer_allocations_total()
    \return the number of allocated eventer objects over the life of the process.
*/
API_EXPORT(int64_t)   eventer_allocations_total();

/*! \fn int eventer_name_callback(const char *name, eventer_func_t callback)
    \brief Register a human/developer readable name for a eventer callback function.
    \param name the human readable name.
    \param callback the functin pointer of the eveter callback.
    \return 0 on success.
*/
API_EXPORT(int)       eventer_name_callback(const char *name, eventer_func_t f);

/*! \fn int eventer_name_callback_ext(const char *name, eventer_func_t callback, void (*fn)(char *buff,int bufflen,eventer_t e,void *closure), void *closure)
    \brief Register a functional describer for a callback and it's event object.
    \param name the human readable name.
    \param callback the functin pointer of the eveter callback.
    \param fn function to call when describing the event. It should write a null terminated string into buff (no more than bufflen).
    \return 0 on success.

    This function allows more in-depth descriptions of events.  When an event
    is displayed (over the console or REST endpoints), this function is called
    with the event in question and the closure specified at registration time.
*/
API_EXPORT(int)       eventer_name_callback_ext(const char *name,
                                                eventer_func_t f,
                                                void (*fn)(char *,int,eventer_t,void *),
                                                void *);

/*! \fn const char *eventer_name_for_callback(evneter_func_t f)
    \brief Retrieve a human readable name for the provided callback.
    \param f a callback function.
    \return name of callback

    The returned value may be a pointer to reusable thread-local storage.
    The value should be used before a subsequent call to this function.
    Aside from that caveat, it is thread-safe.
*/
API_EXPORT(const char *)
                      eventer_name_for_callback(eventer_func_t f);

/*! \fn const char *eventer_name_for_callback(evneter_func_t f, eventer_t e)
    \brief Retrieve a human readable name for the provided callback with event context.
    \param f a callback function.
    \param e and event object
    \return name of callback

    The returned value may be a pointer to reusable thread-local storage.
    The value should be used before a subsequent call to this function.
    Aside from that caveat, it is thread-safe.
*/
API_EXPORT(const char *)
                      eventer_name_for_callback_e(eventer_func_t, eventer_t);

/*! \fn evneter_func_t eventer_callback_for_name(const char *name)
    \brief Find an event callback function that has been registered by name.
    \param name the name of the callback.
    \return the function pointer or NULL if no such callback has been registered.
*/
API_EXPORT(eventer_func_t)
                      eventer_callback_for_name(const char *name);

/* These values are set on initialization and are safe to use
 * on any platform.  They will be set to zero on platforms that
 * do not support them.  As such, you can always bit-or them.
 */
API_EXPORT(int) NE_SOCK_CLOEXEC;
API_EXPORT(int) NE_O_CLOEXEC;

typedef struct _eventer_impl {
  const char         *name;
  int               (*init)();
  int               (*propset)(const char *key, const char *value);
  void              (*add)(eventer_t e);
  eventer_t         (*remove)(eventer_t e);
  void              (*update)(eventer_t e, int newmask);
  eventer_t         (*remove_fd)(int fd);
  eventer_t         (*find_fd)(int fd);
  void              (*trigger)(eventer_t e, int mask);
  int               (*loop)(int);
  void              (*foreach_fdevent)(void (*f)(eventer_t, void *), void *);
  void              (*wakeup)(eventer_t);
  void             *(*alloc_spec)();
  struct timeval    max_sleeptime;
  int               maxfds;
  struct {
    eventer_t e;
    pthread_t executor;
    mtev_spinlock_t lock;
  }                 *master_fds;
} *eventer_impl_t;

extern eventer_impl_t __eventer;
extern mtev_log_stream_t eventer_err;
extern mtev_log_stream_t eventer_deb;

API_EXPORT(int) eventer_choose(const char *name);

/*! \fn void eventer_loop()
    \brief Start the event loop.
    \return N/A (does not return)

    This function should be called as that last think in your `child_main` function.
    See [`mtev_main`](c.md#mtevmain`).
*/
API_EXPORT(void) eventer_loop();

/*! \fn int eventer_is_loop(pthread_t tid)
    \brief Determine if a thread is participating in the eventer loop.
    \param tid a thread
    \return 0 if the specified thread lives outside the eventer loop; 1 otherwise.
*/
API_EXPORT(int) eventer_is_loop(pthread_t tid);

/*! \fn int eventer_loop_concurrency()
    \brief Determine the concurrency of the default eventer loop.
    \return number of threads used for the default eventer loop.
*/
API_EXPORT(int) eventer_loop_concurrency();

/*! \fn void eventer_init_globals()
    \brief Initialize global structures required for eventer operation.

    This function is called by [`mtev_main`](c.md#mtevmain).  Developers should not
    need to call this function directly.
*/
API_EXPORT(void) eventer_init_globals();

#define eventer_propset       __eventer->propset
#define eventer_init          __eventer->init

/*! \fn void eventer_add(eventer_t e)
    \brief Add an event object to the eventer system.
    \param e an event object to add.
*/
#define eventer_add           __eventer->add

/*! \fn eventer_t eventer_remove(eventer_t e)
    \brief Remove an event object from the eventer system.
    \param e an event object to add.
    \return the event object removed if found; NULL if not found.
*/
#define eventer_remove        __eventer->remove

/*! \fn void eventer_update(evneter_t e, int mask)
    \brief Change the activity mask for file descriptor events.
    \param e an event object
    \param mask a new mask that is some bitwise or of `EVENTER_READ`, `EVENTER_WRITE`, and `EVENTER_EXCEPTION`
*/
#define eventer_update        __eventer->update


/*! \fn eventer_t eventer_remove_fd(int e)
    \brief Remove an event object from the eventer system by file descriptor.
    \param fd a file descriptor
    \return the event object removed if found; NULL if not found.
*/
#define eventer_remove_fd     __eventer->remove_fd

/*! \fn eventer_t eventer_find_fd(int e)
    \brief Find an event object in the eventer system by file descriptor.
    \param fd a file descriptor
    \return the event object if it exists; NULL if not found.
*/
#define eventer_find_fd       __eventer->find_fd

/*! \fn void eventer_trigger(eventer_t e, int mask)
    \brief Trigger an unregistered eventer and incorporate the outcome into the eventer.
    \param e an event object that is not registered with the eventer.
    \param mask the mask to be used when invoking the event's callback.

    This is often used to "start back up" an event that has been removed from the
    eventer for any reason.
*/
#define eventer_trigger       __eventer->trigger

#define eventer_max_sleeptime __eventer->max_sleeptime

/*! \fn void eventer_foreach_fdevent(void (*fn)(eventer_t, void *), void *closure)
    \brief Run a user-provided function over all registered file descriptor events.
    \param fn a function to be called with each event and `closure` as its arguments.
    \param closure the second argument to be passed to `fn`.
*/
#define eventer_foreach_fdevent  __eventer->foreach_fdevent

/*! \fn void eventer_wakeup(eventer_t e)
    \brief Signal up an event loop manually.
    \param e an event

    The event `e` is used to determine which thread of the eventer loop to wake up.
    If `e` is `NULL` the first thread in the default eventer loop is signalled. The
    eventer loop can wake up on timed events, asynchronous job completions and 
    file descriptor activity.  If, for an external reason, one needs to wake up
    a looping thread, this call is used.
*/
#define eventer_wakeup        __eventer->wakeup

extern eventer_impl_t registered_eventers[];

#define eventer_hrtime_t mtev_hrtime_t
#define eventer_gethrtime mtev_gethrtime

#include "eventer/eventer_jobq.h"

API_EXPORT(int) eventer_boot_ctor();
API_EXPORT(eventer_jobq_t *) eventer_default_backq(eventer_t);

/*! \fn int eventer_impl_propset(const char *key, const char *value)
    \brief Set properties for the event loop.
    \param key the property
    \param value the property's value.
    \return 0 on success, -1 otherwise.

    Sets propoerties within the eventer. That can only be called prior
    to [`eventer_init`](c.md#eventerinit). See [Eventer configuuration)(../config/eventer.md)
    for valid properties.
*/
API_EXPORT(int) eventer_impl_propset(const char *key, const char *value);

/*! \fn int eventer_impl_setrlimit()
    \brief Attempt to set the rlimit on allowable open files.
    \return the limit of the number of open files.

    The target is the `rlim_nofiles` eventer config option. If that configuration
    option is unspecified, 1048576 is used.
*/
API_EXPORT(int) eventer_impl_setrlimit();

/*! \fn void eventer_add_asynch(eventer_t e)
    \brief Add an asynchronous event to a specific job queue.
    \param q a job queue
    \param e an event object

    This adds the `e` event to the job queue `q`.  `e` must have a mask
    of `EVENETER_ASYNCH`.
*/
API_EXPORT(void) eventer_add_asynch(eventer_jobq_t *q, eventer_t e);

/*! \fn void eventer_add_timed(eventer_t e)
    \brief Add a timed event to the eventer system.
    \param e an event object

    This adds the `e` event to the eventer. `e` must have a mask of
    `EVENTER_TIMED`.
*/
API_EXPORT(void) eventer_add_timed(eventer_t e);

/*! \fn eventer_t eventer_remove_timed(eventer_t e)
    \brief Remove a timed event from the eventer.
    \param e an event object (mask must be `EVENTER_TIMED`).
    \return the event removed, NULL if not found.
*/
API_EXPORT(eventer_t) eventer_remove_timed(eventer_t e);

/*! \fn eventer_t eventer_remove_timed(eventer_t e)
    \brief Remove a timed event from the eventer.
    \param e an event object (mask must be `EVENTER_TIMED`).
    \return the event removed, NULL if not found.
*/

/*! \fn void eventer_foreach_timedevent(void (*fn)(eventer_t, void *), void *closure)
    \brief Run a user-provided function over all registered timed events.
    \param fn a function to be called with each event and `closure` as its arguments.
    \param closure the second argument to be passed to `fn`.
*/
API_EXPORT(void)
  eventer_foreach_timedevent (void (*f)(eventer_t e, void *), void *closure);

/*! \fn void eventer_add_recurrent(eventer_t e)
    \brief Add an event to run during every loop cycle.
    \param e an event object

    `e` must have a mask of EVENER_RECURRENT.  This event will be invoked on
    a single thread (dictated by `e`) once for each pass through the eventer loop.
    This happens _often_, so do light work.
*/
API_EXPORT(void) eventer_add_recurrent(eventer_t e);

/*! \fn eventer_t eventer_remove_recurrent(eventer_t e)
    \brief Remove a recurrent event from the eventer.
    \param e an event object.
    \return The event removed (`== e`); NULL if not found.
*/
API_EXPORT(eventer_t) eventer_remove_recurrent(eventer_t e);

/*! \fn int eventer_get_epoch(struct timeval *epoch)
    \brief Find the start time of the eventer loop.
    \param epoch a point to a `struct timeval` to fill out.
    \return 0 on success; -1 on failure (eventer loop not started).
*/
API_EXPORT(int) eventer_get_epoch(struct timeval *epoch);

/*! \fn eventer_pool_t *eventer_pool(const char *name)
    \brief Find an eventer pool by name.
    \param name the name of an eventer pool.
    \return an `eventer_pool_t *` by the given name, or NULL.
*/
API_EXPORT(eventer_pool_t *) eventer_pool(const char *name);

/*! \fn eventer_pool_t *eventer_get_pool_for_event(eventer_t e)
    \brief Determin which eventer pool owns a given event.
    \param e an event object.
    \return the `eventer_pool_t` to which the event is scheduled.
*/
API_EXPORT(eventer_pool_t *) eventer_get_pool_for_event(eventer_t);

/*! \fn const char *eventer_pool_name(eventer_pool_t *pool)
    \brief Retrieve the name of an eventer pool.
    \param pool an eventer pool.
    \return the name of the eventer pool.
*/
API_EXPORT(const char *) eventer_pool_name(eventer_pool_t *);

/*! \fn uint32_t eventer_pool_concurrency(eventer_pool_t *pool)
    \brief Retrieve the concurrency of an eventer pool.
    \param pool an eventer pool.
    \return the number of threads powering the specified pool.
*/
API_EXPORT(uint32_t) eventer_pool_concurrency(eventer_pool_t *);

/*! \fn void eventer_pool_watchdog_timeout(eventer_pool_t *pool, double timeout)
    \brief Set a custom watchdog timeout for threads in an eventer pool.
    \param pool an eventer pool
    \param timeout the deadman timer in seconds.
*/
API_EXPORT(void) eventer_pool_watchdog_timeout(eventer_pool_t *pool, double timeout);

/*! \fn pthread_t eventer_choose_owner(int n)
    \brief Find a thread in the default eventer pool.
    \param n an integer.
    \return a pthread_t of an eventer loop thread in the default eventer pool.

    This return the first thread when 0 is passed as an argument.  All non-zero arguments
    are spread acorss the remaining threads (if existent) as `n` modulo one less than
    the concurrency of the default event pool.

    This is done because many systems aren't thread safe and can only schedule their
    work on a single thread (thread 1). By spreading all thread-safe workloads across
    the remaining threads we reduce potential overloading of the "main" thread.

    To assign an event to a thread, use the result of this function to assign:
    `e->thr_owner`.
*/
API_EXPORT(pthread_t) eventer_choose_owner(int);

/*! \fn pthread_t eventer_choose_owner_pool(eventer_pool_t *pool, int n)
    \brief Find a thread in a specific eventer pool.
    \param pool an eventer pool.
    \param n an integer.
    \return a pthread_t of an eventer loop thread in the specified evneter pool.

    This function chooses a thread within the specified pool by taking `n`
    modulo the concurrency of the pool.  If the default pool is speicified, special
    assignment behavior applies. See [`eventer_choose_owner`](c.md#eventerchooseowner).

    To assign an event to a thread, use the result of this function to assign:
    `e->thr_owner`.
*/
API_EXPORT(pthread_t) eventer_choose_owner_pool(eventer_pool_t *pool, int);

/* Helpers to schedule timed events */
/*! \fn eventer_t eventer_at(eventer_func_t func, void *closure, struct timeval whence)
    \brief Convenience function to create an event to run a callback at a specific time.
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param whence the time at which to run the callback.
    \return an event that has not been added to the eventer.

    > Note this does not actually schedule the event. See [`eventer_add_at`](c.md#eventeraddat).
*/
static inline eventer_t
eventer_at(eventer_func_t func, void *cl, struct timeval t) {
  eventer_t e = eventer_alloc();
  e->whence = t;
  e->mask = EVENTER_TIMER;
  e->callback = func;
  e->closure = cl;
  return e;
}

/*! \fn eventer_t eventer_add_at(eventer_func_t func, void *closure, struct timeval whence)
    \brief Convenience function to schedule a callback at a specific time.
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param whence the time at which to run the callback.
    \return N/A (C Macro).
*/
#define eventer_add_at(func, cl, t) do { \
  eventer_add(eventer_at(func,cl,t)); \
} while(0)

/*! \fn eventer_t eventer_in(eventer_func_t func, void *closure, struct timeval diff)
    \brief Convenience function to create an event to run a callback in the future
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param diff the amount of time to wait before running the callback.
    \return an event that has not been added to the eventer.

    > Note this does not actually schedule the event. See [`eventer_add_in`](c.md#eventeraddin).
*/
static inline eventer_t
eventer_in(eventer_func_t func, void *cl, struct timeval t) {
  struct timeval __now;
  eventer_t e = eventer_alloc();
  mtev_gettimeofday(&__now, NULL);
  add_timeval(__now, t, &e->whence);
  e->mask = EVENTER_TIMER;
  e->callback = func;
  e->closure = cl;
  return e;
}

/*! \fn eventer_t eventer_add_in(eventer_func_t func, void *closure, struct timeval diff)
    \brief Convenience function to create an event to run a callback in the future
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param diff the amount of time to wait before running the callback.
    \return N/A (C Macro).
*/
#define eventer_add_in(func, cl, t) do { \
  eventer_add(eventer_in(func,cl,t)); \
} while(0)

/*! \fn eventer_t eventer_in_s_us(eventer_func_t func, void *closure, unsigned long seconds, unsigned long microseconds)
    \brief Convenience function to create an event to run a callback in the future
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param seconds the number of seconds to wait before running the callback.
    \param microseconds the number of microseconds (in addition to `seconds`) to wait before running the callback.
    \return an event that has not been added to the eventer.

    > Note this does not actually schedule the event. See [`eventer_add_in_s_us`](c.md#eventeraddinsus).
*/
static inline eventer_t
eventer_in_s_us(eventer_func_t func, void *cl, unsigned long s, unsigned long us) {
  struct timeval __now, diff = { (time_t)s, (suseconds_t)us };
  eventer_t e = eventer_alloc();
  mtev_gettimeofday(&__now, NULL);
  add_timeval(__now, diff, &e->whence);
  e->mask = EVENTER_TIMER;
  e->callback = func;
  e->closure = cl;
  return e;
}

/*! \fn eventer_t eventer_add_in_s_us(eventer_func_t func, void *closure, unsigned long seconds, unsigned long microseconds)
    \brief Convenience function to create an event to run a callback in the future
    \param func the callback function to run.
    \param closure the closure to be passed to the callback.
    \param seconds the number of seconds to wait before running the callback.
    \param microseconds the number of microseconds (in addition to `seconds`) to wait before running the callback.
    \return N/A (C Macro).
*/
#define eventer_add_in_s_us(func, cl, s, us) do { \
  eventer_add(eventer_in_s_us(func,cl,s,us)); \
} while(0)

/* Helpers to set sockets non-blocking / blocking */
/*! \fn int eventer_set_fd_nonblocking(int fd)
    \brief Set a file descriptor into non-blocking mode.
    \param fd a file descriptor
    \return 0 on success, -1 on error (errno set).
*/
API_EXPORT(int) eventer_set_fd_nonblocking(int fd);

/*! \fn int eventer_set_fd_blocking(int fd)
    \brief Set a file descriptor into blocking mode.
    \param fd a file descriptor
    \return 0 on success, -1 on error (errno set).
*/
API_EXPORT(int) eventer_set_fd_blocking(int fd);

/*! \fn int eventer_thread_check(eventer_t e)
    \brief Determine if the calling thread "owns" an event.
    \param e an event object
    \return 0 if `e->thr_owner` is the `pthread_self()`, non-zero otherwise.
*/
API_EXPORT(int) eventer_thread_check(eventer_t);

/* Private */
API_EXPORT(int) eventer_impl_init();
API_EXPORT(void) eventer_update_timed(eventer_t e, int mask);
API_EXPORT(void) eventer_dispatch_timed(struct timeval *now,
                                        struct timeval *next);
API_EXPORT(void) eventer_dispatch_recurrent(struct timeval *now);
API_EXPORT(void *) eventer_get_spec_for_event(eventer_t);
API_EXPORT(int) eventer_cpu_sockets_and_cores(int *sockets, int *cores);

#endif
