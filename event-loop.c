#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include "event-loop.h"

static int event_loop_count_epoll_size(const int fd)
{
    int result;

    result = (((fd + 1) >> (EVENT_LOOP_MAX_SHIFT_BITS)) + 1) << (EVENT_LOOP_MAX_SHIFT_BITS);
    if (result < 0) {
        result = INT_MAX;
    }

    return result;
}

static int event_loop_reinit(event_loop_t *event_loop, int newfd)
{
    int epoll_fd;
    int epoll_volume;
    struct epoll_event *epoll_events;

    if (newfd < event_loop->epoll_volume) {
        if (newfd > event_loop->epoll_fd_max) {
            event_loop->epoll_fd_max = newfd;
        }

        return 0;
    }

    epoll_volume = event_loop_count_epoll_size(newfd);
    epoll_events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * epoll_volume);
    if (epoll_events == NULL) {
        return -1;
    }

    epoll_fd = epoll_create(epoll_volume);
    if (epoll_fd < 0) {
        free(epoll_events);
        return -1;
    }

    if (event_loop->epoll_fd >= 0) {
        (void)close(event_loop->epoll_fd);
    }

    if (event_loop->epoll_events != NULL) {
        free(event_loop->epoll_events);
    }

    if (newfd > event_loop->epoll_fd_max) {
        event_loop->epoll_fd_max = newfd;
    }

    event_loop->epoll_fd = epoll_fd;
    event_loop->epoll_events = epoll_events;
    event_loop->epoll_volume = epoll_volume;
    (void)memset(epoll_events, 0, sizeof(struct epoll_event) * epoll_volume);

    return 0;
}

static int event_loop_active_event(event_loop_t *event_loop, event_type_t *event)
{
    struct epoll_event ev;

    if (event_loop_reinit(event_loop, event->fd) != 0) {
        return -1;
    }

    ev.data.ptr = event;
    switch (event->type) {
    case EVENT_TYPE_READ:
        ev.events = EPOLLIN | EPOLLET;
        break;
    case EVENT_TYPE_WRITE:
        ev.events = EPOLLOUT | EPOLLET;
        break;
    case EVENT_TYPE_TIMER:
    case EVENT_TYPE_SIGNAL:
        ev.events = EPOLLIN | EPOLLET;
        break;
    case EVENT_TYPE_LINUX_EVENT:
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        break;
    }

    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, event->fd, &ev);
}

static int event_loop_add_event(event_loop_t *event_loop, event_type_t *event)
{
    int ret;

    ret = event_loop_active_event(event_loop, event);
    if (ret == 0) {
        ++event_loop->event_size;
        event->loop = event_loop;
        list_add_tail(&event->node, &event_loop->event_head);
    }

    return ret;
}

event_loop_t *event_loop_create(void)
{
    event_loop_t *event_loop;

    event_loop = (event_loop_t *)malloc(sizeof(event_loop_t));
    if (event_loop == NULL) {
        return NULL;
    }

    event_loop->event_size = 0;
    INIT_LIST_HEAD(&event_loop->event_head);
    INIT_LIST_HEAD(&event_loop->event_unused);
    (void)sigemptyset(&event_loop->event_sigset);
    event_loop->event_current = NULL;
    event_loop->epoll_fd = -1;
    event_loop->epoll_fd_max = -1;
    event_loop->epoll_volume = -1;
    event_loop->epoll_events = NULL;
    event_loop->epoll_get_cnt = 0;
    if (event_loop_reinit(event_loop, 0) != 0) {
        free(event_loop);
        event_loop = NULL;
    }

    return event_loop;
}

void event_loop_destroy(event_loop_t *event_loop)
{
    event_type_t *event;
    event_type_t *tmp;

    if (event_loop == NULL) {
        return;
    }

    event_loop->event_current = NULL;
    list_for_each_entry_safe(event, tmp, &event_loop->event_head, node) {
        event_loop_cancel(event);
    }

    if (event_loop->epoll_fd >= 0) {
        (void)close(event_loop->epoll_fd);
    }

    if (event_loop->epoll_events != NULL) {
        free(event_loop->epoll_events);
    }

    free(event_loop);
}

static event_type_t *event_malloc(event_loop_t *event_loop, enum event_type_e type,
        event_func_t handler, const char *name, void *arg, int fd)
{
    event_type_t *event;

    event = (event_type_t *)malloc(sizeof(event_type_t));
    if (event == NULL) {
        return NULL;
    }

    (void)memset(event, 0, sizeof(event_type_t));
    event->type = type;
    event->handler = handler;
    event->arg = arg;
    event->fd = fd;
    if (name != NULL) {
        strncpy(event->name, name, sizeof(event->name) - 1);
    }

    if (event_loop_add_event(event_loop, event) != 0) {
        free(event);
        event = NULL;
    }

    return event;
}

event_type_t *event_loop_create_read(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, int fd)
{
    if (event_loop == NULL || handler == NULL || fd < 0) {
        return NULL;
    }

    return event_malloc(event_loop, EVENT_TYPE_READ, handler, name, arg, fd);
}

event_type_t *event_loop_create_timer(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, time_t time)
{
    struct timespec t;

    t.tv_sec = time;
    t.tv_nsec = 0;

    return event_loop_create_timer_timespec(event_loop, handler, name, arg, t);
}

event_type_t *event_loop_create_timer_timespec(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, struct timespec time)
{
    struct itimerspec t;

    t.it_interval.tv_sec = 0;
    t.it_interval.tv_nsec = 0;
    t.it_value.tv_sec = time.tv_sec;
    t.it_value.tv_nsec = time.tv_nsec;
    if (t.it_value.tv_nsec == 0 && t.it_value.tv_sec == 0) {
        t.it_value.tv_nsec = 1;
    }

    return event_loop_create_loop_timer_itimerspec(event_loop, handler, name, arg, t);
}

event_type_t *event_loop_create_loop_timer(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, time_t time)
{
    struct itimerspec t;

    if (time == 0) {
        return NULL;
    }

    t.it_interval.tv_sec = time;
    t.it_interval.tv_nsec = 0;
    t.it_value.tv_sec = time;
    t.it_value.tv_nsec = 0;

    return event_loop_create_loop_timer_itimerspec(event_loop, handler, name, arg, t);
}

event_type_t *event_loop_create_loop_timer_itimerspec(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, struct itimerspec time)
{
    int ret;
    int timerfd;
    event_type_t *event;

    if (event_loop == NULL || handler == NULL) {
        return NULL;
    }

    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0) {
        return NULL;
    }

    ret = timerfd_settime(timerfd, 0, &time, NULL);
    if (ret < 0) {
        (void)close(timerfd);
        return NULL;
    }

    event = event_malloc(event_loop, EVENT_TYPE_TIMER, handler, name, arg, timerfd);
    if (event == NULL) {
        (void)close(timerfd);
    } else if (time.it_interval.tv_sec == 0 && time.it_interval.tv_nsec == 0) {
        event->flag |= EVENT_F_ONESHOT;
    }

    return event;
}

static int event_unmask_signal(sigset_t *dst, const sigset_t *set, const sigset_t *unmasked)
{
    int i;
    sigset_t tmp;

    for (i = 0; i < sizeof(tmp) / sizeof(unsigned long int); ++i) {
        ((unsigned long int *)&tmp)[i] = ~((unsigned long int *)unmasked)[i];
    }

    return sigandset(dst, set, &tmp);
}

event_type_t *event_loop_create_signal(event_loop_t *event_loop, event_func_t handler,
        const char *name, void *arg, const sigset_t *mask)
{
    int ret;
    int signal_fd;
    sigset_t tmpset;
    event_type_t *event;

    if (event_loop == NULL || handler == NULL || mask == NULL) {
        return NULL;
    }

    ret = event_unmask_signal(&tmpset, &event_loop->event_sigset, mask);
    if (ret != 0) {
        return NULL;
    }

    ret = memcmp(&tmpset, &event_loop->event_sigset, sizeof(sigset_t));
    if (ret != 0) {
        return NULL;
    }

    ret = sigprocmask(SIG_BLOCK, mask, NULL);
    if (ret != 0) {
        return NULL;
    }

    signal_fd = signalfd(-1, mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd < 0) {
        return NULL;
    }

    event = event_malloc(event_loop, EVENT_TYPE_SIGNAL, handler, name, arg, signal_fd);
    if (event == NULL) {
        (void)close(signal_fd);
    } else {
        (void)memcpy(&event->data.sig.set, mask, sizeof(sigset_t));
        ret = sigorset(&event_loop->event_sigset, &event_loop->event_sigset, mask);
        if (ret != 0) {
            event_loop_cancel(event);
            event = NULL;
        }
    }

    return event;
}

static void event_loop_remove_unused_event(event_type_t *event)
{
    list_del(&event->node);
    free(event);
}

void event_loop_cancel(event_type_t *event)
{
    if (event == NULL) {
        return;
    }

    --event->loop->event_size;
    list_del(&event->node);
    list_add_tail(&event->node, &event->loop->event_unused);
    (void)epoll_ctl(event->loop->epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
    switch (event->type) {
    case EVENT_TYPE_READ:
    case EVENT_TYPE_WRITE:
        break;
    case EVENT_TYPE_SIGNAL:
        (void)event_unmask_signal(&event->loop->event_sigset, &event->loop->event_sigset,
                &event->data.sig.set);
        (void)sigprocmask(SIG_UNBLOCK, &event->data.sig.set, NULL);
    case EVENT_TYPE_TIMER:
    case EVENT_TYPE_LINUX_EVENT:
        (void)close(event->fd);
        break;
    }

    if (event->loop->event_current != event) {
        event_loop_remove_unused_event(event);
    } else {
        event->flag |= EVENT_F_CANCEL;
    }
}

event_type_t *event_loop_wait(event_loop_t *event_loop)
{
    int cnt;
    event_type_t *unused;
    event_type_t *event;

    if (event_loop == NULL) {
        return NULL;
    }

    event_loop->event_current = NULL;
    cnt = event_loop->epoll_get_cnt;
    if (cnt <= 0) {
        (void)memset(event_loop->epoll_events, 0,
                sizeof(struct epoll_event) * event_loop->epoll_fd_max);
        list_for_each_entry_safe(unused, event, &event_loop->event_unused, node) {
            event_loop_remove_unused_event(unused);
        }

        while (1) {
            cnt = epoll_wait(event_loop->epoll_fd, event_loop->epoll_events,
                    event_loop->epoll_fd_max, 0);
            if (cnt < 0) {
                if (errno == EINTR) {
                    continue;
                }

                return NULL;
            } else if (cnt == 0) {
                if (event_loop->event_size != 0) {
                    continue;
                }

                return NULL;
            }

            break;
        }
    }

    event_loop->epoll_get_cnt = --cnt;
    event = (event_type_t *)event_loop->epoll_events[cnt].data.ptr;
    event_loop->event_current = event;

    return event;
}

int event_loop_deal_event(event_type_t *event)
{
    int ret;
    uint64_t timer_calls;
    struct signalfd_siginfo fdsi;
    event_loop_t *event_loop;

    if (event == NULL) {
        return -1;
    }

    event_loop = event->loop;
    if (event_loop == NULL || event->handler == NULL || event->fd < 0) {
        return -1;
    }

    switch (event->type) {
    case EVENT_TYPE_TIMER:
        ret = read(event->fd, &timer_calls, sizeof(uint64_t));
        if (ret != sizeof(uint64_t)) {
            event->data.timer_count = 0;
            return 0;
        }

        event->data.timer_count = timer_calls;
        break;
    case EVENT_TYPE_SIGNAL:
        ret = read(event->fd, &fdsi, sizeof(struct signalfd_siginfo));
        if (ret != sizeof(struct signalfd_siginfo)) {
            event->data.sig.no = 0;
            return 0;
        }

        event->data.sig.no = fdsi.ssi_signo;
        break;
    case EVENT_TYPE_READ:
    case EVENT_TYPE_WRITE:
    default:
        break;
    }

    ret = event->handler(event);
    if (event->flag & EVENT_F_ONESHOT) {
        event_loop_cancel(event);
    }

    if (event->flag & EVENT_F_CANCEL) {
        event_loop_remove_unused_event(event);
    }

    return ret;
}

void event_loop_run(event_loop_t *event_loop)
{
    event_type_t *event;

    while ((event = event_loop_wait(event_loop)) != NULL) {
        (void)event_loop_deal_event(event);
    }
}
