/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * An edited version of verto-libev.c, using an embedded libev with renamed
 * symbols.  The corresponding version of verto-libev.c is stored in this
 * directory for reference, although it is not built here.
 */

#include "autoconf.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <verto.h>
#include <verto-module.h>
#include "rename.h"
#define EV_API_STATIC 1
#define EV_STANDALONE 1
/* Avoid using clock_gettime, which would create a dependency on librt. */
#define EV_USE_MONOTONIC 0
#define EV_USE_REALTIME 0
#define EV_FEATURES 0x5f        /* Everything but back ends */
#ifdef HAVE_POLL_H
#define EV_USE_POLL 1
#endif
/* ev.c explicitly disables poll() on Mac or FreeBSD; fall back to select(). */
#define EV_USE_SELECT 1
#include "ev.c"

static verto_mod_ctx *
k5ev_ctx_new(void)
{
    return ev_loop_new(EVFLAG_AUTO);
}

static verto_mod_ctx *
k5ev_ctx_default(void)
{
    return ev_default_loop(EVFLAG_AUTO);
}

static void
k5ev_ctx_free(verto_mod_ctx *ctx)
{
    if (ctx != EV_DEFAULT)
        ev_loop_destroy(ctx);
}

static void
k5ev_ctx_run(verto_mod_ctx *ctx)
{
    ev_run(ctx, 0);
}

static void
k5ev_ctx_run_once(verto_mod_ctx *ctx)
{
    ev_run(ctx, EVRUN_ONCE);
}

static void
k5ev_ctx_break(verto_mod_ctx *ctx)
{
    ev_break(ctx, EVBREAK_ONE);
}

static void
k5ev_ctx_reinitialize(verto_mod_ctx *ctx)
{
    ev_loop_fork(ctx);
}

static void
libev_callback(EV_P_ ev_watcher *w, int revents)
{
    verto_ev_flag state = VERTO_EV_FLAG_NONE;

    if (verto_get_type(w->data)== VERTO_EV_TYPE_CHILD)
        verto_set_proc_status(w->data, ((ev_child*) w)->rstatus);

    if (revents & EV_READ)
        state |= VERTO_EV_FLAG_IO_READ;
    if (revents & EV_WRITE)
        state |= VERTO_EV_FLAG_IO_WRITE;
    if (revents & EV_ERROR)
        state |= VERTO_EV_FLAG_IO_ERROR;

    verto_set_fd_state(w->data, state);
    verto_fire(w->data);
}

static void
k5ev_ctx_set_flags(verto_mod_ctx *ctx, const verto_ev *ev,
		   verto_mod_ev *evpriv)
{
    if (verto_get_type(ev) == VERTO_EV_TYPE_IO) {
        int events = EV_NONE;

        if (verto_get_flags(ev) & VERTO_EV_FLAG_IO_READ)
            events |= EV_READ;
        if (verto_get_flags(ev) & VERTO_EV_FLAG_IO_WRITE)
            events |= EV_WRITE;

        ev_io_stop(ctx, (ev_io*) evpriv);
        ev_io_set(((ev_io*) evpriv), verto_get_fd(ev), events);
        ev_io_start(ctx, (ev_io*) evpriv);
    }
}

#define setuptype(type, ...) \
    w.type = malloc(sizeof(ev_ ## type)); \
    if (w.type) { \
    	ev_ ## type ## _init(w.type, (EV_CB(type, (*))) __VA_ARGS__); \
    	ev_ ## type ## _start(ctx, w.type); \
    } \
    break

static verto_mod_ev *
k5ev_ctx_add(verto_mod_ctx *ctx, const verto_ev *ev, verto_ev_flag *flags)
{
    union {
       ev_watcher *watcher;
       ev_io *io;
       ev_timer *timer;
       ev_idle *idle;
       ev_signal *signal;
       ev_child *child;
    } w;
    ev_tstamp interval;

    w.watcher = NULL;
    *flags |= VERTO_EV_FLAG_PERSIST;
    switch (verto_get_type(ev)) {
        case VERTO_EV_TYPE_IO:
            setuptype(io, libev_callback, verto_get_fd(ev), EV_NONE);
        case VERTO_EV_TYPE_TIMEOUT:
            interval = ((ev_tstamp) verto_get_interval(ev)) / 1000.0;
            setuptype(timer, libev_callback, interval, interval);
        case VERTO_EV_TYPE_IDLE:
            setuptype(idle, libev_callback);
        case VERTO_EV_TYPE_SIGNAL:
            setuptype(signal, libev_callback, verto_get_signal(ev));
        case VERTO_EV_TYPE_CHILD:
            *flags &= ~VERTO_EV_FLAG_PERSIST; /* Child events don't persist */
            setuptype(child, libev_callback, verto_get_proc(ev), 0);
        default:
            break; /* Not supported */
    }

    if (w.watcher) {
        w.watcher->data = (void*) ev;
        k5ev_ctx_set_flags(ctx, ev, w.watcher);
    }
    return w.watcher;
}

static void
k5ev_ctx_del(verto_mod_ctx *ctx, const verto_ev *ev, verto_mod_ev *evpriv)
{
    switch (verto_get_type(ev)) {
        case VERTO_EV_TYPE_IO:
            ev_io_stop(ctx, (ev_io*) evpriv);
            break;
        case VERTO_EV_TYPE_TIMEOUT:
            ev_timer_stop(ctx, (ev_timer*) evpriv);
            break;
        case VERTO_EV_TYPE_IDLE:
            ev_idle_stop(ctx, (ev_idle*) evpriv);
            break;
        case VERTO_EV_TYPE_SIGNAL:
            ev_signal_stop(ctx, (ev_signal*) evpriv);
            break;
        case VERTO_EV_TYPE_CHILD:
            ev_child_stop(ctx, (ev_child*) evpriv);
            break;
        default:
            break;
    }

    free(evpriv);
}

verto_ctx *verto_new_k5ev(void);
verto_ctx *verto_default_k5ev(void);

VERTO_MODULE(k5ev, NULL,
             VERTO_EV_TYPE_IO |
             VERTO_EV_TYPE_TIMEOUT |
             VERTO_EV_TYPE_IDLE |
             VERTO_EV_TYPE_SIGNAL |
             VERTO_EV_TYPE_CHILD);
