all: kilo

kilo: kilo.c
	$(CC) -o kilo kilo.c -Wall -W -Wcpp -pedantic -std=c99

clean:
	rm kilo
