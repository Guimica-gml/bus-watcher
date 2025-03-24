#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <winsock2.h>
#elif __linux__
#    include <unistd.h>
#    include <netdb.h>
#    include <sys/socket.h>
#    include <netinet/in.h>
#    define SD_READ SHUT_RD
#    define SD_WRITE SHUT_WR
#    define SD_BOTH SHUT_RDWR
#    define closesocket close
#    define h_addr h_addr_list[0]
typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
#endif

#include "errno.h"

#include "./utils.h"
#include "./json.h"
#define ARENA_IMPLEMENTATION
#include "./arena.h"

#define URL "api.olhovivo.sptrans.com.br"

typedef struct {
    String header;
    String payload;
} Answer;

Answer make_request(SOCKET sock, Arena *arena, String *request) {
    Answer answer = {0};
    int bytes_sent = send(sock, request->items, request->count, 0);
    if ((size_t)bytes_sent != request->count) {
        fprintf(stderr, "Error: could not send request\n");
        exit(1);
    }

    // TODO(nic): check if recv fails
    while (!(answer.header.count >= 4
             && memcmp(&answer.header.items[answer.header.count - 4], "\r\n\r\n", 4) == 0))
    {
        char ch;
        recv(sock, &ch, sizeof(char), 0);
        str_append_char(arena, &answer.header, ch);
    }

    size_t payload_count = 0;
    String_View header = { answer.header.items, answer.header.count };
    while (header.size > 0) {
        if (sv_starts_with(header, "Content-Length: ")) {
            sv_chop_until(&header, ':');
            String_View payload_count_sv = sv_trim(sv_chop_until(&header, '\n'));
            payload_count = sv_to_int64(payload_count_sv);
        }
        sv_chop_until(&header, '\n');
    }
    assert(payload_count > 0);

    char *payload = malloc(payload_count);
    int bytes_read = recv(sock, payload, payload_count, MSG_WAITALL);
    assert(payload_count == (size_t)bytes_read);
    arena_da_append_many(arena, &answer.payload, payload, payload_count);
    free(payload);
    return answer;
}

#define defer_return(value) \
    do {                    \
        result = (value);   \
        goto defer;         \
    } while(0)

typedef int Errno;

Errno read_file(Arena *arena, const char *filepath, String *str) {
    int result = 0;
    char *content = NULL;

    FILE *file = fopen(filepath, "r");
    if (file == NULL) defer_return(errno);
    if (fseek(file, 0, SEEK_END) != 0) defer_return(errno);
    long file_size = ftell(file);
    if (file_size < 0) defer_return(errno);
    rewind(file);

    content = malloc(file_size);
    assert(content != NULL && "Error: not enough ram");

    long read_size = fread(content, sizeof(char), file_size, file);
    if (read_size != file_size) defer_return(errno);
    arena_da_append_many(arena, str, content, file_size);

defer:
    if (content) free(content);
    if (file) fclose(file);
    return result;
}

bool authenticate(SOCKET sock, Arena *arena, String *cookie) {
    String token = {0};
    Errno err = read_file(arena, "./src/TOKEN", &token);
    if (err) {
        fprintf(stderr, "Could not read token from file: %s\n", strerror(errno));
        return false;
    }

    String request = {0};
    str_append_fmt(arena, &request, "POST /v2.1/Login/Autenticar?token=%.*s HTTP/1.1\r\n", (int)token.count, token.items);
    str_append_fmt(arena, &request, "Host: %s\r\n", URL);
    str_append_fmt(arena, &request, "Connection: keep-alive\r\n");
    str_append_fmt(arena, &request, "Content-Type: application/json; charset=utf-8\r\n");
    str_append_fmt(arena, &request, "Content-Length: 0\r\n");
    str_append_fmt(arena, &request, "\r\n");

    Answer answer = make_request(sock, arena, &request);
    String_View sv = sv_from_parts(answer.header.items, answer.header.count);

    while (sv.size > 0) {
        if (sv_starts_with(sv, "Set-Cookie: ")) {
            sv_chop_until(&sv, ':');
            String_View cookie_sv = sv_trim(sv_chop_until(&sv, ';'));
            str_append_sv(arena, cookie, cookie_sv);
            return true;
        }
        sv_chop_until(&sv, '\n');
    }

    return false;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa_data = {0};
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        fprintf(stderr, "Error: WSA startup failed with error code: %d\n", startup_result);
        exit(1);
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        fprintf(stderr, "Error: could not create socket\n");
        exit(1);
    }

    struct hostent *host = gethostbyname(URL);
    if (host == NULL) {
        fprintf(stderr, "Error: Could not get host\n");
        exit(1);
    }

    SOCKADDR_IN sock_addr = {0};
    sock_addr.sin_port = htons(80);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = *((unsigned long *) host->h_addr);

    if (connect(sock, (SOCKADDR*) &sock_addr, sizeof(sock_addr)) != 0) {
        fprintf(stderr, "Error: Could not connect to the server\n");
        exit(1);
    }

    Arena arena = {0};
    String cookie = {0};

    bool could_auth = authenticate(sock, &arena, &cookie);
    assert(could_auth);

    {
        String request = {0};
        str_append_fmt(&arena, &request, "GET /v2.1/Linha/Buscar?termosBusca=2552 HTTP/1.1\r\n");
        str_append_fmt(&arena, &request, "Host: %s\r\n", URL);
        str_append_fmt(&arena, &request, "Connection: keep-alive\r\n");
        str_append_fmt(&arena, &request, "Cookie: %.*s\r\n", (int) cookie.count, cookie.items);
        str_append_fmt(&arena, &request, "Content-Type: application/json; charset=utf-8\r\n");
        str_append_fmt(&arena, &request, "Content-Length: 0\r\n");
        str_append_fmt(&arena, &request, "\r\n");

        Answer answer = make_request(sock, &arena, &request);
        Json_Object json = {0};
        Json_Result result = json_parse(
            &arena, &json,
            answer.payload.items, answer.payload.count
        );
        assert(!result.failed);
        json_print_obj(&json);
        printf("\n");
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    arena_free(&arena);
    return 0;
}
