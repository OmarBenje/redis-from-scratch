CC := gcc
CFLAGS := -Wall -Wextra -Werror -g -std=c11 -fsanitize=address,undefined
LDFLAGS := -fsanitize=address,undefined
TARGET := server
SRCS := main.c
OBJS := $(SRCS:.c=.o)
HEADERS :=

.PHONY: all clean fclean re

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(TARGET)

re: fclean all

