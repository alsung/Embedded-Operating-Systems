# Makefile for building test code
test: ddfs_test.c edge_test.o func_test.o rand_test.o
	cc -g -o test ddfs_test.c edge_test.o func_test.o rand_test.o

edge_test.o: edge_test.c edge_test.h
	cc -g -c -Wall -Wextra -Wpedantic -Wshadow -std=c99 -O2 edge_test.c

func_test.o: func_test.c func_test.h
	cc -g -c -Wall -Wextra -Wpedantic -Wshadow -std=c99 -O2 func_test.c

rand_test.o: rand_test.c rand_test.h
	cc -g -c -Wall -Wextra -Wpedantic -Wshadow -std=c99 -O2 rand_test.c

run: test
	./test

clean:
	# source: https://linuxconfig.org/how-to-remove-all-files-and-directories-owned-by-a-specific-user-on-linux
	find /mnt -user root -exec rm -fr /mnt/{} \;
	rm -rf /mnt/open_test.txt /mnt/test_link /mnt/test_link_new /mnt/file*
	rm -rf test *.o *.tmp
