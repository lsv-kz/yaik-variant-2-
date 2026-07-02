#include "main.h"

using namespace std;
//======================================================================
static int read_request_headers(Connect *c);
//======================================================================
static int set_response(Connect *c)
{
    if ((c->h1->resp.httpMethod != M_GET) &&
        (c->h1->resp.httpMethod != M_HEAD) &&
        (c->h1->resp.httpMethod != M_POST)
    )
    {
        return -RS501;
    }

    if (c->h1->resp.host.size() == 0)
    {
        print_err(c, "<%s:%d> size Host: %d\n", __func__, __LINE__, c->h1->resp.host.size());
        return -RS400;
    }

    c->h1->resp.vhost = NULL;
    VHost *h = c->serv->vhosts;
    for ( ; h; h = h->next)
    {
        if (!strncmp(h->hostname.c_str(), c->h1->resp.host.c_str(), h->hostname.size()))
        {
            c->h1->resp.vhost = h;
            break;
        }
    }

    if (c->h1->resp.vhost == NULL)
        c->h1->resp.vhost = c->serv->vhosts; // The first server is the default server.

    if ((c->numReq >= (unsigned int)conf->MaxRequestsPerClient) || (conf->TimeoutKeepAlive == 0))
        c->h1->connKeepAlive = false;
    int path_len = 0;
    c->h1->resp.decode_query_string = "";
    const char *p = strchr(c->h1->resp.path.c_str(), '?');
    if (p)
    {
        c->h1->resp.query_string = p + 1;
        path_len = p - c->h1->resp.path.c_str();
    }
    else
    {
        c->h1->resp.query_string = "";
        path_len = c->h1->resp.path.size();
    }

    decode(c->h1->resp.path.c_str(), path_len, c->h1->resp.decode_path);

    int len = c->h1->resp.decode_path.size();
    if (len >= c->h1->resp.clean_decode_path_size)
    {
        if (c->h1->resp.clean_decode_path)
        {
            delete [] c->h1->resp.clean_decode_path;
            c->h1->resp.clean_decode_path = NULL;
            c->h1->resp.clean_decode_path_size = 0;
        }

        c->h1->resp.clean_decode_path = new(nothrow) char [len + 1];
        if (c->h1->resp.clean_decode_path == NULL)
        {
            print_err(c, "<%s:%d> Error new char [%d]: %s\n", __func__, __LINE__, len + 1, strerror(errno));
            return -RS500;
        }

        c->h1->resp.clean_decode_path_size = len + 1;
    }

    memcpy(c->h1->resp.clean_decode_path, c->h1->resp.decode_path.c_str(), len);
    c->h1->resp.clean_decode_path[len] = 0;

    if (c->h1->resp.query_string.size())
    {
        decode(c->h1->resp.query_string.c_str(), c->h1->resp.query_string.size(), c->h1->resp.decode_query_string);
    }

    int err = clean_path(c->h1->resp.clean_decode_path, len);
    if (err <= 0)
    {
        print_err(c, "<%s:%d> Error: clean_path[%d]\n", __func__, __LINE__, err);
        return -RS400;
    }
    //------------------------------------------------------------------
    if (!strncmp(c->h1->resp.clean_decode_path, "/cgi-bin/", 9) || !strncmp(c->h1->resp.clean_decode_path, "/cgi/", 5))
    {
        c->h1->resp.cgi_type = CGI;
        c->h1->resp.cgi_status = CGI_CREATE;
        c->h1->resp.source_data = DYN_PAGE;
        return 0;
    }

    if (strstr(c->h1->resp.clean_decode_path, ".php"))
    {
        if (conf->UsePHP == "php-cgi")
            c->h1->resp.cgi_type = PHPCGI;
        else if (conf->UsePHP == "php-fpm")
            c->h1->resp.cgi_type = PHPFPM;
        else
            return -RS404;
        c->h1->resp.cgi_status = CGI_CREATE;
        c->h1->resp.source_data = DYN_PAGE;
        return 0;
    }
    //------------------------------------------------------------------
    string path;
    path.reserve(c->h1->resp.vhost->DocumentRoot.size() + c->h1->resp.clean_decode_path_size + 256);
    path = c->h1->resp.vhost->DocumentRoot;
    path += c->h1->resp.clean_decode_path;
    struct stat st;
    int ret = lstat(path.c_str(), &st);
    if (ret == -1)
    {
        if (errno == EACCES)
            return -RS403;
        ret = is_cgi(&c->h1->resp);
        if (ret < 0)
        {
            print_err(c, "<%s:%d> File \"%s\" not found\n", __func__, __LINE__, c->h1->resp.clean_decode_path);
            return -RS404;
        }
        else
        {
            c->h1->resp.cgi_status = CGI_CREATE;
            return 0;
        }
    }
    else
    {
        if ((!S_ISDIR(st.st_mode)) && (!S_ISREG(st.st_mode)))
        {
            print_err(c, "<%s:%d> Error: file (!S_ISDIR && !S_ISREG) \n", __func__, __LINE__);
            return -RS403;
        }
    }

    if (S_ISDIR(st.st_mode))
    {
        if (c->h1->resp.clean_decode_path[strlen(c->h1->resp.clean_decode_path) - 1] != '/')
        {
            c->h1->resp.resp_status = RS301;
            c->h1->resp.path.insert(path_len, "/");
            c->h1->hdrs.strcpy("Location: ");
            c->h1->hdrs.ncat(c->h1->resp.path.c_str(), c->h1->resp.path.size());
            c->h1->hdrs.ncat("\r\n", 2);
            return send_message(c, "301 Moved Permanently");
        }

        int ret = index_dir(c, path.c_str(), c->h1->resp.clean_decode_path, &c->h1->resp.send_data);
        if (ret < 0)
        {
            return ret;
        }

        c->h1->resp.resp_status = RS200;
        c->h1->resp.source_data = FROM_DATA_BUFFER;
        c->h1->resp.resp_content_len = c->h1->resp.send_data.size();
        c->h1->resp.resp_content_type = "text/html;charset=UTF-8";
        if (create_response_headers(c))
            return -1;
        return 0;
    }
    //--------------------- send file ----------------------------------
    c->h1->resp.source_data = FROM_FILE;
    c->h1->resp.file_size = file_size(path.c_str());
    c->h1->resp.resp_content_type = content_type(path.c_str());
    c->h1->resp.fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (c->h1->resp.fd == -1)
    {
        print_err(c, "<%s:%d> Error open(%s): %s\n", __func__, __LINE__,
                                    path.c_str(), strerror(errno));
        if (errno == EACCES)
            return -RS403;
        else
            return -RS404;
    }

    if (c->h1->resp.range.size())
    {
        int ret = parse_range(c->h1->resp.range.c_str(), c->h1->resp.file_size, &c->h1->resp.offset, &c->h1->resp.resp_content_len);
        if (ret < 0)
        {
            print_err(c, "<%s:%d> Error parse_range(%s)=%d\n", __func__, __LINE__, c->h1->resp.range.c_str(), ret);
            return ret;
        }
        else if (ret == 0)
        {
            c->h1->resp.resp_content_len = c->h1->resp.file_size;
            c->h1->resp.resp_status = RS200;
        }
        else
        {
            if (c->h1->resp.offset > 0)
                lseek(c->h1->resp.fd, c->h1->resp.offset, SEEK_SET);
            c->h1->resp.resp_status = RS206;
        }
    }
    else
    {
        c->h1->resp.resp_content_len = c->h1->resp.file_size;
        c->h1->resp.resp_status = RS200;
    }

    if (create_response_headers(c))
        return -1;
    return 0;
}
//======================================================================
int EventHandlerClass::http1_worker(Connect *c, int revents)
{
    if ((c->h1->con_status == READ_REQUEST) && (revents & POLLIN))
    {
        int ret = read_request_headers(c);
        if (ret == 1)
        {
            ret = set_response(c);
            if (ret < 0)
            {
                //print_err(c, "<%s:%d> set_response()=%d\n", __func__, __LINE__, ret);
                c->h1->connKeepAlive = false;
                c->err = ret;
                http1_end_request(c);
                return -1;
            }

            c->client_timer = 0;
            if (c->h1->resp.source_data == DYN_PAGE)
            {
                if (c->h1->resp.post_content_len > 0)
                    c->h1->con_status = READ_POSTDATA;
                else
                    c->h1->con_status = SEND_RESP_HEADERS;
            }
            else
            {
                c->h1->con_status = SEND_RESP_HEADERS;
            }
        }
        else if (ret == 0)
            c->client_timer = 0;
        else
        {
            if (ret != ERR_TRY_AGAIN)
            {
                c->h1->resp.buf.init();
                c->err = ret;
                c->h1->connKeepAlive = false;
                http1_end_request(c);
                return -1;
            }
            else
                print_err(c, "<%s:%d> read_request_headers()=ERR_TRY_AGAIN\n", __func__, __LINE__);
        }
    }
    else if ((c->h1->con_status == READ_POSTDATA) && (revents & POLLIN))
    {
        int ret = read_post_data(c);
        if (ret <= 0)
        {
            if (ret != ERR_TRY_AGAIN)
            {
                print_err(c, "<%s:%d> Error read_post_data()=%d\n", __func__, __LINE__, ret);
                if (ret == 0)
                    c->err = -1;
                else
                    c->err = ret;
                http1_end_request(c);
                return -1;
            }
            else
                print_err(c, "<%s:%d> read_post_data()=ERR_TRY_AGAIN\n", __func__, __LINE__);
        }
    }
    else if ((c->h1->con_status == SEND_RESP_HEADERS) && (revents & POLLOUT))
    {
        if (c->h1->resp.headers.size_remain())
        {
            int ret = write_to_client(c, c->h1->resp.headers.ptr_remain(), c->h1->resp.headers.size_remain(), 0);
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                {
                    print_err(c, "<%s:%d> Error send headers: %d, %d/%d\n", __func__, __LINE__,
                            ret, c->h1->resp.headers.size_remain(), c->h1->resp.headers.size());
                    c->err = ret;
                    http1_end_request(c);
                    return -1;
                }
                else
                    print_err(c, "<%s:%d> SEND_RESP_HEADERS: ERR_TRY_AGAIN\n", __func__, __LINE__);
            }
            else
            {
                c->client_timer = 0;
                c->h1->resp.headers.inc_offset(ret);
                if (c->h1->resp.headers.size_remain())
                    return ERR_TRY_AGAIN;
                c->h1->resp.headers.init();
                c->h1->resp.send_headers = true;
                if (c->h1->resp.httpMethod == M_HEAD)
                    http1_end_request(c);
                else
                {
                    if (c->h1->resp.resp_content_len == 0)
                        http1_end_request(c);
                    else
                        c->h1->con_status = SEND_ENTITY;
                }
            }
        }
    }
    else if ((c->h1->con_status == SEND_ENTITY) && (revents & POLLOUT))
    {
        int write_bytes = 0;
        if (c->h1->resp.source_data == FROM_FILE)
        {
            int data_len = conf->HTTP1_DataBufSize;;
            if (c->h1->resp.resp_content_len < data_len)
                data_len = c->h1->resp.resp_content_len;
            write_bytes = read(c->h1->resp.fd, snd_buf, data_len);
            if (write_bytes < 0)
            {
                print_err(c, "<%s:%d> Error read(fd=%d)=%d: %s\n", __func__, __LINE__, 
                            c->h1->resp.fd, write_bytes, strerror(errno));
                c->err = -1;
                http1_end_request(c);
                return -1;
            }
            else if (write_bytes == 0)
            {
                print_err(c, "<%s:%d> Error read(fd=%d)=%d\n", __func__, __LINE__, 
                            c->h1->resp.fd, write_bytes);
                c->err = -1;
                http1_end_request(c);
                return -1;
            }
        }
        else
        {
            if (c->h1->resp.send_data.size_remain() > 0)
            {
                if (c->h1->resp.send_data.size_remain() > conf->HTTP1_DataBufSize)
                    write_bytes = conf->HTTP1_DataBufSize;
                else
                    write_bytes = c->h1->resp.send_data.size_remain();
                memcpy(snd_buf, c->h1->resp.send_data.ptr_remain(), write_bytes);
            }
            else
            {
                print_err(c, "<%s:%d> Error send_data=0, %s\n", __func__, __LINE__, get_str_sourcedata(c->h1->resp.source_data));
                c->err = -1;
                http1_end_request(c);
                return -1;
            }
        }

        int ret = write_to_client(c, snd_buf, write_bytes, 0);
        if (ret < 0)
        {
            if (ret != ERR_TRY_AGAIN)
            {
                print_err(c, "<%s:%d> Error send data: %d, send %lld bytes\n", __func__, __LINE__,
                            ret, c->h1->resp.send_bytes);
                c->err = ret;
                http1_end_request(c);
                return -1;
            }

            if (c->h1->resp.source_data == FROM_FILE)
            {
                lseek(c->h1->resp.fd, -write_bytes, SEEK_CUR);
            }
        }
        else
        {
            c->client_timer = 0;
            c->h1->resp.send_bytes += ret;
            if (c->h1->resp.source_data == FROM_FILE)
            {
                c->h1->resp.resp_content_len -= ret;
                if (ret < write_bytes)
                    lseek(c->h1->resp.fd, ret - write_bytes, SEEK_CUR);
                if (c->h1->resp.resp_content_len == 0)
                    http1_end_request(c);
            }
            else
            {
                if (c->h1->resp.source_data == FROM_DATA_BUFFER)
                    c->h1->resp.resp_content_len -= ret;
                c->h1->resp.send_data.inc_offset(ret);
                if (c->h1->resp.send_data.size_remain())
                {
                    //print_err(c, "<%s:%d> write_to_client()=%d, %d/%d\n", __func__, __LINE__,
                    //            ret, c->h1->resp.send_data.size_remain(), c->h1->resp.send_data.size());
                    return 0;
                }

                c->h1->resp.send_data.init();
                if (c->h1->resp.source_data == DYN_PAGE)
                {
                    if (c->h1->resp.cgi.end)
                    {
                        http1_end_request(c);
                    }
                }
                else if (c->h1->resp.source_data == FROM_DATA_BUFFER)
                {
                    if (c->h1->resp.resp_content_len == 0)
                        http1_end_request(c);
                }
            }
        }
    }
    else if (c->h1->con_status == HTTP1_SHUTDOWN)
    {
        ERR_clear_error();
        char buf[256];
        int err = SSL_read(c->tls.ssl, buf, sizeof(buf));
        if (err <= 0)
        {
            c->tls.err = SSL_get_error(c->tls.ssl, err);
            if (c->tls.err == SSL_ERROR_WANT_READ)
            {
                c->tls.poll_events = POLLIN;
            }
            else if (c->tls.err == SSL_ERROR_WANT_WRITE)
            {
                c->tls.poll_events = POLLOUT;
            }
            else
            {
                close_connect(c);
                return -1;
            }
        }
        else
        {
            print_err(c, "<%s:%d> SSL_SHUTDOWN: SSL_read()=%d\n", __func__, __LINE__, err);
            c->client_timer = 0;
            c->tls.shutdown_timer = 0;
            if (conf->PrintDebugMsg)
                hex_print_stderr("recv SSL_SHUTDOWN", __LINE__, buf, err);
        }
        return 0;
    }
    else
    {
        if (conf->PrintDebugMsg)
            print_err(c, "<%s:%d> %s, 0x%02X\n", __func__, __LINE__, c->h1->get_str_status(), revents);
        c->err = -1;
        http1_end_request(c);
    }

    return 0;
}
//======================================================================
void EventHandlerClass::http1_end_request(Connect *c)
{
    if (c->h1->resp.source_data == DYN_PAGE)
    {
        if ((c->h1->resp.cgi_type == CGI) ||
            (c->h1->resp.cgi_type == PHPCGI))
        {
            if (c->h1->resp.cgi.from_script > 0)
            {
                close(c->h1->resp.cgi.from_script);
                c->h1->resp.cgi.from_script = -1;
            }

            if (c->h1->resp.cgi.to_script > 0)
            {
                close(c->h1->resp.cgi.to_script);
                c->h1->resp.cgi.to_script = -1;
            }

            kill_chld(c->h1->resp.cgi.pid);
        }
        else if ((c->h1->resp.cgi_type == PHPFPM) ||
                (c->h1->resp.cgi_type == FASTCGI) ||
                (c->h1->resp.cgi_type == SCGI))
        {
            if (c->h1->resp.cgi.fd > 0)
            {
                shutdown(c->h1->resp.cgi.fd, SHUT_RDWR);
                close(c->h1->resp.cgi.fd);
                c->h1->resp.cgi.fd = -1;
            }
        }

        if (c->h1->resp.cgi.start)
        {
            --cgi_num_work;
            c->h1->resp.cgi.start = false;
        }
    }
    else
    {
        if (c->h1->resp.source_data == FROM_FILE)
        {
            if (c->h1->resp.fd > 0)
            {
                close(c->h1->resp.fd);
                c->h1->resp.fd = -1;
            }
        }
        else if (c->h1->resp.source_data == FROM_DATA_BUFFER)
        {
            c->h1->resp.send_data.init();
        }
    }

    if ((c->h1->connKeepAlive == false) || c->err < 0)
    {
        if (c->h1->resp.send_headers || (c->err == ERR_TRY_AGAIN))
            c->err = -1;

        if (c->err <= -RS101) // err < -100
        {
            if (c->h1->resp.httpMethod == M_POST)
                c->h1->connKeepAlive = false;
            c->h1->resp.resp_status = -c->err;
            c->err = 0;
            c->h1->hdrs.init();
            if (send_message(c, NULL) == 0)
                return;
        }

        if (c->h1->con_status > READ_REQUEST)
        {
            print_log(c);
        }

        if (c->tls.ssl && c->SecureConnect)
        {
            ssl_shutdown(c);
        }
        else
        {
            close_connect(c);
        }
    }
    else
    {
        print_log(c);
        c->h1->init();
        ++c->numReq;
        c->h1->con_status = READ_REQUEST;
    }
}
//======================================================================
static int parse_startline_request(Connect *c, const char *s)
{
    if (s == NULL)
    {
        print_err(c, "<%s:%d> Error: start line is empty\n",  __func__, __LINE__);
        return -1;
    }
    //----------------------------- method -----------------------------
    int i = 0;
    char ch, buf[16];
    for ( ; i < ((int)sizeof(buf) - 1); ++i)
    {
        ch = s[i];
        if ((ch == ' ') || (ch == 0))
            break;
        else
            buf[i] = ch;
    }

    buf[i] = 0;
    c->h1->resp.httpMethod = get_int_method(buf);
    if (c->h1->resp.httpMethod == M_NULL)
    {
        print_err(c, "<%s:%d> Error httpMethod=0, [%s]\n",  __func__, __LINE__, buf);
        return -RS400;
    }
    //------------------------------- uri ------------------------------
    ++i;
    if (s[i] == ' ')
    {
        print_err(c, "<%s:%d> Error 400, [%s]\n",  __func__, __LINE__, s);
        return -RS400;
    }

    int j = i;
    while ((ch = s[i]))
    {
        if (ch == ' ')
            break;
        ++i;
    }

    c->h1->resp.path.assign(s + j, i - j);
    //------------------------------ version ---------------------------
    ++i;
    if (s[i] == ' ')
    {
        print_err(c, "<%s:%d> Error 400, [%s]\n",  __func__, __LINE__, s);
        return -RS400;
    }

    if (strcmp(s + i, "HTTP/1.1"))
    {
        print_err(c, "<%s:%d> Error version protocol: [%s]\n", __func__, __LINE__, s + i);
        return -RS400;
    }

    return 0;
}
//======================================================================
static int parse_header(Connect *c, const char *s)
{
    if (s == NULL)
    {
        print_err(c, "<%s:%d> Error: header is empty\n",  __func__, __LINE__);
        return -1;
    }

    char ch;
    int colon = 0;
    int i = 0;
    while ((ch = s[i]))
    {
        i++;
        if (ch == ':')
            colon = 1;
        else if ((ch == ' ') || (ch == '\t'))
        {
            if (colon == 0)
            {
                print_err(c, "<%s:%d> Error 400, [%s]\n",  __func__, __LINE__, s);
                return -RS400;
            }

            break;
        }
    }

    if (s[i] == ' ')
    {
        print_err(c, "<%s:%d> Error 400, [%s]\n",  __func__, __LINE__, s);
        return -RS400;
    }

    if (strstr_case(s, "accept-encoding:"))
    {

    }
    else if (strstr_case(s, "connection:"))
    {
        if (strstr_case(s + i, "keep-alive"))
            c->h1->connKeepAlive = true;
        else
            c->h1->connKeepAlive = false;
    }
    else if (strstr_case(s, "content-length:"))
    {
        c->h1->resp.sReqContentLen = s + i;
        c->h1->resp.post_content_len = atoll(s + i);
    }
    else if (strstr_case(s, "content-type:"))
    {
        c->h1->resp.sReqContentType = s + i;
    }
    else if (strstr_case(s, "host:"))
    {
        c->h1->resp.host = s + i;
    }
    else if (strstr_case(s, "range:"))
    {
        c->h1->resp.range = s + i;
    }
    else if (strstr_case(s, "referer:"))
    {
        c->h1->resp.referer = s + i;
    }
    else if (strstr_case(s, "upgrade:"))
    {

    }
    else if (strstr_case(s, "user-agent:"))
    {
        c->h1->resp.user_agent = s + i;
    }
    else
    {
        if (conf->PrintDebugMsg)
            print_err(c, "<%s:%d> [%s]\n",  __func__, __LINE__, s);
    }

    return 0;
}
//======================================================================
static int read_request_headers(Connect *c)
{
    while (true)
    {
        int ret = socket_read_line(c);
        if (ret == 1)
        {
            if (c->h1->resp.buf.size() == 0) // empty line
            {
                if (c->h1->resp.httpMethod == M_NULL)
                {
                    print_err(c, "<%s:%d> Error: httpMethod == M_NULL\n",  __func__, __LINE__);
                    return -RS400;
                }

                return 1;
            }

            if (c->h1->resp.httpMethod == M_NULL)
            {
                int ret = parse_startline_request(c, c->h1->resp.buf.ptr());
                if (ret < 0)
                {
                    print_err(c, "<%s:%d> Error parse_startline_request()=%d\n", __func__, __LINE__, ret);
                    return ret;
                }

                c->h1->resp.buf.init();
            }
            else
            {
                int ret = parse_header(c, c->h1->resp.buf.ptr());
                if (ret < 0)
                {
                    print_err(c, "<%s:%d> Error parse_header()=%d\n", __func__, __LINE__, ret);
                    return ret;
                }

                c->h1->resp.buf.init();
            }
        }
        else if (ret == 0)
        {
            if (c->h1->resp.buf.size() > (MAX_URI + 16))
                return -RS414;
            return 0;
        }
        else
        {
            return ret;
        }
    }

    return -1;
}
//======================================================================
int create_response_headers(Connect *c)
{
    if (c->h1->resp.headers.reserve(512) < 0)
    {
        print_err(c, "<%s:%d> Error create ByteArray object\n", __func__, __LINE__);
        return -1;
    }

    if (c->h1->resp.resp_status == 0)
    {
        print_err(c, "<%s:%d> Error resp_status = 0\n", __func__, __LINE__);
        return -1;
    }

    c->h1->resp.headers.strcpy("HTTP/1.1 ");
    c->h1->resp.headers.strcat(http1_status_response(c->h1->resp.resp_status));
    c->h1->resp.headers.strcat("\r\n");

    c->h1->resp.headers.strcat("Date: ");
    c->h1->resp.headers.timecat();
    c->h1->resp.headers.strcat("\r\n");

    if (conf->ServerSoftware.size())
    {
        c->h1->resp.headers.strcat("Server: ");
        c->h1->resp.headers.strcat(conf->ServerSoftware.c_str());
        c->h1->resp.headers.strcat("\r\n");
    }

    if (c->h1->resp.resp_status == RS204)
    {
        c->h1->resp.headers.strcat("Content-Length: 0\r\n");
    }
    else
    {
        if ((c->h1->resp.source_data == DYN_PAGE) || (c->h1->resp.source_data == FROM_DATA_BUFFER))
        {
            if (c->h1->chunk_mode == CHUNK)
            {
                if ((c->h1->resp.httpMethod == M_GET) || (c->h1->resp.httpMethod == M_POST))
                {
                    c->h1->resp.headers.strcat("Transfer-Encoding: chunked\r\n");
                }
            }
            else
            {
                if (c->h1->resp.resp_content_type)
                {
                    c->h1->resp.headers.strcat("Content-Type: ");
                    c->h1->resp.headers.strcat(c->h1->resp.resp_content_type);
                    c->h1->resp.headers.strcat("\r\n");
                }
                if (c->h1->resp.resp_content_len >= 0)
                {
                    c->h1->resp.headers.strcat("Content-Length: ");
                    c->h1->resp.headers.cat_int(c->h1->resp.resp_content_len);
                    c->h1->resp.headers.strcat("\r\n");
                }
            }
        }
        else
        {
            if (c->h1->resp.resp_status == RS206)
            {
                if (c->h1->resp.resp_content_type)
                {
                    c->h1->resp.headers.strcat("Content-Type: ");
                    c->h1->resp.headers.strcat(c->h1->resp.resp_content_type);
                    c->h1->resp.headers.strcat("\r\n");
                }

                c->h1->resp.headers.strcat("Content-Length: ");
                c->h1->resp.headers.cat_int(c->h1->resp.resp_content_len);
                c->h1->resp.headers.strcat("\r\n");

                c->h1->resp.headers.strcat("Content-Range: bytes ");
                c->h1->resp.headers.cat_int(c->h1->resp.offset);
                c->h1->resp.headers.strcat("-");
                c->h1->resp.headers.cat_int(c->h1->resp.offset + c->h1->resp.resp_content_len - 1);
                c->h1->resp.headers.strcat("/");
                c->h1->resp.headers.cat_int(c->h1->resp.file_size);
                c->h1->resp.headers.strcat("\r\n");

                c->h1->resp.headers.strcat("Accept-Ranges: bytes\r\n");
            }
            else if (c->h1->resp.resp_status == RS200)
            {
                if (c->h1->resp.resp_content_type)
                {
                    c->h1->resp.headers.strcat("Content-Type: ");
                    c->h1->resp.headers.strcat(c->h1->resp.resp_content_type);
                    c->h1->resp.headers.strcat("\r\n");
                }

                if (c->h1->resp.resp_content_len >= 0)
                {
                    c->h1->resp.headers.strcat("Content-Length: ");
                    c->h1->resp.headers.cat_int(c->h1->resp.resp_content_len);
                    c->h1->resp.headers.strcat("\r\n");

                    c->h1->resp.headers.strcat("Accept-Ranges: bytes\r\n");
                }
            }
            else if (c->h1->resp.resp_status == RS416)
            {
                if (c->h1->resp.resp_content_type)
                {
                    c->h1->resp.headers.strcat("Content-Type: ");
                    c->h1->resp.headers.strcat(c->h1->resp.resp_content_type);
                    c->h1->resp.headers.strcat("\r\n");
                }

                if (c->h1->resp.resp_content_len)
                {
                    c->h1->resp.headers.strcat("Content-Length: ");
                    c->h1->resp.headers.cat_int(c->h1->resp.resp_content_len);
                    c->h1->resp.headers.strcat("\r\n");
                }

                c->h1->resp.headers.strcat("Content-Range: bytes */");
                c->h1->resp.headers.cat_int(c->h1->resp.file_size);
                c->h1->resp.headers.strcat("\r\n");
            }
        }
    }

    if (c->h1->connKeepAlive == false)
        c->h1->resp.headers.strcat("Connection: close\r\n");
    else
        c->h1->resp.headers.strcat("Connection: keep-alive\r\n");

    if (c->h1->hdrs.size())
    {
        c->h1->resp.headers.ncat(c->h1->hdrs.ptr(), c->h1->hdrs.size());
        c->h1->hdrs.init();
    }

    c->h1->resp.headers.strcat("\r\n");

    if (c->h1->resp.headers.error())
    {
        print_err(c, "<%s:%d> Error create response headers\n", __func__, __LINE__);
        c->h1->resp.referer = "Error create response headers";
        return -1;
    }

    if (conf->PrintDebugMsg)
        print_err(c, "===================================\n%s\n", c->h1->resp.headers.ptr());
    return 0;
}
//======================================================================
int send_message(Connect *c, const char *msg)
{
    c->err = 0;
    c->h1->resp.send_data.init();
    c->h1->resp.send_data.reserve(256);

    if (c->h1->resp.resp_status == RS204)
    {
        c->h1->resp.resp_content_len = 0;
        c->h1->resp.resp_content_type = NULL;
    }
    else
    {
        const char *title = http1_status_response(c->h1->resp.resp_status);
        c->h1->resp.send_data.strcpy("<html>\r\n"
                "<head>\r\n"
                "<title>");
        c->h1->resp.send_data.strcat(title);
        c->h1->resp.send_data.strcat("</title>\r\n"
                "<meta charset=\"utf-8\">\r\n"
                "</head>\r\n"
                "<body>\r\n"
                "<h3>");
        c->h1->resp.send_data.strcat(title);
        c->h1->resp.send_data.strcat("</h3>\r\n");
        if (msg)
        {
            c->h1->resp.send_data.strcat("<p>");
            c->h1->resp.send_data.strcat(msg);
            c->h1->resp.send_data.strcat("</p>\r\n");
        }
        c->h1->resp.send_data.strcat("<hr>\r\n");
        c->h1->resp.send_data.timecat();
        c->h1->resp.send_data.strcat("\r\n"
                "</body>\r\n"
                "</html>");

        c->h1->resp.resp_content_type = "text/html";
        c->h1->resp.resp_content_len = c->h1->resp.send_data.size();
    }

    c->h1->chunk_mode = NO_CHUNK;
    c->h1->con_status = SEND_RESP_HEADERS;
    c->h1->resp.source_data = FROM_DATA_BUFFER;
    if (create_response_headers(c) < 0)
        return -1;
    c->client_timer = 0;
    return 0;
}
//======================================================================
int read_post_data(Connect *c)
{
    if (c->h1->resp.post_content_len <= 0)
    {
        print_err(c, "<%s:%d> Error reqContentLength=%lld\n", __func__, __LINE__, c->h1->resp.post_content_len);
        return -1;
    }

    char buf[16384];
    int len = sizeof(buf);
    int ret = read_from_client(c, buf, len);
    if (ret <= 0)
    {
        return ret;
    }

    c->h1->resp.post_content_len -= ret;
    if ((c->h1->resp.cgi_type == PHPFPM) || (c->h1->resp.cgi_type == FASTCGI))
    {
        char s[8];
        fcgi_set_header(s, FCGI_STDIN, ret);
        c->h1->resp.post_data.ncat(s, 8);
        c->h1->resp.post_data.ncat(buf, ret);
        if (c->h1->resp.post_content_len <= 0)
        {
            fcgi_set_header(s, FCGI_STDIN, 0);
            c->h1->resp.post_data.ncat(s, 8);
        }
    }
    else
    {
        c->h1->resp.post_data.ncat(buf, ret);
    }

    return ret;
}
//======================================================================
const char *http1_status_response(int st)
{
    switch (st)
    {
        case 0:
            return "";
        case RS101:
            return "101 Switching Protocols";
        case RS200:
            return "200 OK";
        case RS204:
            return "204 No Content";
        case RS206:
            return "206 Partial Content";
        case RS301:
            return "301 Moved Permanently";
        case RS302:
            return "302 Moved Temporarily";
        case RS400:
            return "400 Bad Request";
        case RS401:
            return "401 Unauthorized";
        case RS402:
            return "402 Payment Required";
        case RS403:
            return "403 Forbidden";
        case RS404:
            return "404 Not Found";
        case RS405:
            return "405 Method Not Allowed";
        case RS406:
            return "406 Not Acceptable";
        case RS407:
            return "407 Proxy Authentication Required";
        case RS408:
            return "408 Request Timeout";
        case RS411:
            return "411 Length Required";
        case RS413:
            return "413 Request entity too large";
        case RS414:
            return "414 Request-URI Too Large";
        case RS416:
            return "416 Range Not Satisfiable";
        case RS429:
            return "429 Too Many Requests";
        case RS431:
            return "431 Request Header Fields Too Large";
        case RS500:
            return "500 Internal Server Error";
        case RS501:
            return "501 Not Implemented";
        case RS502:
            return "502 Bad Gateway";
        case RS503:
            return "503 Service Unavailable";
        case RS504:
            return "504 Gateway Time-out";
        case RS505:
            return "505 HTTP Version not supported";
        default:
            return "500 Internal Server Error?";
    }
    return "";
}
