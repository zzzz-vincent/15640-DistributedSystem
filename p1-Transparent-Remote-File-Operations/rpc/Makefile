all: mylib.so server

mylib.o: mylib.c
	gcc -Wall -fPIC -DPIC -c mylib.c -I../include

mylib.so: mylib.o
	ld -shared -o mylib.so mylib.o -ldl -L../lib

clean:
	rm -f *.o *.so server