#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "event-loop.h"

static int timer(event_type_t *event)
{
    int *arg;
    time_t t;
    struct tm ft;
    char datetime[32];

    t = time(NULL);
    (void)localtime_r(&t, &ft);
    (void)strftime(datetime, sizeof(datetime), "%F %T", &ft);
    (void)fprintf(stdout, "time : %s\nname : %s\n", datetime,
            event_loop_event_name(event));
    arg = event_loop_event_arg(event);
    if (event->type == EVENT_TYPE_TIMER && arg != NULL) {
        if (++(*arg) >= 5) {
            (void)fprintf(stdout, "loop timer quit now.\n");
            event_loop_cancel(event);
            return 0;
        }
    }
    (void)fprintf(stdout, "\n");
    (void)fflush(stdout);

    return 0;
}

int main(void)
{
    event_loop_t *loop;
    event_type_t *event;
    int loop_timer_calls[2];

    loop = event_loop_create();
    if (loop == NULL) {
        return -1;
    }

    event = event_loop_create_timer(loop, timer, "one-shot timer0", NULL, 0);
    if (event == NULL) {
        goto exit;
    }

    event = event_loop_create_timer(loop, timer, "one-shot timer1", NULL, 10);
    if (event == NULL) {
        goto exit;
    }

    event = event_loop_create_timer(loop, timer, "one-shot timer2", NULL, 20);
    if (event == NULL) {
        goto exit;
    }

    loop_timer_calls[0] = 0;
    event = event_loop_create_loop_timer(loop, timer, "loop timer", &loop_timer_calls[0], 5);
    if (event == NULL) {
        goto exit;
    }

    loop_timer_calls[1] = 0;
    event = event_loop_create_loop_timer(loop, timer, "loop timer", &loop_timer_calls[1], 6);
    if (event == NULL) {
        goto exit;
    }

    event_loop_run(loop);
exit:
    fprintf(stderr, "event_loop exit\n");
    event_loop_destroy(loop);

    return -1;
}