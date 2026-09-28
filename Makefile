CC = gcc
CFLAGS = -Wall -Wextra -g -I.
SRCS = ./rcn.c ./rcn_daemon.c ./rcn_epoll.c ./rcn_relay.c ./rcn_util.c ./rcn_arg.c ./rcn_device.c ./rcn_peer.c ./rcn_stream.c
TARGET = rcn

all:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

debug:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all debug clean
