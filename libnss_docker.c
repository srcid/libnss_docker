#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <netdb.h>
#include <nss.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IPV4_STR_LEN 16 /* 12 digits for numbers, 3 for dots, and last one for \0 */

#define DOCKER_UNIX_SOCKET_PATH  "/var/run/docker.sock"
#define DOCKER_URL_TEMPLATE      "http://localhost/v1.47/containers/%s/json"
#define DOCKER_DOMAIN_SUFFIX     ".docker"
#define DOCKER_DOMAIN_SUFFIX_LEN 7
#define DOCKER_NAME_LEN          129
#define DOCKER_ID_LEN            65
#define DOCKER_STATUS_LEN        16

struct Response {
    char *content;
    size_t size;
};

size_t write_callback(void *contents, size_t size, size_t nmemb, void *user_data)
{
    size_t realsize           = size * nmemb;
    struct Response *response = (struct Response *) user_data;
    char *ptr                 = (char *) malloc(sizeof(char) * (realsize + 1));

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

struct Response *fetch_container_json(const char *container_name_or_id)
{
    CURL *curl;
    CURLcode res;
    const size_t docker_url_size = 1024;
    char docker_url[docker_url_size];
    struct Response *response = (struct Response *) malloc(sizeof(struct Response));
    response->content         = NULL;
    response->size            = 0;
    curl                      = curl_easy_init();

    snprintf(docker_url, docker_url_size, DOCKER_URL_TEMPLATE, container_name_or_id);
    curl_easy_setopt(curl, CURLOPT_URL, docker_url);
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, DOCKER_UNIX_SOCKET_PATH);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) response);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: [%d] %s\n", res, curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);

    return response;
}

struct DockerInfo {
    char id[DOCKER_ID_LEN];
    char name[DOCKER_NAME_LEN];
    char status[DOCKER_STATUS_LEN];
    char ip[IPV4_STR_LEN];
};

int get_docker_info(const char *container_name_or_id, struct DockerInfo *docker_info)
{
    struct Response *response;
    json_error_t error;
    json_t *json;

    response = fetch_container_json(container_name_or_id);
    json     = json_loads(response->content, 0, &error);

    /* clearing response, as it's not neeeded anymore */
    free(response->content);
    free(response);

    if (!json) {
        /* Couldn't convert response string to JSON */
        fprintf(stderr, "Failed to convert to JSON: %s\n", error.text);
        return 1;
    }

    char *id, *name, *status, *ip;
    int json_unpack_ex_status;

    json_unpack_ex_status = json_unpack_ex(
        json, &error, 0, "{ s:s, s:s, s:{s:s}, s:{s:{s:{s:s}}} }", "Id", &id, "Name", &name,
        "State", "Status", &status, "NetworkSettings", "Networks", "bridge", "IPAddress", &ip);

    if (json_unpack_ex_status) {
        /* Couldn't stract the required fields */
        fprintf(stderr, "Failed to extract fields: %s\n", error.text);
        return 2;
    }

    strcpy(docker_info->id, id);
    strcpy(docker_info->name, &name[1]); /* skip '/' character in the name at index 0 */
    strcpy(docker_info->status, status);
    strcpy(docker_info->ip, ip);

    /* clearing JSON object */
    json_decref(json);

    return 0;
}

/**
 * Check if ".docker" is suffix of a string
 *
 * @param[in] name string containing root domain
 * @return 1 if it's suffix is ".docker", 0 otherwise
 *
 * @pre name != NULL
 */
int is_docker_domain(const char *name)
{
    const size_t name_len = strlen(name);

    /* It must have at leat one character without ".docker" */
    if (name_len <= DOCKER_DOMAIN_SUFFIX_LEN) return 0;

    const char *suffix = &name[name_len - DOCKER_DOMAIN_SUFFIX_LEN];

    return !strcmp(suffix, DOCKER_DOMAIN_SUFFIX); /* strcmp returns 0 if it's equal */
}

enum nss_status _nss_docker_gethostbyname_r(const char *name, struct hostent *result, char *buf,
                                            size_t buflen, int *errnop, int *h_errnop)
{
    if (!is_docker_domain(name)) {
        *errnop = ENOENT;
        return NSS_STATUS_UNAVAIL;
    }

    const size_t name_len = strlen(name);
    char container_name_or_id[DOCKER_NAME_LEN];
    strncpy(container_name_or_id, name, name_len - DOCKER_DOMAIN_SUFFIX_LEN);
    container_name_or_id[name_len - DOCKER_DOMAIN_SUFFIX_LEN] = '\0';
    struct DockerInfo docker_info;
    int get_docker_info_status = get_docker_info(container_name_or_id, &docker_info);

    if (get_docker_info_status != 0) return NSS_STATUS_NOTFOUND;

    struct in_addr container_ip_addr;

    if (inet_pton(AF_INET, docker_info.ip, &container_ip_addr) != 1) {
        *errnop = EINVAL;
        return NSS_STATUS_UNAVAIL;
    }

    const size_t required_buflen = name_len + 1             // size of name plus one byte for '\0'
                                   + sizeof(struct in_addr) // size of the ip address
                                   + sizeof(char *) * 2; // size of the arrays aliases and addr_list

    if (buflen < required_buflen) {
        *errnop = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }

    char **aliases   = (char **) buf;
    char **addr_list = (char **) (buf + sizeof(char *) * 2);
    char *h_name     = (char *) (buf + sizeof(char *) * 2 + sizeof(char *) * 2);
    struct in_addr *addr =
        (struct in_addr *) (buf + sizeof(char *) * 2 + sizeof(char *) * 2 + strlen(name) + 1);

    strcpy(h_name, name);

    aliases[0]   = NULL;
    addr_list[0] = (char *) addr;
    addr_list[1] = NULL;
    *addr        = container_ip_addr;

    result->h_name      = h_name;
    result->h_aliases   = aliases;
    result->h_addrtype  = AF_INET;
    result->h_length    = sizeof(struct in_addr);
    result->h_addr_list = addr_list;

    return NSS_STATUS_SUCCESS;
}

enum nss_status _nss_docker_gethostbyname2_r(const char *name, int af, struct hostent *result,
                                             char *buf, size_t buflen, int *errnop, int *h_errnop)
{
    if (af != AF_INET) {
        *errnop = ENOENT;
        return NSS_STATUS_UNAVAIL;
    }

    return _nss_docker_gethostbyname_r(name, result, buf, buflen, errnop, h_errnop);
}
