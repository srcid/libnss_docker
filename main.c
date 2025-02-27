#include <stdio.h>
#include <stdlib.h>

char* get_container_ip(const char* container_name_or_id);

int main(int argc, char const *argv[])
{
    if (argc != 2) return 0;

    char* ip = get_container_ip(argv[1]);

    if (ip == NULL) return 0;

    printf("IP: %s\n", ip);
    free(ip);
    
    return 0;
}
