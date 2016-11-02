CC=gcc
CFLAGS=-I. -O2 -g -Wall $(shell pkg-config --cflags gio-unix-2.0)
LDFLAGS=$(shell pkg-config --libs gio-unix-2.0)
TARGET=sensortag-hid
OBJ=sensortag-hid.o bluez-gatt-client.o uhid.o

all : $(TARGET)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean: 
	rm  -f ./*.o
	rm -f $(TARGET)
