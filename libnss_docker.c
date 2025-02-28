#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <jansson.h>

#define DOCKER_UNIX_SOCKET_PATH "/var/run/docker.sock"
#define DOCKER_URL_TEMPLATE "http://localhost/v1.47/containers/%s/json"
#define IPV4_STR_SIZE 16 /* 12 digits for numbers, 3 for dots, and last one for \0 */

struct Response {
    char* content;
    size_t size;
};

size_t write_callback(void* contents, size_t size, size_t nmemb, void* user_data)
{
    size_t realsize = size * nmemb;
    struct Response* response = (struct Response*) user_data;

    char *ptr = (char *) malloc(sizeof(char) * (realsize + 1));

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
    struct Response* response = (struct Response* ) malloc(sizeof(struct Response));
    response->content = NULL;
    response->size = 0;
    curl = curl_easy_init();
    
    snprintf(docker_url, docker_url_size, DOCKER_URL_TEMPLATE, container_name_or_id);
    curl_easy_setopt(curl, CURLOPT_URL, docker_url);
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, DOCKER_UNIX_SOCKET_PATH);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)response);
    
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr,
            "curl_easy_perform() failed: [%d] %s\n",
            res,
            curl_easy_strerror(res)
        );
    }

    curl_easy_cleanup(curl);

    return response;
}

char* get_container_ip(const char* container_name_or_id)
{
    struct Response* response = fetch_container_json(container_name_or_id);
    json_error_t error;
    json_t *root = json_loads(response->content, 0, &error);

    /* clearing response, as it's not neeeded anymore */
    free(response->content);
    free(response);

    if (!root) {
        /* Couldn't convert response string to JSON */
        fprintf(stderr, "Failed to convert to JSON: %s\n", error.text);
        return NULL;
    }

    json_t* state = json_object_get(root, "State");
    const char* status = json_string_value(json_object_get(state, "Status"));

    if (state == NULL || status == NULL || strcmp(status, "running") != 0) {
        /* Container it's not running, then it can have an IP. Ignoring it. */
        return NULL;
    }
    
    json_t* network_settings = json_object_get(root, "NetworkSettings");
    json_t* networks = json_object_get(network_settings, "Networks");
    json_t* bridge = json_object_get(networks, "bridge");
    
    if (network_settings == NULL || networks == NULL || bridge == NULL) {
        /* Container it's not on bridge network. Ignoring it. */
        return NULL;
    }
    
    const char* ip = json_string_value(json_object_get(bridge, "IPAddress"));
    char* res_ip = (char*) malloc(sizeof(char) * IPV4_STR_SIZE);

    strcpy(res_ip, ip);

    /* clearing JSON object */
    json_decref(root);

    return res_ip;
}

