#ifndef _EVENT_LOOP_H_
#define _EVENT_LOOP_H_

#include <time.h>
#include <stddef.h>
#include <sys/epoll.h>
#include "list.h"

#define EVENT_LOOP_INLINE               __attribute__((always_inline)) static inline 
#define EVENT_TYPE_NAME_LEN             16
#define EVENT_LOOP_MAX_SHIFT_BITS       10

typedef struct event_loop_s event_loop_t;
typedef struct event_type_s event_type_t;
typedef int (*event_func_t)(event_type_t *);

enum event_type_e {
    EVENT_TYPE_READ,
    EVENT_TYPE_WRITE,
    EVENT_TYPE_TIMER,
};

struct event_type_s {
    struct list_head    node;
    enum event_type_e   type;
    event_func_t        handler;
    event_loop_t       *loop;
    void               *arg;

#define EVENT_F_ONESHOT                 (1 << 0)
#define EVENT_F_CANCEL                  (1 << 1)

    int                 flag;
    int                 fd;
    char                name[EVENT_TYPE_NAME_LEN];
};

struct event_loop_s {
    size_t              event_size;
    struct list_head    event_head;

    struct list_head    event_unused;

    event_type_t       *event_current;

    int                 epoll_fd;
    int                 epoll_volume;
    int                 epoll_fd_max;
    struct epoll_event *epoll_events;
    int                 epoll_get_cnt;       
};

EVENT_LOOP_INLINE int event_loop_event_fd(event_type_t *event)
{
    return event->fd;
}

EVENT_LOOP_INLINE void *event_loop_event_arg(event_type_t *event)
{
    return event->arg;
}

EVENT_LOOP_INLINE const char *event_loop_event_name(event_type_t *event)
{
    return event->name;
}

extern event_loop_t *event_loop_create(void);

extern event_type_t *event_loop_wait(event_loop_t *event_loop);

extern int event_loop_deal_event(event_type_t *event);

extern void event_loop_run(event_loop_t *event_loop);

extern void event_loop_destroy(event_loop_t *event_loop);

extern event_type_t *event_loop_create_read(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, int fd);

extern event_type_t *event_loop_create_timer(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, time_t time);

extern event_type_t *event_loop_create_timer_timespec(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, struct timespec time);

extern event_type_t *event_loop_create_loop_timer(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, time_t time);

extern event_type_t *event_loop_create_loop_timer_itimerspec(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, struct itimerspec time);

extern event_type_t *event_loop_create_signal(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg, int fd, const sigset_t *mask);

extern event_type_t *event_loop_create_event(event_loop_t *event_loop,
        event_func_t handler, const char *name, void *arg);

extern void event_loop_cancel(event_type_t *event);

#endif /* _EVENT_LOOP_H_ */