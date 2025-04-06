#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_http_server.h"

// Starts the HTTP server and returns the server handle.
httpd_handle_t start_webserver(void);

#endif // WEBSERVER_H
