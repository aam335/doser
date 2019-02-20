#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <errno.h>
#include <fcntl.h>

#include <string.h>
#include <ev.h>

#include "http.h"
#include "log.h"

#define BUF_SIZE 4096

void core_HttpGetFileStop(EV_P_ HttpGetFile_t *c, int res)
{
    if (c->w.active)
    {
        c->lastErrno = errno;
        ev_io_stop(EV_A_ & c->w);
        close(c->Fd);
        if (c->FileFd > 0)
            close(c->FileFd);
        if (c->cb)
            (*c->cb)(res, c, NULL, res);
        c->FileFd = 0;
        c->Fd = 0;
    }
}

void core_HttpClientGetFileCb(EV_P_ ev_io *w, int revents)
{
    HttpGetFile_t *c = w->data;
    int res;
    char buf[BUF_SIZE];
    //    dbgp("CB!!!! %i", revents);
    Uri_t *u = c->u;

    if (EV_ERROR & revents)
    {
        dbgp("Error Connect to");
        res = -1;
        goto check_err;
    }
    else if (EV_WRITE & revents)
    {
        int len;

        len = snprintf(buf, BUF_SIZE,
                       "GET %s%s%s HTTP/1.1\r\n"
                       "HOST: %s:%i\r\n"
                       "Connection: close\r\n",
                       u->Path, u->Query ? "?" : "",
                       u->Query ? u->Query : "",
                       u->Host, u->PortInt);

        if (u->UserInfo)
        {
            size_t in_len, out_len;
            in_len = strlen(u->UserInfo);
            char *tmp = base64_encode((uint8_t *)u->UserInfo, in_len, &out_len);
            len += snprintf(buf + len, BUF_SIZE - len, "Authorization: Basic %s\r\n",
                            tmp);
            free(tmp);
        }
        len += snprintf(buf + len, BUF_SIZE - len, "\r\n");
        res = send(w->fd, buf, len, 0);
        if (res == -1 && errno == EAGAIN)
        {
            res = 1;
        }
        if (res > 0)
        {
            ev_io_stop(EV_A_ w);
            ev_io_set(w, w->fd, EV_READ);
            ev_io_start(EV_A_ w);
        }
        else
        {
            dbgp("Error while send: %s (userinfo:%s) -> (%i)%s", c->u->Uri, c->u->UserInfo, errno, strerror(errno));
        }
    }
    else if (EV_READ & revents)
    {
        // simplest http reply parcer
        int bytes_read;
        bytes_read = recvnb(w->fd, buf, BUF_SIZE, &res);
        if (res < 0)
            goto check_err;
        //dbgp("%i", bytes_read);
        if (!c->HeaderReceived)
        {
            if (strncasecmp(buf, "HTTP/1.1", 8) == 0 && bytes_read > 12)
            {
                c->httpReplyCode = atoi(buf + 9);
                if (c->httpReplyCode != 200)
                {
                    dbgpl(3, "Error reply %s (userinfo:%s) -> %s", c->u->Uri, c->u->UserInfo, buf);
                    res = -1;
                    goto check_err;
                }
            }
            char *eoh = memmem(buf, bytes_read, "\r\n\r\n", 4);
            if (!eoh)
            {
                res = -1;
                dbgp("wrong header from server");
                goto check_err;
            }
            *eoh = '\0';
            eoh += 4;

            char *sp = strcasestr(buf, "Content-Length:");
            if (sp)
            {
                c->ContentLenght = atoi(sp + 15);
                //                dbgp("contlen=%i", c->ContentLenght);
            }
            else
            {
                c->ContentLenght = 1000000; // nolen
            }
            c->HeaderReceived = 1;
            bytes_read -= (eoh - buf);
            c->ReadedBytes = bytes_read;
            if (c->FileFd > 0 && bytes_read > 0)
            {
                res = write(c->FileFd, eoh, bytes_read);
            }
            c->ContentLenght -= bytes_read;
        }
        else
        {
            c->ReadedBytes += bytes_read;
            if (c->FileFd > 0)
            {
                res = write(c->FileFd, buf, bytes_read);
            }
            if (res >= 0)
            {
                c->ContentLenght -= res;
                if (c->ContentLenght <= 0)
                {
                    res = 0;
                }
            }
            else
            {
                dbgp("Error: %s %i", strerror(errno), errno);
            }
        }
    }
check_err:
    if (res <= 0)
    {
        core_HttpGetFileStop(EV_A_ c, res);
        if (c->FileName[0] != '\0' && res < 0)
        {
            unlink(c->FileName);
        }
    }
}

int HttpGetFile(EV_P_ HttpGetFile_t *c, Uri_t *u, char *path, char *filename,
                HttpResultCB_t cb, void *cb_data)
{
    if (!c || !u)
        return 0;

    // проверяю на активность коллбэка
    if (c->w.active)
    {
        err_exit("Hardcode error: worker still active");
        // dbgp("Timeout on fd %i %s", c->w.fd, c->u->Uri);
        // HttpGetFileStop(EV_A_ c);
    }

    memset(c, 0, sizeof(HttpGetFile_t));
    c->Fd = UriConnect(u);
    c->u = u;
    if (c->Fd < 0)
        goto err;

    if (path && filename)
    {
        c->FileFd = path_open(path, filename, 0644);
        if (c->FileFd < 0)
            goto err;
        snprintf(c->FileName, PATH_MAX, "%s/%s", path, filename);
    }
    else
    {
        c->FileFd = -1;
    }

    c->w.data = c;
    c->data = cb_data;
    c->cb = cb;
    ev_io_init(&c->w, &core_HttpClientGetFileCb, c->Fd, EV_WRITE);
    ev_io_start(EV_A_ & c->w);

    return 0;
err:
    HttpGetFileStop(EV_A_ c, errno);
    return -1;
}

void HttpGetFileStop(EV_P_ HttpGetFile_t *c, int err)
{
    //    dbgp("stop %i", res);
    errno = err;
    core_HttpGetFileStop(EV_A_ c, -1);
}
