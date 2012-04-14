CC=$(CROSS_COMPILE)gcc

%.o: %.c
	$(CC) -O2 -c $^ -o $@

all: libvpu_encode.o
	gcc -o libvpu_encode libvpu_encode.o -lvpu -lpthread
