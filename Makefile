
compile: test_server.c string_lib.c
	gcc -Wall -Wextra -o test_server test_server.c string_lib.c -g

run: compile
	./test_server
