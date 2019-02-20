#include <stdlib.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <string.h>

#include <ev.h>

#include "utils.h"
#include "log.h"

int make_socket_non_blocking(int sfd)
{
    int flags;
    if (-1 == (flags = fcntl(sfd, F_GETFL, 0)))
        flags = 0;
    return fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
}

static int do_mkdir(const char *path, mode_t mode)
{
    struct stat st;
    int status = 0;

    if (stat(path, &st) != 0)
    {
        /* Directory does not exist. EEXIST for race condition */
        if (mkdir(path, mode) != 0 && errno != EEXIST)
            status = -1;
    }
    else if (!S_ISDIR(st.st_mode))
    {
        errno = ENOTDIR;
        status = -1;
    }

    return (status);
}

/**
 ** mkpath - ensure all directories in path exist
 ** Algorithm takes the pessimistic view and works top-down to ensure
 ** each directory in path exists, rather than optimistically creating
 ** the last element and working backwards.
 */
int mkpath(const char *path, mode_t mode)
{
    char *pp;
    char *sp;
    int status;
    char *copypath = strdup(path);

    status = 0;
    pp = copypath;
    while (status == 0 && (sp = strchr(pp, '/')) != 0) //'/' - path from root
    {
        if (sp != pp)
        {
            /* Neither root nor double slash in path */
            *sp = '\0';
            status = do_mkdir(copypath, mode);
            *sp = '/';
        }
        pp = sp + 1;
    }
    if (status == 0)
        status = do_mkdir(path, mode);
    free(copypath);
    return (status);
}

int path_open(char *path, char *file_name, mode_t mode)
{
    int res;
    char tmpstr[PATH_MAX];
    snprintf(tmpstr, PATH_MAX, "%s/%s", path, file_name);
    res = open(tmpstr, O_WRONLY | O_CREAT | O_TRUNC, mode);

    if (res < 0)
    {
        res = mkpath(path, 0775);
        if (res >= 0)
        {
            res = open(tmpstr, O_WRONLY | O_CREAT, mode);
        }
        return res;
    }
    return res;
}

#define XFREE(x)       \
    {                  \
        if (x != NULL) \
            free(x);   \
    }

void UriFree(Uri_t *u)
{
    if (u != NULL)
    {
        XFREE(u->_line);
        XFREE(u->Path);
        XFREE(u->Uri);
        if (u->_servinfo)
        {
            freeaddrinfo(u->_servinfo);
        }
        free(u);
    }
}

Uri_t *UriParce(char *uri)
{
    if (!uri)
        return NULL;
    char *token = NULL, *token1;
    Uri_t *u = calloc(1, sizeof(Uri_t));
    char *_line = strdup(uri);
    char *lp = _line;

    u->_line = _line;
    // 1. Scheme
    token = strchr(lp, ':');
    if (token)
    {
        *token = '\0';
        u->Scheme = lp;
        lp = token + 1;
    }
    else
    {
        goto err;
    }
    // 2.  "//" && authority
    if (lp[0] == '/' && lp[1] == '/')
    {
        lp += 2;
        token1 = strchr(lp, '/');
        token = strchr(lp, '@');

        if (token && token1 > token)
        { // HAS LOGIN||LOGIN:PASS;
            *token = '\0';
            //            token1 = strchr(lp, ':');
            //            if (token1) { // Login:pass
            //                *token1 = '\0';
            //                u->Pass = token1 + 1;
            //            }
            u->UserInfo = lp;
            lp = token + 1;
        }
    }
    else
    {
        goto err;
    }
    // 3. host && path
    u->Host = lp;
    token = strchr(lp, ':'); // port
    token1 = strchr(lp, '/');
    if (token && token < token1)
    {
        *token = '\0';
        u->Port = token + 1;
        u->PortInt = atoi(token + 1);
        lp = token + 1;
    }
    token = strchr(lp, '/');
    if (token)
    {
        token1 = strchr(token, '?');
        if (token1)
        {
            *token1 = '\0';
            u->Query = token1 + 1;
        }
        u->Path = strdup(token);
        *token = '\0';
    }
    int UriLen = strlen(uri) + 10;
    u->Uri = calloc(1, UriLen);

    if (!strcmp(u->Scheme, "http") && !u->PortInt)
    {
        u->PortInt = 80;
    }

    snprintf(u->Uri, UriLen, "%s://%s:%i%s%s%s",
             u->Scheme, u->Host, u->PortInt, u->Path ? u->Path : "/", u->Query ? "?" : "", u->Query ? u->Query : "");

    //    free(_line);
    return u;
err:
    dbgp("Scheme error at %s", uri);
    free(_line);
    XFREE(u->Path);
    XFREE(u->Uri);
    free(u);
    return NULL;
}

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

char *base64_encode(uint8_t *data,
                    size_t input_length,
                    size_t *output_length)
{
    int i, j;
    //    uint32_t octet_a, octet_b, octet_c, triple;

    *output_length = 4 * ((input_length + 2) / 3);

    char *encoded_data = malloc(*output_length + 1);
    if (encoded_data == NULL)
        return NULL;

    for (i = 0, j = 0; i < input_length;)
    {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }
    for (i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';
    encoded_data[*output_length] = '\0';
    return encoded_data;
}
// unblocked read for maximum of in_buff_size bytes
int recvnb(int fd, char *in_buff, int in_buff_size, int *ret)
{
    int in_size = 0;
    //    int close_session = 0;
    int count;
    while (1)
    {
        count = recv(fd, in_buff + in_size, in_buff_size - in_size, 0);
        if (count == -1)
        {
            // EAGAIN means we have read all data, done to main loop.
            if (errno == EAGAIN)
            {
                count = 1;
            }
            else
            {
                dbgpl(4, "read error %i/%s fd %i", errno,strerror(errno), fd);
            }
            break;
        }
        else if (count == 0)
        {
            // Simply EOF
            dbgpl(3, "EOF fd %i", fd);
            break;
        }
        // done on buffer readed
        in_size += count;
        if (in_size == in_buff_size)
        {
            break;
        }
    }
    if (ret)
        *ret = count;
    return count >= 0 ? in_size : count;
}

int UriAddrInfo(Uri_t *uri)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol */
    int res = getaddrinfo(uri->Host, uri->Port, &hints, &uri->_servinfo);
    if (res != 0)
    {
        dbgp("getaddrinfo: %s\n", gai_strerror(res));
        return -1;
    }
    return 0;
}

int UriConnect(Uri_t *uri)
{
    struct addrinfo *rp;
    int sfd;

    if (!uri->_servinfo)
    {
        if (UriAddrInfo(uri) != 0)
        {
            return -1;
        }
    }

    for (rp = uri->_servinfo; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                     rp->ai_protocol);
        if (sfd == -1)
            continue;

        make_socket_non_blocking(sfd);

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; /* Success */
        else if (errno == EINPROGRESS)
            break;

        close(sfd);
    }

    if (rp == NULL)
    {
        dbgpl(3, "Could not connect %s %i %s", uri->Host, uri->PortInt, strerror(errno));
        return -1;
    }
    return sfd;
}