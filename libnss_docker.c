#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <netdb.h>
#include <nss.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IPV4_STR_SIZE 16 /* 12 digits for numbers, 3 for dots, and last one for \0 */

#define DOCKER_UNIX_SOCKET_PATH  "/var/run/docker.sock"
#define DOCKER_URL_TEMPLATE      "http://localhost/v1.47/containers/%s/json"
#define DOCKER_DOMAIN_SUFFIX     ".docker"
#define DOCKER_DOMAIN_SUFFIX_LEN 7
#define DOCKER_NAME_LENGTH       129

struct Response {
    char* content;
    size_t size;
};

size_t write_callback(void* contents, size_t size, size_t nmemb, void* user_data)
{
    size_t realsize           = size * nmemb;
    struct Response* response = (struct Response*) user_data;

    char* ptr                 = (char*) malloc(sizeof(char) * (realsize + 1));

    if (!ptr) {
        /* out of memory! */
        printf("not enough memory (malloc returned NULL)\n");
        return 0;
    }

    response->content = ptr;
    memcpy(response->content, contents, realsize);
    response->size += realsize;
    response->content[response->size] = '\0';

    return realsize;
}

struct Response* fetch_container_json(const char* container_name_or_id)
{
    CURL* curl;
    CURLcode res;
    const size_t docker_url_size = 1024;
    char docker_url[docker_url_size];
    struct Response* response = (struct Response*) malloc(sizeof(struct Response));
    response->content         = NULL;
    response->size            = 0;
    curl                      = curl_easy_init();

    snprintf(docker_url, docker_url_size, DOCKER_URL_TEMPLATE, container_name_or_id);
    curl_easy_setopt(curl, CURLOPT_URL, docker_url);
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, DOCKER_UNIX_SOCKET_PATH);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) response);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: [%d] %s\n", res, curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);

    return response;
}

char* get_container_ip(const char* container_name_or_id)
{
    struct Response* response = fetch_container_json(container_name_or_id);
    json_error_t error;
    json_t* root = json_loads(response->content, 0, &error);

    /* clearing response, as it's not neeeded anymore */
    free(response->content);
    free(response);

    if (!root) {
        /* Couldn't convert response string to JSON */
        fprintf(stderr, "Failed to convert to JSON: %s\n", error.text);
        return NULL;
    }

    json_t* state      = json_object_get(root, "State");
    const char* status = json_string_value(json_object_get(state, "Status"));

    if (state == NULL || status == NULL || strcmp(status, "running") != 0) {
        /* Container it's not running, then it can have an IP. Ignoring it. */
        return NULL;
    }

    json_t* network_settings = json_object_get(root, "NetworkSettings");
    json_t* networks         = json_object_get(network_settings, "Networks");
    json_t* bridge           = json_object_get(networks, "bridge");

    if (network_settings == NULL || networks == NULL || bridge == NULL) {
        /* Container it's not on bridge network. Ignoring it. */
        return NULL;
    }

    const char* ip = json_string_value(json_object_get(bridge, "IPAddress"));
    char* res_ip   = (char*) malloc(sizeof(char) * IPV4_STR_SIZE);

    strcpy(res_ip, ip);

    /* clearing JSON object */
    json_decref(root);

    return res_ip;
}

enum nss_status _nss_docker_gethostbyname_r(const char* name, struct hostent* result, char* buf,
                                            size_t buflen, int* errnop, int* h_errnop)
{
    const size_t name_len = strlen(name);
    int is_docker_domain  = strcmp(&name[name_len - DOCKER_DOMAIN_SUFFIX_LEN],
                                   DOCKER_DOMAIN_SUFFIX); /* 0 if it's equal */

    if (is_docker_domain != 0) {
        *errnop = ENOENT;
        return NSS_STATUS_UNAVAIL;
    }

    char container_name[DOCKER_NAME_LENGTH];
    strncpy(container_name, name, name_len - DOCKER_DOMAIN_SUFFIX_LEN);
    container_name[name_len - DOCKER_DOMAIN_SUFFIX_LEN] = '\0';
    char* ip                                            = get_container_ip(container_name);

    if (ip == NULL) return NSS_STATUS_NOTFOUND;

    struct in_addr ip_addr;

    if (inet_pton(AF_INET, ip, &ip_addr) != 1) {
        *errnop = EINVAL;
        return NSS_STATUS_UNAVAIL;
    }

    free(ip);

    const size_t required_buflen = name_len + 1             // size of name plus one byte for '\0'
                                   + sizeof(struct in_addr) // size of the ip address
                                   + sizeof(char*) * 2; // size of the arrays aliases and addr_list

    if (buflen < required_buflen) {
        *errnop = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }

    char** aliases   = (char**) buf;
    char** addr_list = (char**) (buf + sizeof(char*) * 2);
    char* h_name     = (char*) (buf + sizeof(char*) * 2 + sizeof(char*) * 2);
    struct in_addr* addr =
        (struct in_addr*) (buf + sizeof(char*) * 2 + sizeof(char*) * 2 + strlen(name) + 1);

    strcpy(h_name, name);

    aliases[0]          = NULL;
    addr_list[0]        = (char*) addr;
    addr_list[1]        = NULL;
    *addr               = ip_addr;

    result->h_name      = h_name;
    result->h_aliases   = aliases;
    result->h_addrtype  = AF_INET;
    result->h_length    = sizeof(struct in_addr);
    result->h_addr_list = addr_list;

    return NSS_STATUS_SUCCESS;
}

enum nss_status _nss_docker_gethostbyname2_r(const char* name, int af, struct hostent* result,
                                             char* buf, size_t buflen, int* errnop, int* h_errnop)
{
    if (af != AF_INET) {
        *errnop = ENOENT;
        return NSS_STATUS_UNAVAIL;
    }

    return _nss_docker_gethostbyname_r(name, result, buf, buflen, errnop, h_errnop);
}
