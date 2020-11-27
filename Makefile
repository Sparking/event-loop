CC := gcc

CPPFLAGS := -Wall -Werror -std=gnu99 -MMD -Iinclude -D_GNU_SOURCE
CFLAGS   := -g -O0 -shared -fPIC
LDFLAGS  :=
LIBS     :=

src  := event-loop.c rbtree.c
objs := $(patsubst %.c,%.o,$(src))
deps := $(patsubst %.c,%.d,$(src))

out  := libevent-loop.so

.PHONY: all
all: run.elf

$(out): $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

-include $(deps)

$(objs): %.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	$(RM) $(objs)
	$(RM) $(deps)
	$(RM) $(out)

run.elf: demo.c $(out)
	$(CC) $(CPPFLAGS) -g -O0 -Wl,-rpath=. -o $@ $< -L. -levent-loop $(LIBS)
