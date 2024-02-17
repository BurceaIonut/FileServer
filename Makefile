.PHONY: clean
build: 
	gcc server.c -lpthread -o server
	gcc client.c -o client
clean: rm -f *.o server