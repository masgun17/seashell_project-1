CC = gcc

all: clean install run

install: seashell

clean:
	rm -rf seashell

run: seashell
	./seashell

seashell: seashell.c
	$(CC) -o seashell seashell.c


