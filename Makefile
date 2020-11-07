CC := gcc

CPPFLAGS := -Wall -Werror -std=gnu99 -MMD -Iinclude
CFLAGS   := -g -O0
LDFLAGS  :=
LIBS     :=

src  := event-loop.c demo.c
objs := $(patsubst %.c,%.o,$(src))
deps := $(patsubst %.c,%.d,$(src))

out  := run.elf

$(out): $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

-include $(deps)

$(objs): %.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

