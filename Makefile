out_binaries=kilo

all: clean build

build: kilo.c
	$(CC) kilo.c -g -o $(out_binaries) -Wall -Wextra -pedantic -std=c99

kilo: build

clean:
	rm -f $(out_binaries)
	rm -f -r "$(out_binaries).dSYM"

run: all
	./$(out_binaries)
