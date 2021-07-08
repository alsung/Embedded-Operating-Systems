CFLAGS=-Wall -Wextra -Wpedantic -Wshadow -std=c99 -O2

all: myshell

myshell: myshell.o lex.yy.o
	sudo cc $(CFLAGS) -o myshell myshell.o lex.yy.o -lfl
	sudo chmod -R 777 ../asgn1

myshell.o: myshell.c
	sudo cc $(CFLAGS) -c myshell.c

lex.yy.o: shell.l
	sudo flex shell.l
	sudo cc $(CFLAGS) -c lex.yy.c

clean:
	sudo rm -f myshell.o lex.yy.o lex.yy.c 

spotless: clean
	sudo rm -f myshell
