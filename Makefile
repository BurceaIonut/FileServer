.PHONY: clean
build: 
	gcc server.c -lpthread -o server
clean: rm -f *.o server