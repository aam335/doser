#include <stdio.h>
#include <stdlib.h>

#include <ev.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "log.h"
#include "http.h"

typedef enum
{
    FLAG_SAVE_RESULTS = 1 << 0,
    FLAG_TIME_LIMIT = 1 << 1,
    FLAG_CONN_LIMIT = 1 << 2,
    DEF_CONN_LIMIT = 1000,
    DEF_TIME_LIMIT = 5000
} flags_t;

uint32_t flags = 0;
char *to_path = NULL;
long time_limit = 10;
long timeout = 5;
long conn_limit = DEF_CONN_LIMIT;
long sim_conn = DEF_CONN_LIMIT;

Uri_t *uri = NULL;

void parceFlags(char *s);

EV_P;
int req_count = 0;
int req_count_active = 0;
int req_done_count = 0;
int int_tcp_err = 0;
int ext_tcp_err = 0;
ssize_t req_readed_bytes = 0;

typedef struct
{
    uint8_t on_work;
    HttpGetFile_t file;
    Uri_t uri;
    int my_no;
    ev_tstamp startAt;
    int64_t startMs;
} hconn_t;

typedef struct
{
    ev_tstamp min, max, total;
    int count;
} ReplyStat_t;

ReplyStat_t replys[1000];
#define ERR_MAP_SIZE 100
int errMap[ERR_MAP_SIZE];
int errMapCount = 0;
int mapErr(int err)
{
    int i;
    for (i = 0; i < errMapCount; i++)
    {
        if (errMap[i] == err)
            return i;
    }
    if (errMapCount < ERR_MAP_SIZE)
    {
        errMap[errMapCount++] = err;
        return errMapCount - 1;
    }
    return 0; // more than ERR_MAP_SIZE different errors
}

int done_cb(int res, void *v, char *v1, ssize_t readed)
{
    HttpGetFile_t *c = v;
    hconn_t *hc = c->data;
    if (v1 != NULL || readed != 0)
    {
        ext_tcp_err++;
        // dbgp(">>>%li %i %i",readed,c->lastErrno,c->httpReplyCode);
    }

    dbgpl(2, "readed:%li", c->ReadedBytes);
    if (c->ReadedBytes > 0)
    {
        req_readed_bytes += c->ReadedBytes;
    }

    hc->on_work = 0;
    req_count_active--;
    req_done_count++;

    int rc = (c->httpReplyCode <= 0 || c->httpReplyCode >= 999) ? mapErr(c->lastErrno) : c->httpReplyCode;
    ev_tstamp diff = ev_now(EV_A) - hc->startAt; //0.000001* (utime() - hc->startMs);

    if (replys[rc].total == 0)
    {
        replys[rc].min = diff;
        replys[rc].max = diff;
    }

    if (replys[rc].min > diff)
        replys[rc].min = diff;
    if (replys[rc].max < diff)
        replys[rc].max = diff;
    replys[rc].total += diff;
    replys[rc].count++;

    dbgpl(2, "%i res:%i(%i) %i", hc->my_no, res, rc, replys[rc].count);
    return 0;
}

hconn_t *conns = NULL;
void worker(void)
{
    if (conns == NULL)
    {
        return;
    }
    char filename[20];
    for (int i = 0; i < sim_conn; i++)
    {
        if (conns[i].on_work)
        {
            if (conns[i].startAt + timeout - ev_now(EV_A) < 0)
            {
                HttpGetFileStop(EV_A_ & (conns[i].file), ETIMEDOUT);
            }
        }
        else if ((flags & FLAG_CONN_LIMIT) && req_count < conn_limit)
        {
            snprintf(filename, 20, "%08i.req", req_count++);
            conns[i].startAt = ev_now(EV_A);
            if (HttpGetFile(EV_A, &(conns[i].file), uri, to_path, filename, done_cb, &conns[i]) != 0)
            {
                int_tcp_err++; // internal (such as "Too many open files") error
                continue;
            }
            conns[i].on_work = 1;
            conns[i].my_no = req_count;
            req_count_active++;
        }
    }
}

ev_timer timeout_watcher;
ev_tstamp start_time;

static void pereodic_worker(EV_P_ ev_timer *w, int revents)
{
    if ((flags & FLAG_CONN_LIMIT) && req_count <= conn_limit)
    {
        worker(); // arm needed connections
    }
    if (req_count_active == 0)
    { // nojobs
        ev_break(EV_A_ EVBREAK_ONE);
        dbgpl(1, "all jobs done")
    }
    if (flags & FLAG_TIME_LIMIT)
    {
        ev_tstamp after = start_time + (ev_tstamp)time_limit - ev_now(EV_A);
        if (after < 0)
        {
            ev_break(EV_A_ EVBREAK_ONE);
            dbgpl(1, "Time limit")
        }
    }
}

static void
sigint_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
    signal(w->signum, SIG_IGN);
    ev_break(loop, EVBREAK_ALL);
}

int main(int argc, char **argv)
{
    conp("# http benchmarking tool");
    if (argc < 2)
    {
        conp("Usage: %s uri [-csimcount] [-llimitcount] [-ttimelimit] [-wconntimeout]", argv[0]);
        conp("-v[v...] - verbose");
        conp("-cNumber - number of parallel queries");
        conp("-lNumber - limit queries count (default 1000)");
        conp("-tNumber - limit total test time in seconds");
        conp("-wNumber - connection timeout (default 5s)");
        return 0;
    }
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            parceFlags(argv[i]);
        }
        else
        {
            uri = UriParce(argv[i]);
            if (!uri)
            {
                err_exit("%s uri parce error", argv[1]);
            }
        }
    }
    if (!uri)
    {
        err_exit("error no uri set");
    }
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    rlim_t real_rlim_needs = (flags & FLAG_SAVE_RESULTS) == 0 ? sim_conn : sim_conn * 2;
    if (rl.rlim_max < real_rlim_needs)
    {
        err_exit("Hard limit for RLIMIT_NOFILE is %lu (u needs is %lu)", rl.rlim_max, real_rlim_needs);
    }

    if (rl.rlim_cur < real_rlim_needs)
    {
        rl.rlim_cur = rl.rlim_max;
        dbgpl(1, "New limits: %lu/%lu", rl.rlim_cur, rl.rlim_max);
        if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
        {
            err_exit("Error:%s", strerror(errno));
        }
    }

    if (!(flags & FLAG_TIME_LIMIT))
    {
        flags |= FLAG_CONN_LIMIT;
    }

    EV_A = EV_DEFAULT;
    conns = calloc(sizeof(hconn_t), sim_conn);
    ev_timer_init(&timeout_watcher, pereodic_worker, 0.1, 0.05);
    ev_timer_start(loop, &timeout_watcher);

    start_time = ev_now(EV_A);
    memset(replys, 0, sizeof(replys));

    ev_signal signal_watcher;
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);

    ev_run(loop, 0);

    start_time = ev_now(EV_A) - start_time;
    conp("# Done in %f sec, %f ips, %f bps (body)", start_time, req_done_count / start_time, req_readed_bytes / start_time);
    if (req_count_active > 0)
    {
        conp("# queries armed, but not replied:%i", req_count_active);
    }
    if (int_tcp_err > 0)
    {
        conp("# internal (connect) errors: %i", int_tcp_err);
    }
    if (ext_tcp_err > 0)
    {
        conp("# external (http server tcp reject/timeout) errors: %i", ext_tcp_err);
    }
    conp("code\tmin\tmax\tavg\ttotal queries");
    for (int i = ERR_MAP_SIZE; i < 999; i++)
    {
        if (replys[i].count > 0)
        {
            conp("%i\t%.4f\t%.4f\t%.4f\t%i", i, replys[i].min, replys[i].max, replys[i].total / replys[i].count, replys[i].count);
        }
    }
    for (int i = 0; i < ERR_MAP_SIZE; i++)
    {
        if (replys[i].count > 0)
        {
            conp(">>%s\t%.4f\t%.4f\t%.4f\t%i", strerror(errMap[i]), replys[i].min, replys[i].max, replys[i].total / replys[i].count, replys[i].count);
        }
    }
    return 0;
}

void parceFlags(char *s)
{
    for (int i = 0; s[i]; i++)
    {
        switch (s[i])
        {
        case '-':
            continue;
        case 'v':
            log_level++;
            continue;
        case 's':
            flags |= FLAG_SAVE_RESULTS;
            to_path = strdup(s + i + 1);
            break;
        case 't':
            flags |= FLAG_TIME_LIMIT;
            time_limit = strtol(s + i + 1, NULL, 10);
            break;
        case 'l':
            flags |= FLAG_CONN_LIMIT;
            conn_limit = atoi(s + i + 1);
            break;
        case 'c':
            sim_conn = atoi(s + i + 1);
            break;
        case 'w':
            timeout = strtol(s + i + 1, NULL, 10);
            break;
        }
        break;
    }
}