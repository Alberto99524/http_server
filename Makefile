CC = gcc
CFLAGS = -Wall -Wextra -g -pthread -MMD -MP
LDFLAGS = -pthread

TARGET = server
SRCS = server.c http_parser.c http_response_builder.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

.PHONY: all clean
