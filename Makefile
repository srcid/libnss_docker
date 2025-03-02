all: libnss_docker.so.2
	gcc -g libnss_docker.so.2 main.c
	
libnss_docker.so.2: libnss_docker.c
	gcc -g -shared -fPIC -lcurl -ljansson -Wall -o libnss_docker.so.2 libnss_docker.c

clean:
	rm a.out libnss_docker.so.2 > /dev/null 2>&1

.PHONY: all clean
