CC=$(CROSS_COMPILE)gcc

all:
	$(CC) -o libvpu_encode libvpu_encode.c -lvpu -lpthread
	$(CC) -o ipukms_csc ipukms_csc.c -I /usr/include/libpng/  -lpng -ldrm

