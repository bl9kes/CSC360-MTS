
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
TARGET = mts

all: $(TARGET)

$(TARGET): mts.c
	$(CC) $(CFLAGS) -o $(TARGET) mts.c

clean:
	rm -f $(TARGET) output.txt
