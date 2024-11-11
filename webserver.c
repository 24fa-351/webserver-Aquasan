//Alex Nguyen

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#define DEFAULT_PORT 80
#define MAX_BUFFER 1024
#define STATIC_DIR "./static"
#define MAX_STATS 100

// Global statistics
int    request_count = 0;
long   total_received_bytes = 0;
long   total_sent_bytes = 0;
HANDLE stats_lock;

// http://localhost:"port"/static/image.jpg
void serve_static(SOCKET client_sock, const char *path) {
    char file_path[MAX_BUFFER];
    snprintf(file_path, sizeof(file_path), "%s%s", STATIC_DIR, path);

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        const char *error_message =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 "
            "Not Found</h1>";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }

    // Send HTTP header
    const char *header =
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";
    send(client_sock, header, strlen(header), 0);
    total_sent_bytes += strlen(header);

    // Send the file contents
    char   buffer[MAX_BUFFER];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
        total_sent_bytes += bytes_read;
    }

    fclose(file);
}

// http://localhost:"port"/stats
void serve_stats(SOCKET client_sock) {
    char response[MAX_BUFFER];
    WaitForSingleObject(stats_lock, INFINITE);  // Lock mutex
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
             "<html><body><h1>Server Stats</h1>"
             "<p>Requests received: %d</p>"
             "<p>Total bytes received: %ld</p>"
             "<p>Total bytes sent: %ld</p>"
             "</body></html>",
             request_count, total_received_bytes, total_sent_bytes);
    ReleaseMutex(stats_lock);  // Unlock mutex
    send(client_sock, response, strlen(response), 0);
    total_sent_bytes += strlen(response);
}

// http://localhost:"port"/calc?a=1&b=2
void serve_calc(SOCKET client_sock, const char *query) {
    int a = 0, b = 0;
    if (sscanf(query, "a=%d&b=%d", &a, &b) == 2) {
        char response[MAX_BUFFER];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                 "<html><body><h1>Calculation Result</h1>"
                 "<p>%d + %d = %d</p></body></html>",
                 a, b, a + b);
        send(client_sock, response, strlen(response), 0);
        total_sent_bytes += strlen(response);
    } else {
        const char *error_message =
            "HTTP/1.1 400 Bad Request\r\nContent-Type: "
            "text/html\r\n\r\n<h1>400 Bad Request</h1>";
        send(client_sock, error_message, strlen(error_message), 0);
        total_sent_bytes += strlen(error_message);
    }
}

// Function to handle client requests
DWORD WINAPI handle_client(LPVOID client_sock_ptr) {
    SOCKET client_sock = (SOCKET)client_sock_ptr;
    char   buffer[MAX_BUFFER];
    int    bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == SOCKET_ERROR || bytes_received == 0) {
        closesocket(client_sock);
        return 0;
    }
    buffer[bytes_received] = '\0';

    // Parse the request
    request_count++;
    total_received_bytes += bytes_received;

    // Simple HTTP request parsing
    if (strncmp(buffer, "GET ", 4) == 0) {
        char *path_start = buffer + 4;
        char *path_end = strchr(path_start, ' ');
        if (path_end) {
            *path_end = '\0';
        }

        if (strncmp(path_start, "/static", 7) == 0) {
            serve_static(client_sock, path_start + 7);  // Serve static file
        } else if (strncmp(path_start, "/stats", 6) == 0) {
            serve_stats(client_sock);  // Serve stats
        } else if (strncmp(path_start, "/calc", 5) == 0) {
            char *query = strchr(path_start, '?');
            if (query) {
                serve_calc(client_sock, query + 1);  // Serve calc result
            }
        } else {
            const char *error_message =
                "HTTP/1.1 404 Not Found\r\nContent-Type: "
                "text/html\r\n\r\n<h1>404 Not Found</h1>";
            send(client_sock, error_message, strlen(error_message), 0);
            total_sent_bytes += strlen(error_message);
        }
    }

    closesocket(client_sock);
    return 0;
}

// Function to create the server and listen on a specified port
void start_server(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        exit(EXIT_FAILURE);
    }

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 5) == SOCKET_ERROR) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        SOCKET client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            perror("Accept failed");
            continue;
        }

        CreateThread(NULL, 0, handle_client, (LPVOID)client_sock, 0, NULL);
    }

    closesocket(server_sock);
    WSACleanup();
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    // Parse command-line options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        }
    }

    // Initialize the stats lock
    stats_lock = CreateMutex(NULL, FALSE, NULL);
    if (stats_lock == NULL) {
        fprintf(stderr, "CreateMutex failed\n");
        return 1;
    }

    // Start the server
    start_server(port);

    CloseHandle(stats_lock);
    return 0;
}
