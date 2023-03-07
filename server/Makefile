CC ?= $(CROSS-COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -pthread -lrt

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -c -o $(TARGET).o $(TARGET).c $(LDFLAGS)
	$(CC) $(CFLAGS) -I/ -o $(TARGET) $(TARGET).o $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o *.elf *.map *.out