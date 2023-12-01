CC = gcc
CFLAGS = -O2 -lpthread
TARGET = server

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) -o $(TARGET) $(TARGET).c $(CFLAGS) 

clean:
	rm -f $(TARGET)
