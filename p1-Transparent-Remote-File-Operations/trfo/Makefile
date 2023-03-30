CFLAGS+=-Wall -fPIC -DPIC -I../include
LDFLAGS=-L../lib
LDLIBS=-ldirtree

all: clean mylib.so server

serde.o:
	gcc -Wall -fPIC -DPIC -c -g serde.c

mylib.o: mylib.c
	gcc -Wall -fPIC -DPIC -c mylib.c serde.c -I../include

mylib.so: serde.o mylib.o
	ld -shared -o mylib.so serde.o mylib.o -ldl -L../lib

server: serde.c server.c ../lib/libdirtree.so

clean:
	rm -f server *.o *.so *.h.gch
