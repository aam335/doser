#ifndef HTTP_H
#define HTTP_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <ev.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <linux/limits.h> // PATH_MAX

#include "utils.h"

    typedef enum HttpMethod_t
    {
        HTTP_GET,
        HTTP_PUT,
        HTTP_POST
    } HttpMethod_t;

    // int <0 = err, >0 = ReplySize, 0=size unknown
    // void * - cb data
    // char * = read buffer
    // ssize_t = bytes readed;
    // return = >0 ок <0 force close connection;

    typedef int (*HttpResultCB_t)(int, void *, char *, ssize_t);

    typedef struct HttpGetFile_t
    {
        ssize_t ContentLenght;
        ssize_t ReadedBytes;
        HttpResultCB_t cb;
        void *data;
        int Fd;
        uint8_t HeaderReceived;
        struct sockaddr_in Addr;
        EV_P;
        ev_io w;
        int FileFd;
        Uri_t *u;
        //        int Offset;
        //        int LastRet;
        char FileName[PATH_MAX];
        int httpReplyCode;
        int lastErrno;
    } HttpGetFile_t;

    int HttpGetFile(EV_P_ HttpGetFile_t *c, Uri_t *u, char *path, char *filename,
                    HttpResultCB_t cb, void *cb_data);
    void HttpGetFileStop(EV_P_ HttpGetFile_t *c, int error);
#endif