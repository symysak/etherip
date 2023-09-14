PROGRAM = etherip
OBJS = etherip.o \

SRCS = $(OBJS:%.o=%.c)
CC = gcc
CFLAGS := -W -Wall -iquote .
LDFLAGS :=

ifeq ($(shell uname),Linux)
  BASE = platform/linux
  CFLAGS := $(CFLAGS) -pthread -iquote $(BASE)
  OBJS := $(OBJS) $(BASE)/tap.o $(BASE)/socket.o
endif

all: $(PROGRAM)

$(PROGRAM):$(OBJS)
		$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)

clean: $(PROGRAM)
		rm -f *.o $(PROGRAM)
		rm -f platform/linux/*.o platform/linux/*~

install: $(PROGRAM)
		cp $(PROGRAM) /usr/local/sbin

remove: $(PROGRAM)
		rm -f /usr/local/sbin/$(PROGRAM)