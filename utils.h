#ifndef UTILS_H
#define UTILS_H
#ifndef SOL_TCP
#define SOL_TCP 6 // socket options TCP level
#endif
#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18 // how long for loss retry before timeout [ms]
#endif

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

typedef struct Uri_t
{
    char *_line;
    char *Scheme;
    char *UserInfo;
    char *Host;
    char *Port;
    int PortInt;
    char *Path;
    char *Query;
    char *Uri;
    struct addrinfo *_servinfo;
} Uri_t;

int mkpath(const char *path, mode_t mode);

void UriFree(Uri_t *uri);
Uri_t *UriParce(char *uri);
int UriConnect(Uri_t *uri);
int UriAddrInfo(Uri_t *uri);

char *base64_encode(uint8_t *data, size_t input_length, size_t *output_length);
int make_socket_non_blocking(int sfd);
int recvnb(int fd, char *in_buff, int in_buff_size, int *ret);

int path_open(char *path, char *file_name, mode_t mode);

#endif
