PROGRAM = etherip
OBJS = etherip.o \

SRCS = $(OBJS:%.o=%.c)
CC = gcc
CFLAGS = -W -Wall -iquote .
LDFLAGS =

ifeq ($(shell uname),Linux)
  BASE = platform/linux
  CFLAGS := $(CFLAGS) -pthread -iquote $(BASE)
  OBJS := $(OBJS) $(BASE)/tap.o $(BASE)/socket.o
endif

$(PROGRAM):$(OBJS)
		$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)