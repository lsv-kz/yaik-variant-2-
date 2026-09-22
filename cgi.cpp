#include "main.h"

using namespace std;
//======================================================================
void kill_chld(pid_t pid)
{
    if (pid > 0)
    {
        if (waitpid(pid, NULL, WNOHANG) == 0)
        {
            if (kill(pid, SIGKILL) == 0)
                waitpid(pid, NULL, 0);
            else
                print_err("<%s:%d> Error kill(): %s\n", __func__, __LINE__, strerror(errno));
        }
    }
}
//======================================================================
const char *get_script_name(const char *name)
{
    const char *p;
    if (!name)
        return "";

    if ((p = strchr(name + 1, '/')))
        return p;

    return "";
}
//======================================================================
const char *base_name(const char *path)
{
    const char *p;

    if (!path)
        return NULL;

    p = strrchr(path, '/');
    if (p)
        return p + 1;

    return path;
}
//======================================================================
int cgi_set_size_chunk(BytesArray *ba)
{
    const char *hex = "0123456789ABCDEF";
    int size = ba->size_remain() - 8;
    int i = 7;
    ba->set_byte('\n', i);
    --i;
    ba->set_byte('\r', i);
    --i;

    for ( ; i >= 0; --i)
    {
        ba->set_byte(hex[size % 16], i);
        size /= 16;
        if (size == 0)
            break;
    }

    if (size != 0)
        return -1;

    ba->inc_offset(i);
    ba->ncat("\r\n", 2);

    return 0;
}
//======================================================================
int EventHandlerClass::cgi_fork(Connect *c, Stream *resp, int* serv_cgi, int* cgi_serv)
{
    struct stat st;

    if (resp->cgi_type == CGI)
    {
        resp->cgi.path = conf->ScriptDir;
        resp->cgi.path += get_script_name(resp->clean_decode_path);
    }
    else if (resp->cgi_type == PHPCGI)
    {
        resp->cgi.path = resp->vhost->DocumentRoot;
        resp->cgi.path += resp->clean_decode_path;
    }

    if (stat(resp->cgi.path.c_str(), &st) == -1)
    {
        print_err(resp, "<%s:%d> script (%s) not found\n", __func__, __LINE__, resp->cgi.path.c_str());
        return -RS404;
    }
    //--------------------------- fork ---------------------------------
    pid_t pid = fork();
    if (pid < 0)
    {
        resp->cgi.pid = pid;
        print_err(resp, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (pid == 0)
    {
        //----------------------- child --------------------------------
        close(cgi_serv[0]);
        cgi_serv[0] = -1;

        int fd = open("/dev/null", O_RDONLY);
        if (fd > 0)
        {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        if (resp->httpMethod == M_POST)
        {
            close(serv_cgi[1]);
            if (serv_cgi[0] != STDIN_FILENO)
            {
                if (dup2(serv_cgi[0], STDIN_FILENO) < 0)
                {
                    print_err(resp, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                    exit(1);
                }
                close(serv_cgi[0]);
                serv_cgi[0] = -1;
            }
        }

        if (cgi_serv[1] != STDOUT_FILENO)
        {
            if (dup2(cgi_serv[1], STDOUT_FILENO) < 0)
            {
                print_err(resp, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                goto to_pipe;
            }
            close(cgi_serv[1]);
            cgi_serv[1] = -1;
        }

        if (resp->cgi_type == PHPCGI)
            setenv("REDIRECT_STATUS", "true", 1);
        setenv("SERVER_SOFTWARE", conf->ServerSoftware.c_str(), 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("REQUEST_METHOD", get_str_method(resp->httpMethod), 1);
        if (c->Protocol == P_HTTP2)
            setenv("SERVER_PROTOCOL", "HTTP/2.0", 1);
        else
            setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
        setenv("DOCUMENT_ROOT", resp->vhost->DocumentRoot.c_str(), 1);
        setenv("DOCUMENT_URI", resp->clean_decode_path, 1);
        setenv("REQUEST_URI", resp->path.c_str(), 1);
        setenv("REMOTE_ADDR", c->remoteAddr, 1);
        setenv("REMOTE_PORT", c->remotePort, 1);
        setenv("SCRIPT_NAME", resp->clean_decode_path, 1);
        setenv("SCRIPT_FILENAME", resp->cgi.path.c_str(), 1);

        if (resp->host.size())
            setenv("HTTP_HOST", resp->host.c_str(), 1);
        if (resp->referer.size())
            setenv("HTTP_REFERER", resp->referer.c_str(), 1);
        if (resp->user_agent.size())
            setenv("HTTP_USER_AGENT", resp->user_agent.c_str(), 1);

        if (resp->httpMethod == M_POST)
        {
            if (resp->sReqContentType.size())
                setenv("CONTENT_TYPE", resp->sReqContentType.c_str(), 1);

            if (resp->sReqContentLen.size())
                setenv("CONTENT_LENGTH", resp->sReqContentLen.c_str(), 1);
        }

        if (resp->query_string.size())
            setenv("QUERY_STRING", resp->query_string.c_str(), 1);

        if (resp->cgi_type == CGI)
        {
            if (chdir(conf->ScriptDir.c_str()))
            {
                fprintf(stderr, "<%s:%d> Error chdir(%s): %s\n", __func__, __LINE__, conf->ScriptDir.c_str(), strerror(errno));
                goto to_pipe;
            }

            execl(get_script_name(resp->clean_decode_path) + 1, base_name(resp->cgi.path.c_str()), NULL);
            print_err(resp, "<%s:%d> Error execl(%s, %s): %s\n", __func__, __LINE__,
                        get_script_name(resp->clean_decode_path) + 1, base_name(resp->clean_decode_path), strerror(errno));
        }
        else if (resp->cgi_type == PHPCGI)
        {
            execl(conf->PathPHP.c_str(), base_name(conf->PathPHP.c_str()), NULL);
            print_err(resp, "<%s:%d> Error execl(%s, %s): %s\n", __func__, __LINE__,
                        conf->PathPHP.c_str(), base_name(conf->PathPHP.c_str()), strerror(errno));
        }

    to_pipe:
        {
            char err_msg[] = "Status: 500 Internal Server Error\r\n"
                "Content-type: text/html; charset=UTF-8\r\n"
                "\r\n"
                "<!DOCTYPE html>\n"
                "<html>\n"
                "<head>\n"
                "<title>500 Internal Server Error</title>\n"
                "<meta http-equiv=\"content-type\" content=\"text/html\">\n"
                "</head>\n"
                "<body>\n"
                "<p> 500 Internal Server Error</p>\n"
                "</body>\n"
                "</html>";
            write(STDOUT_FILENO, err_msg, strlen(err_msg));
        }
        close(STDOUT_FILENO);
        exit(EXIT_FAILURE);
    }
    else
    {
        resp->cgi.pid = pid;
        resp->cgi.timer = 0;

        close(cgi_serv[1]);
        cgi_serv[1] = -1;

        int opt = 1;
        ioctl(cgi_serv[0], FIONBIO, &opt);

        if (resp->httpMethod == M_POST)
        {
            ioctl(serv_cgi[1], FIONBIO, &opt);
            if (serv_cgi[0] > 0)
            {
                close(serv_cgi[0]);
                serv_cgi[0] = -1;
            }

            if ((resp->post_content_len <= 0) && (resp->post_data.size() == 0))
            {
                resp->cgi_status = CGI_STDOUT;
                close(serv_cgi[1]);
                serv_cgi[1] = -1;
            }
            else
                resp->cgi_status = CGI_STDIN;
        }
        else
            resp->cgi_status = CGI_STDOUT;

        resp->cgi.from_script = cgi_serv[0];
        resp->cgi.to_script = serv_cgi[1];

        return 0;
    }
}
//======================================================================
int EventHandlerClass::cgi_create_proc(Connect *c, Stream *resp)
{
    int serv_cgi[2], cgi_serv[2];
    int n = pipe(cgi_serv);
    if (n == -1)
    {
        print_err(resp, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
        return -1;
    }

    if (resp->httpMethod == M_POST)
    {
        n = pipe(serv_cgi);
        if (n == -1)
        {
            print_err(resp, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
            close(cgi_serv[0]);
            cgi_serv[0] = -1;

            close(cgi_serv[1]);
            cgi_serv[1] = -1;
            return -1;
        }
    }
    else
    {
        serv_cgi[0] = -1;
        serv_cgi[1] = -1;
    }

    n = cgi_fork(c, resp, serv_cgi, cgi_serv);
    if (n < 0)
    {
        if (resp->httpMethod == M_POST)
        {
            close(serv_cgi[0]);
            serv_cgi[0] = -1;

            close(serv_cgi[1]);
            serv_cgi[1] = -1;
            resp->cgi.to_script = -1;
        }

        close(cgi_serv[0]);
        cgi_serv[0] = -1;
        resp->cgi.from_script = -1;

        close(cgi_serv[1]);
        cgi_serv[1] = -1;
        return n;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::cgi_stdin(Stream *resp, int fd)
{
    int ret = write(fd, resp->post_data.ptr_remain(), resp->post_data.size_remain());
    if (ret <= 0)
    {
        if (errno == EAGAIN)
        {
            print_err(resp, "<%s:%d> Error write(): EAGAIN\n", __func__, __LINE__);
            return ERR_TRY_AGAIN;
        }
        else
        {
            print_err(resp, "<%s:%d> Error write()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
            return -1;
        }
    }

    resp->cgi.timer = 0;
    resp->post_data.inc_offset(ret);
    if (resp->post_data.size_remain() == 0)
    {
        resp->post_data.init();
        if (resp->post_content_len <= 0)
        {
            resp->cgi_status = CGI_STDOUT;
            if (resp->cgi_type <= PHPCGI)
            {
                if (resp->cgi.to_script > 0)
                {
                    close(resp->cgi.to_script);
                    resp->cgi.to_script = -1;
                }
            }
        }
    }

    return ret;
}
//======================================================================
int EventHandlerClass::cgi_stdout(Connect *c, Stream *resp, int fd, int buf_size)
{
    if (resp->create_headers && resp->buf.size_remain())
        return resp->buf.size_remain();
    char buf[16384];
    //char buf[256];
    buf_size = sizeof(buf);
    int ret = read(fd, buf, buf_size);
    if (ret == -1)
    {
        print_err(resp, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
        return -1;
    }
    else if (ret > 0)
    {
        resp->cgi.timer = 0;
        resp->buf.ncat(buf, ret);
    }

    return ret;
}
//======================================================================
int is_cgi(Stream *resp)
{
    const char *p = strrchr(resp->clean_decode_path, '/');
    if (!p)
        return -1;
    fcgi_list_addr *i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        if (i->script_name[0] == '~')
        {
            if (!strcmp(p, i->script_name.c_str() + 1))
                break;
        }
        else
        {
            if (resp->clean_decode_path == i->script_name)
                break;
        }
    }

    if (!i)
        return -1;

    resp->cgi.socket = &i->addr;
    if (i->type == FASTCGI)
        resp->cgi_type = FASTCGI;
    else if (i->type == SCGI)
        resp->cgi_type = SCGI;
    else
    {
        resp->source_data = NO_SOURCE;
        return -1;
    }

    resp->source_data = DYN_PAGE;
    resp->resp_status = RS200;

    return 0;
}
//======================================================================
void EventHandlerClass::http1_get_cgi_headers(Connect *c)
{
    int ret = cgi_parse_headers(c, &c->h1->resp, false);
    if (ret < 0)
    {
        print_err(c, "<%s:%d> Error cgi_parse_headers() = -1\n", __func__, __LINE__);
        c->err = -RS502;
        http1_end_request(c);
        return;
    }
    else if (ret == 0)
    {
        print_err(c, "<%s:%d> cgi_parse_headers() = 0\n", __func__, __LINE__);
        return;
    }

    if (c->h1->resp.resp_status == 0)
        c->h1->resp.resp_status = RS200;
    if ((c->h1->resp.httpMethod == M_HEAD) || (c->h1->resp.resp_status == RS204))
    {
        c->h1->resp.cgi.end = true;
        c->h1->resp.send_data.init();
    }
    c->h1->resp.resp_content_len = -1;
    if (create_response_headers(c) < 0)
    {
        c->err = -1;
        http1_end_request(c);
    }
    else
    {
        c->h1->resp.create_headers = true;
    }

    if (c->h1->resp.buf.size_remain() > 0)
    {
        if (c->h1->chunk_mode == CHUNK)
        {
            c->h1->resp.send_data.ncpy("01234567", 8);
            c->h1->resp.send_data.ncat(c->h1->resp.buf.ptr_remain(), c->h1->resp.buf.size_remain());
            int ret = cgi_set_size_chunk(&c->h1->resp.send_data);
            if (ret < 0)
            {
                print_err(c, "<%s:%d> Error cgi_set_size_chunk()\n", __func__, __LINE__);
                c->err = -RS502;
                http1_end_request(c);
                return;
            }
        }
        else
            c->h1->resp.send_data.ncpy(c->h1->resp.buf.ptr_remain(), c->h1->resp.buf.size_remain());
        c->h1->resp.buf.init();
    }
    else
    {
        c->h1->resp.buf.init();
    }
}
//======================================================================
void EventHandlerClass::http2_get_cgi_headers(Connect* c, Stream *resp)
{
    int ret = cgi_parse_headers(c, resp, true);
    if (ret < 0)
    {
        print_err(resp, "<%s:%d> Error cgi_parse_headers() = -1\n", __func__, __LINE__);
        set_error_message(c, resp, RS502);
        return;
    }
    else if (ret == 0)
    {
        return;
    }

    set_frame_headers(resp);
    char str_status[32];
    snprintf(str_status, sizeof(str_status), "%d", resp->resp_status);
    add_header(resp->headers, 8, str_status);                      // :status
    add_header(resp->headers, 54, conf->ServerSoftware.c_str());   // server
    add_header(resp->headers, 33, get_time().c_str());             // date
    add_cgi_headers(resp);
//hex_print_stderr(__func__, __LINE__, resp->headers.ptr(), resp->headers.size());
    if (resp->resp_status == RS204)
    {
        add_header(resp->headers, 28, "0");
        set_frame_flags(&resp->headers, FLAG_END_STREAM);
        resp->create_headers = true;
        resp->cgi.end = true;
        resp->buf.init();
        return;
    }

    if (resp->buf.size_remain() == 0)
        resp->buf.init();
    resp->create_headers = true;
}
//======================================================================
int cgi_parse_headers(Connect* c, Stream *resp, bool lower_case)
{
    const int MAX_HEADER_LEN = 512;
    const char *p = resp->buf.ptr_remain();
    unsigned int size = resp->buf.size_remain();

    char name[512];
    char val[512];
    name[0] = 0;
    val[0] = 0;

    int name_len = 0;
    int val_len = 0;

    for (unsigned int i = 0; i < size; )
    {
        for ( name_len = 0; i < size; ) // name
        {
            if (i > MAX_HEADER_LEN)
            {
                print_err(c, "<%s:%d> Error: size of header > %d bytes\n", __func__, __LINE__, i);
                return -1;
            }

            char ch = *(p++);
            ++i;
            if (ch == ':')
                break;
            else if (ch == '\r')
            {
                if (name_len)
                {
                    print_err(c, "<%s:%d> Error\n", __func__, __LINE__);
                    return -1;
                }

                if (i < size)
                {
                    ch = *p++;
                    ++i;
                    if (ch == '\n') // empty line
                    {
                        name[name_len] = 0;
                        resp->buf.inc_offset(i);
                        if (resp->buf.size_remain() == 0)
                            resp->buf.init();
                        return 1;
                    }
                    else
                    {
                        print_err(c, "<%s:%d> Error: \\n not found\n", __func__, __LINE__);
                        return -1;
                    }
                }

                return 0;
            }
            else if (ch == '\n') // empty line
            {
                if (name_len)
                {
                    print_err(c, "<%s:%d> Error\n", __func__, __LINE__);
                    return -1;
                }

                resp->buf.inc_offset(i);
                if (resp->buf.size_remain() == 0)
                    resp->buf.init();
                return 1;
            }
            else if (ch == 0)
            {
                print_err(c, "<%s:%d> Error: character = 0\n", __func__, __LINE__);
                return -1;
            }
            else
            {
                name[name_len++] = ch;
                if ((int)sizeof(name) <= name_len)
                {
                    print_err(c, "<%s:%d> Error: names size >= %d\n", __func__, __LINE__, name_len);
                    return -1;
                }
            }
        }

        name[name_len] = 0;

        if (i == size)
            return 0;
        if (lower_case)
        {
            for (int n = 0; n < name_len; ++n)
            {
                name[n] = tolower(name[n]);
            }
        }

        for ( val_len = 0; i < size; ) // value
        {
            if (i > MAX_HEADER_LEN)
            {
                print_err(c, "<%s:%d> Error: size of header > %d bytes\n", __func__, __LINE__, i);
                return -1;
            }

            char ch = *(p++);
            ++i;

            if (ch == '\r')
                continue;
            else if (ch == '\n')
            {
                val[val_len] = 0;
                if (!strcmp_case(name, "status"))
                {
                    sscanf(val, "%d", &resp->resp_status);
                }
                else
                {
                    // add header
                    if (c->Protocol == P_HTTP1)
                    {
                        c->h1->hdrs.ncat(name, name_len);
                        c->h1->hdrs.ncat(": ", 2);
                        c->h1->hdrs.ncat(val, val_len);
                        c->h1->hdrs.ncat("\r\n", 2);
                    }
                    else if (c->Protocol == P_HTTP2)
                    {
                        //print_err(resp, "<%s:%d> [%s: %s]\n", __func__, __LINE__, name, val);
                        add_header(resp->cgi_headers, name, val);
                    }
                }
                
                resp->buf.inc_offset(i);
                size = resp->buf.size_remain();
                if (resp->buf.size_remain() == 0)
                    resp->buf.init();
                i = 0;
                break;
            }
            else if (ch == 0)
            {
                print_err(c, "<%s:%d> Error: character = 0\n", __func__, __LINE__);
                return -1;
            }
            else if (ch == ' ')
            {
                if (val_len)
                    val[val_len++] = ch;
            }
            else
                val[val_len++] = ch;
        }

        if (i == size)
            return 0;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::cgi_worker_(Connect *c, Stream *resp, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int events = poll_fd[cgi_ind_poll].events;
    int fd = poll_fd[cgi_ind_poll].fd;

    if (resp->cgi_status == CGI_STDIN)
    {
        if (resp->cgi_type <= PHPCGI)
        {
            if (resp->cgi.to_script != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.to_script=%d, fd=%d\n", __func__, __LINE__, resp->cgi.to_script, fd);
                return RS500;
            }
        }
        else
        {
            if (resp->cgi.fd != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.fd=%d, fd=%d, 0x%02X\n", __func__, __LINE__, resp->cgi.fd, fd, revents);
                return RS502;
            }
        }

        if (revents == POLLOUT)
        {
            int ret = cgi_stdin(resp, fd);
            if (ret == ERR_TRY_AGAIN)
            {
                print_err(resp, "<%s:%d> Error cgi_stdin ERR_TRY_AGAIN\n", __func__, __LINE__);
                return 0;
            }
            else if (ret < 0)
            {
                print_err(resp, "<%s:%d> Error cgi_stdin()=%d\n", __func__, __LINE__, ret);
                return RS502;
            }
            else
            {
                if (c->Protocol == P_HTTP1)
                {
                    if (resp->post_content_len <= 0)
                        c->h1->con_status = SEND_RESP_HEADERS;
                }
            }
        }
        else if (revents)
        {
            print_err(resp, "<%s:%d> Error events/revents=0x%02X/0x%02X, fd=%d\n", __func__, __LINE__,
                    events, revents, fd);
            return  RS502;
        }
    }
    else if (resp->cgi_status == CGI_STDOUT)
    {
        if (resp->buf.size() && resp->create_headers)
            return 0;
        if (resp->cgi_type <= PHPCGI)
        {
            if (resp->cgi.from_script != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.from_script=%d, fd=%d, 0x%02X\n", __func__, __LINE__,
                                        resp->cgi.from_script, fd, revents);
                return  RS502;
            }
        }
        else
        {
            if (resp->cgi.fd != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.fd=%d, fd=%d, 0x%02X\n", __func__, __LINE__,
                                        resp->cgi.fd, fd, revents);
                return RS502;
            }
        }

        if (revents & POLLIN)
        {
            if (c->Protocol == P_HTTP2)
            {
                if (resp->buf.size_remain() >= c->h2->HTTP2_SendBufSize)
                    return 0;
            }
            else
            {
                if (resp->send_data.size() >= conf->HTTP1_DataBufSize)
                    return 0;
            }

            int buf_size = 16000;
            if (c->Protocol == P_HTTP2)
                buf_size = c->h2->HTTP2_SendBufSize;
            int ret = cgi_stdout(c, resp, fd, buf_size);
            if (ret == ERR_TRY_AGAIN)
            {
                print_err(resp, "<%s:%d> cgi_stdout()=ERR_TRY_AGAIN\n", __func__, __LINE__);
                return 0;
            }
            else if (ret < 0)
            {
                print_err(resp, "<%s:%d> Error cgi_stdout()=%d\n", __func__, __LINE__, ret);
                return RS502;
            }
            else if (ret == 0)
            {
                if (resp->buf.size())
                {
                    print_err(resp, "<%s:%d> Error cgi_stdout()=%d\n", __func__, __LINE__, ret);
                    return RS502;
                }

                if (resp->cgi_type <= PHPCGI)
                {
                    if (resp->cgi.from_script > 0)
                    {
                        close(resp->cgi.from_script);
                        resp->cgi.from_script = -1;
                    }
                }
                else
                {
                    if (resp->cgi.fd > 0)
                    {
                        close(resp->cgi.fd);
                        resp->cgi.fd = -1;
                    }
                }

                resp->cgi.end = true;
                if (c->Protocol == P_HTTP1)
                {
                    if (c->h1->resp.create_headers)
                    {
                        if (c->h1->chunk_mode == CHUNK)
                        {
                            char s[] = "0\r\n\r\n";
                            c->h1->resp.send_data.strcat(s);
                        }
                        else
                        {
                            if (c->h1->resp.send_data.size() == 0)
                                http1_end_request(c);
                        }
                    }
                    else
                    {
                        print_err(c, "<%s:%d> Error: empty line not found\n", __func__, __LINE__);
                        return RS502;
                    }
                }
            }
            else
            {
                if (c->Protocol == P_HTTP2)
                {
                    if (resp->create_headers == false)
                        http2_get_cgi_headers(c, resp);
                }
                else
                {
                    if (resp->create_headers == false)
                        http1_get_cgi_headers(c);
                    else
                    {
                        if (c->h1->chunk_mode == CHUNK)
                        {
                            c->h1->resp.send_data.ncpy("01234567", 8);
                            c->h1->resp.send_data.ncat(c->h1->resp.buf.ptr_remain(), c->h1->resp.buf.size_remain());
                            int ret = cgi_set_size_chunk(&c->h1->resp.send_data);
                            if (ret < 0)
                            {
                                print_err(c, "<%s:%d> Error cgi_set_size_chunk()\n", __func__, __LINE__);
                                return RS502;
                            }
                        }
                        else
                            c->h1->resp.send_data.ncpy(c->h1->resp.buf.ptr_remain(), c->h1->resp.buf.size_remain());
                        c->h1->resp.buf.init();
                    }
                }
            }
        }
        else if (revents)
        {
            if ((resp->headers.size_remain() || resp->send_data.size_remain() || resp->buf.size_remain()) && resp->create_headers)
            {
                return 0;
            }

            if (resp->cgi_type <= PHPCGI)
            {
                if (resp->cgi.from_script > 0)
                {
                    close(resp->cgi.from_script);
                    resp->cgi.from_script = -1;
                }
            }
            else if (resp->cgi_type == SCGI)
            {
                if (resp->cgi.fd > 0)
                {
                    close(resp->cgi.fd);
                    resp->cgi.fd = -1;
                }
            }

            resp->buf.init();
            resp->cgi.end = true;

            if (resp->send_headers == false)
            {
                print_err(resp, "<%s:%d> Error 502 Bad Gateway (revents=0x%02X)\n", __func__, __LINE__, revents);
                return RS502;
            }

            if (c->Protocol == P_HTTP1)
            {
                if (c->h1->chunk_mode == CHUNK)
                {
                    char s[] = "0\r\n\r\n";
                    resp->send_data.ncpy(s, 5);
                }
                else
                {
                    resp->send_data.init();
                    http1_end_request(c);
                }
            }
        }
    }

    return 0;
}
//======================================================================
void EventHandlerClass::cgi_worker(Connect *c, Stream *resp, int cgi_ind_poll)
{
    int err = cgi_worker_(c, resp, cgi_ind_poll);
    if (err)
    {
        if (c->Protocol == P_HTTP2)
        {
            set_error_message(c, resp, err);
        }
        else
        {
            c->err = -err;
            http1_end_request(c);
        }
    }
}
