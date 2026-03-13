
compile: test_server.c 
	gcc -Wall -Wextra -o test_server test_server.c sha1.c -g

run: compile
	./test_server
