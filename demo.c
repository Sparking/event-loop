#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "event-loop.h"

static int singal_cb(event_type_t *event)
{
    int *count;
    int signo;

    count = event_loop_event_arg(event);
    signo = event_loop_event_signo(event);
    if (count == NULL) {
        fprintf(stderr, "get signal %d, signal not get param, exit signal callback.\n", signo);
        event_loop_cancel(event);
        return 0;
    }

    if (signo == SIGINT) {
        fprintf(stdout, "get intterrupt\n");
        *count += 1;
        fprintf(stdout, "interrupt call time %d\n", *count);
        if (*count >= 5) {
            fprintf(stdout, "interrupt call time count to up-limit, exit signal callback.\n");
            event_loop_cancel(event);
            return 0;
        }
    } else {
        fprintf(stdout, "get signal: %d\n", signo);
    }

    return 0;
}

static int timer(event_type_t *event)
{
    int *arg;
    time_t t;
    struct tm ft;
    char datetime[32];

    t = time(NULL);
    (void)localtime_r(&t, &ft);
    (void)strftime(datetime, sizeof(datetime), "%F %T", &ft);
    (void)fprintf(stdout, "name : %s\ntime : %s\n", event_loop_event_name(event),
            datetime);
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

static int sub_ps_cb(int status, void *arg)
{
    time_t t;
    struct tm ft;
    char datetime[32];

    t = time(NULL);
    (void)localtime_r(&t, &ft);
    (void)strftime(datetime, sizeof(datetime), "%F %T", &ft);
    (void)fprintf(stdout, "process exit code : %d\ntime : %s\n", status, datetime);

    return status;
}

int main(void)
{
    pid_t pid;
    sigset_t mask;
    event_loop_t *loop;
    event_type_t *event;
    int loop_timer_calls[2];
    int signal_count;
    char *sleep_ps_arg[] = {"sleep", "3", NULL};

    loop = event_loop_create();
    if (loop == NULL) {
        return -1;
    }

    pid = event_loop_create_process(loop, sub_ps_cb, NULL, sleep_ps_arg[0], sleep_ps_arg);
    if (pid <= 0) {
        fprintf(stderr, "add process handler failed!");
    } else {
        fprintf(stderr, "process %d has been registered.\n", pid);
    }

    (void)sigemptyset(&mask);
    (void)sigaddset(&mask, SIGINT);
    (void)sigaddset(&mask, SIGABRT);
    signal_count = 0;
    event = event_loop_create_signal(loop, singal_cb, "signal", &signal_count,
            &mask);
    if (event == NULL) {
        goto exit;
    }

    event = event_loop_create_signal(loop, singal_cb, "signal2", &signal_count,
            &mask);
    if (event == NULL) {
        fprintf(stderr, "fail to create signal2 event\n");
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
    event = event_loop_create_loop_timer(loop, timer, "loop timer1",
            &loop_timer_calls[0], 5);
    if (event == NULL) {
        goto exit;
    }

    loop_timer_calls[1] = 0;
    event = event_loop_create_loop_timer(loop, timer, "loop timer2",
            &loop_timer_calls[1], 6);
    if (event == NULL) {
        goto exit;
    }

    event_loop_run(loop);
exit:
    fprintf(stderr, "event_loop exit\n");
    event_loop_destroy(loop);

    return -1;
}