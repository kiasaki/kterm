run: build
	./kterm
build:
	cc -g -o kterm main.c lib/libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11
