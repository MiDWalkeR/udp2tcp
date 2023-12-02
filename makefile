CC = gcc
CFLAGS = -O2 -lpthread -lrt -Wall
TARGET = receiver

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) -o $(TARGET) $(TARGET).c $(CFLAGS) 

clean:
	rm -f $(TARGET)
