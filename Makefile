all: libnss_docker.so.2
	gcc libnss_docker.so.2 main.c
	
libnss_docker.so.2:
	gcc -shared -fPIC -lcurl -ljansson -Wall -o libnss_docker.so.2 libnss_docker.c

clean:
	rm a.out libnss_docker.so.2 > /dev/null 2>&1

.PHONY: all clean
