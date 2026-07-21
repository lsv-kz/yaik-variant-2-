#include "main.h"

using namespace std;

static int flog = STDOUT_FILENO, flog_err = STDERR_FILENO;
static mutex mtxLog;
static unsigned int num_log_records = 0, num_logerr_records = 0;

static time_t create_time;
//======================================================================
void create_logfile(const string& log_dir)
{
    char buf[256];
    struct tm tm1;
    time_t t1;

    time(&t1);
    create_time = t1;
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm1);

    BytesArray file_name;
    file_name.ncpy(log_dir.c_str(), log_dir.size());
    file_name.ncat("/", 1);
    file_name.strcat(buf);
    file_name.ncat("-", 1);
    file_name.ncat(conf->ServerSoftware.c_str(), conf->ServerSoftware.size());
    file_name.strcat(".log");

    flog = open(file_name.ptr(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog == -1)
    {
        cerr << " Error create log: " << file_name.ptr() << "\n";
        exit(1);
    }
}
//======================================================================
void create_error_logfile(const string& log_dir)
{
    char buf[256];
    struct tm tm1;
    time_t t1;

    time(&t1);
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm1);

    BytesArray file_name;
    file_name.ncpy(log_dir.c_str(), log_dir.size());
    file_name.strcat("/error_");
    file_name.strcat(buf);
    file_name.ncat("_", 1);
    file_name.ncat(conf->ServerSoftware.c_str(), conf->ServerSoftware.size());
    file_name.strcat(".log");

    flog_err = open(file_name.ptr(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog_err == -1)
    {
        cerr << "  Error create log_err: " << file_name.ptr() << "\n";
        exit(1);
    }

    dup2(flog_err, STDERR_FILENO);
}
//======================================================================
void close_logs()
{
    close(flog);
    close(flog_err);
}
//======================================================================
void print_err(const char *format, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    BytesArray str;
    str.reserve(330);
    if (str.error())
    {
        fprintf(stderr, "<%s:%d> Error BytesArray.reserve()\n", __func__, __LINE__);
        return;
    }

    str.ncpy("[", 1);
    str.logtimecat();
    str.ncat("] ", 2);
    str.strcat(buf);
    if (n >= (int)sizeof(buf))
        str.strcat("--- overflow ---\n");

mtxLog.lock();
    write(flog_err, str.ptr(), str.size());
    num_logerr_records++;
    if (num_logerr_records > 500000)
    {
        time_t t = time(NULL);
        if ((t - create_time) <  300)
        {
            close(flog_err);
            flog_err = STDERR_FILENO;
            exit(1);
        }
        else
            create_time = time(NULL);
        close(flog_err);
        create_error_logfile(conf->LogDir);
        num_logerr_records = 0;
    }
mtxLog.unlock();
}
//======================================================================
void print_err(Connect *con, const char *format, ...)
{
    if (!con)
        return;
    char buf[768];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    BytesArray str;
    str.reserve(1024);
    if (str.error())
    {
        fprintf(stderr, "<%s:%d> Error BytesArray.reserve()\n", __func__, __LINE__);
        return;
    }

    str.ncpy("[", 1);
    str.logtimecat();
    str.ncat("] - [", 5);
    str.cat_int(con->numConn);
    str.ncat("] ", 2);
    str.strcat(buf);
    if (n >= (int)sizeof(buf))
        str.strcat("--- overflow ---\n");

mtxLog.lock();
    write(flog_err, str.ptr(), str.size());
    num_logerr_records++;
    if (num_logerr_records > 500000)
    {
        time_t t = time(NULL);
        if ((t - create_time) <  300)
        {
            close(flog_err);
            flog_err = STDERR_FILENO;
            exit(1);
        }
        else
            create_time = time(NULL);
        close(flog_err);
        create_error_logfile(conf->LogDir);
        num_logerr_records = 0;
    }
mtxLog.unlock();
}
//======================================================================
void print_err(Stream *resp, const char *format, ...)
{
    if (!resp)
    {
        fprintf(stderr, "<%s:%d> !!! resp=NULL\n", __func__, __LINE__);
        return;
    }

    char buf[768];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    BytesArray str;
    str.reserve(1024);
    if (str.error())
    {
        fprintf(stderr, "<%s:%d> Error BytesArray.reserve()\n", __func__, __LINE__);
        return;
    }

    str.ncpy("[", 1);
    str.logtimecat();
    str.ncat("] - [", 5);
    str.cat_int(resp->numConn);
    str.ncat("/", 1);
    str.cat_int(resp->numReq);
    str.ncat("] ", 2);
    str.strcat(buf);
    if (n >= (int)sizeof(buf))
        str.strcat("--- overflow ---\n");

mtxLog.lock();
    write(flog_err, str.ptr(), str.size());
    num_logerr_records++;
    if (num_logerr_records > 500000)
    {
        time_t t = time(NULL);
        if ((t - create_time) <  300)
        {
            close(flog_err);
            flog_err = STDERR_FILENO;
            exit(1);
        }
        else
            create_time = time(NULL);
        close(flog_err);
        create_error_logfile(conf->LogDir);
        num_logerr_records = 0;
    }
mtxLog.unlock();
}
//======================================================================
void print_log(Connect *c, Stream *resp)
{
    if (!c || !resp)
        return;
    BytesArray str;
    str.reserve(1024);
    if (str.error())
    {
        fprintf(stderr, "<%s:%d> Error BytesArray.reserve()\n", __func__, __LINE__);
        return;
    }

    str.cpy_int(resp->numConn);
    str.ncat("/", 1);
    str.cat_int(resp->numReq);
    str.strcat(" - ");
    str.strcat(c->remoteAddr);
    str.ncat(" > ", 3);
    str.ncat(c->serv->port.c_str(), c->serv->port.size());
    str.ncat(" - [", 4);
    str.logtimecat();
    str.ncat("] \"", 3);
    if (resp->httpMethod != M_NULL)
    {
        str.strcat(get_str_method(resp->httpMethod));
        str.ncat(" ", 1);
    }

    if (resp->clean_decode_path)
    {
        str.strcat(resp->clean_decode_path);
        if (resp->decode_query_string.size())
        {
            str.ncat("?", 1);
            str.ncat(resp->decode_query_string.c_str(), resp->decode_query_string.size());
        }
    }

    str.strcat(" HTTP/2\" ");
    if (resp->resp_status)
    {
        str.cat_int(resp->resp_status);
        str.ncat(" ", 1);
        str.cat_int(resp->send_bytes);
    }

    str.ncat(" \"", 2);
    if (resp->referer.size())
        str.ncat(resp->referer.c_str(), resp->referer.size());
    else
        str.ncat("-", 1);
    str.ncat("\" \"", 3);
    if (resp->user_agent.size())
        str.ncat(resp->user_agent.c_str(), resp->user_agent.size());
    else
        str.ncat("-", 1);
    str.ncat("\" - id=", 7);
    str.cat_int(resp->id);
    str.ncat(" \n", 2);

mtxLog.lock();
    write(flog, str.ptr(), str.size());
    num_log_records++;
    if (num_log_records > 500000)
    {
        close(flog);
        create_logfile(conf->LogDir);
        num_log_records = 0;
    }
mtxLog.unlock();
}
//======================================================================
void print_log(Connect *c)
{
    if (c == NULL)
        return;
    if (c->Protocol == P_HTTP2)
        return;
    BytesArray str;
    str.reserve(1024);
    if (str.error())
    {
        fprintf(stderr, "<%s:%d> Error BytesArray.reserve()\n", __func__, __LINE__);
        return;
    }

    str.cpy_int(c->numConn);
    str.ncat("/", 1);
    str.cat_int(c->numReq);
    str.strcat(" - ");
    str.strcat(c->remoteAddr);
    str.ncat(" > ", 3);
    str.ncat(c->serv->port.c_str(), c->serv->port.size());
    str.ncat(" - [", 4);
    str.logtimecat();
    str.ncat("] \"", 3);
    str.strcat(get_str_method(c->h1->resp.httpMethod));
    str.ncat(" ", 1);
    str.strcat(c->h1->resp.clean_decode_path);

    if (c->h1->resp.decode_query_string.size())
    {
        str.ncat("?", 1);
        str.ncat(c->h1->resp.decode_query_string.c_str(), c->h1->resp.decode_query_string.size());
    }

    str.strcat(" HTTP/1.1\" ");
    str.cat_int(c->h1->resp.resp_status);
    str.ncat(" ", 1);
    str.cat_int(c->h1->resp.send_bytes);
    str.ncat(" \"", 2);
    if (c->h1->resp.referer.size())
        str.ncat(c->h1->resp.referer.c_str(), c->h1->resp.referer.size());
    else
        str.ncat("-", 1);
    str.ncat("\" \"", 3);
    if (c->h1->resp.user_agent.size())
        str.ncat(c->h1->resp.user_agent.c_str(), c->h1->resp.user_agent.size());
    else
        str.ncat("-", 1);
    str.ncat("\"\n", 2);

mtxLog.lock();
    write(flog, str.ptr(), str.size());
    num_log_records++;
    if (num_log_records > 500000)
    {
        close(flog);
        create_logfile(conf->LogDir);
        num_log_records = 0;
    }
mtxLog.unlock();
}
//======================================================================
void create_logfiles(const string& log_dir)
{
    create_logfile(log_dir);
    create_error_logfile(log_dir);
}
