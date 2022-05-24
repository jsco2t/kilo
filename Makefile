out_binaries=kilo

all: clean kilo.c
	$(CC) kilo.c -o $(out_binaries) -Wall -Wextra -pedantic -std=c99

kilo: kilo.c
	$(CC) kilo.c -o $(out_binaries) -Wall -Wextra -pedantic -std=c99

clean:
	rm -f $(out_binaries)

run: all
	./$(out_binaries)
