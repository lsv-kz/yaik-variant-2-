#include "main.h"

using namespace std;
//======================================================================
int EventHandlerClass::cgi_fork(Connect *c, Stream *resp, int* serv_cgi, int* cgi_serv)
{
    struct stat st;

    if (resp->cgi_type == CGI)
    {
        resp->cgi.path = conf->ScriptPath;
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
/*
        int fd = open("/dev/null", O_RDONLY);
        if (fd > 0)
        {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
*/
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
            if (chdir(conf->ScriptPath.c_str()))
            {
                fprintf(stderr, "<%s:%d> Error chdir(%s): %s\n", __func__, __LINE__, conf->ScriptPath.c_str(), strerror(errno));
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
        print_err(resp, "<%s:%d> Error pipe()=%d, id=%d \n", __func__, __LINE__, n, resp->id);
        return -1;
    }

    if (resp->httpMethod == M_POST)
    {
        n = pipe(serv_cgi);
        if (n == -1)
        {
            print_err(resp, "<%s:%d> Error pipe()=%d, id=%d \n", __func__, __LINE__, n, resp->id);
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
            print_err(resp, "<%s:%d> Error write(): EAGAIN; id=%d \n", __func__, __LINE__, resp->id);
            return ERR_TRY_AGAIN;
        }
        else
        {
            print_err(resp, "<%s:%d> Error write()=%d: %s; id=%d \n", __func__, __LINE__, ret, strerror(errno), resp->id);
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
int EventHandlerClass::cgi_stdout(Connect *c, Stream *resp, int fd)
{
    char buf[16384];
    int ret = read(fd, buf, c->h2->HTTP2_SendBufSize);
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
void EventHandlerClass::cgi_worker(Connect *c, Stream *resp, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int events = poll_fd[cgi_ind_poll].revents;
    int fd = poll_fd[cgi_ind_poll].fd;

    if (resp->cgi_status == CGI_STDIN)
    {
        if (resp->cgi_type <= PHPCGI)
        {
            if (resp->cgi.to_script != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.to_script=%d, fd=%d, id=%d \n", __func__, __LINE__,
                                        resp->cgi.to_script, fd, resp->id);
                set_error_message(c, resp, RS500);
                return;
            }
        }
        else
        {
            if (resp->cgi.fd != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.fd=%d, fd=%d, 0x%02X,  id=%d \n", __func__, __LINE__,
                                        resp->cgi.fd, fd, revents, resp->id);
                set_error_message(c, resp, RS502);
                return;
            }
        }

        if (revents == POLLOUT)
        {
            int ret = cgi_stdin(resp, fd);
            if (ret == ERR_TRY_AGAIN)
            {
                print_err(resp, "<%s:%d> Error cgi_stdin ERR_TRY_AGAIN, id=%d \n", __func__, __LINE__, resp->id);
                return;
            }
            else if (ret < 0)
            {
                print_err(resp, "<%s:%d> Error cgi_stdin()=%d\n", __func__, __LINE__, ret);
                set_error_message(c, resp, RS502);
                return;
            }
        }
        else if (revents)
        {
            print_err(resp, "<%s:%d> Error events/revents=0x%02X/0x%02X, fd=%d,   id=%d \n", __func__, __LINE__,
                    events, revents, fd, resp->id);
            set_error_message(c, resp, RS502);
        }
    }
    else if (resp->cgi_status == CGI_STDOUT)
    {
        if (resp->buf.size() && resp->create_headers)
            return;
        if (resp->cgi_type <= PHPCGI)
        {
            if (resp->cgi.from_script != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.from_script=%d, fd=%d, 0x%02X,  id=%d \n", __func__, __LINE__,
                                        resp->cgi.from_script, fd, revents, resp->id);
                set_error_message(c, resp, RS502);
                return;
            }
        }
        else
        {
            if (resp->cgi.fd != fd)
            {
                print_err(resp, "<%s:%d> Error cgi.fd=%d, fd=%d, 0x%02X,  id=%d \n", __func__, __LINE__,
                                        resp->cgi.fd, fd, revents, resp->id);
                set_error_message(c, resp, RS502);
                return;
            }
        }

        if (revents & POLLIN)
        {
            if (resp->buf.size_remain() >= c->h2->HTTP2_SendBufSize)
                return;
            int ret = cgi_stdout(c, resp, fd);
            if (ret == ERR_TRY_AGAIN)
            {
                print_err(resp, "<%s:%d> cgi_stdout()=ERR_TRY_AGAIN, id=%d \n", __func__, __LINE__, resp->id);
            }
            else if (ret < 0)
            {
                print_err(resp, "<%s:%d> Error cgi_stdout()=%d, id=%d \n", __func__, __LINE__, ret, resp->id);
                set_error_message(c, resp, RS502);
            }
            else if (ret == 0)
            {
                if (resp->buf.size())
                {
                    print_err(resp, "<%s:%d> Error cgi_stdout()=%d, id=%d \n", __func__, __LINE__, ret, resp->id);
                    set_error_message(c, resp, RS502);
                    return;
                }

                if (resp->cgi_type <= PHPCGI)
                {
                    close(resp->cgi.from_script);
                    resp->cgi.from_script = -1;
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
            }
            else if (resp->create_headers == false)
            {
                http2_get_cgi_headers(c, resp);
            }
        }
        else if (revents)
        {
            if ((resp->headers.size() || resp->send_data.size() || resp->buf.size_remain()) &&
                 resp->create_headers
            )
            {
                return;
            }

            if (resp->cgi_type <= PHPCGI)
            {
                close(resp->cgi.from_script);
                resp->cgi.from_script = -1;
            }

            resp->buf.init();
            if (resp->send_headers == false)
            {
                print_err(resp, "<%s:%d> Error 502 Bad Gateway, revents=0x%02X, id=%d \n",
                            __func__, __LINE__, revents, resp->id);
                set_error_message(c, resp, RS502);
                return;
            }

            resp->cgi.end = true;
        }
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
        //print_err(resp, "<%s:%d> cgi_parse_headers() = 0\n", __func__, __LINE__);
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
