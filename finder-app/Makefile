CC = $(CROSS-COMPILE)gcc
CFLAGS = -g -Wall
TARGET = writer

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -c -o $(TARGET).o $(TARGET).c
	$(CC) $(CFLAGS) -I/ -o $(TARGET) $(TARGET).o

clean:
	rm -f $(TARGET) *.o *.elf *.map