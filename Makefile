CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
TARGET  = myShell
OBJS    = myShell.o vector.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

myShell.o: myShell.c vector.h
	$(CC) $(CFLAGS) -c myShell.c

vector.o: vector.c vector.h
	$(CC) $(CFLAGS) -c vector.c

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
