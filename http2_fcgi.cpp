#include "main.h"

using namespace std;
//======================================================================
static int fcgi_add_param(Stream *resp, const char *name, const char *val, int len_val)
{
    if (name == NULL)
    {
        print_err(resp, "<%s:%d> Error: name=NULL\n", __func__, __LINE__);
        return -1;
    }

    int len_name = strlen(name);
    if (val == NULL)
        len_val = 0;
    char s[8], *p = s;
    int i = 0;

    if (len_name < 0x80)
    {
        *(p++) = (unsigned char)len_name;
        ++i;
    }
    else
    {
        *(p++) = (unsigned char)((len_name >> 24) | 0x80);
        *(p++) = (unsigned char)(len_name >> 16);
        *(p++) = (unsigned char)(len_name >> 8);
        *(p++) = (unsigned char)len_name;
        i += 4;
    }

    if (len_val < 0x80)
    {
        *(p++) = (unsigned char)len_val;
        ++i;
    }
    else
    {
        *(p++) = (unsigned char)((len_val >> 24) | 0x80);
        *(p++) = (unsigned char)(len_val >> 16);
        *(p++) = (unsigned char)(len_val >> 8);
        *(p++) = (unsigned char)len_val;
        i += 4;
    }

    resp->send_data.ncat(s, i);
    resp->send_data.ncat(name, len_name);
    if (len_val > 0)
    {
        resp->send_data.ncat(val, len_val);
    }

    return 0;
}
//======================================================================
int fcgi_create_params(Connect *c, Stream *resp)
{
    int ret = 0;
    resp->send_data.reserve(4096);
    resp->send_data.ncpy("\0\0\0\0\0\0\0\0", 8);

    if (resp->cgi_type == PHPFPM)
    {
        ret += fcgi_add_param(resp, "REDIRECT_STATUS", "true", 4);
    }

    ret += fcgi_add_param(resp, "PATH", "/bin:/usr/bin:/usr/local/bin", 28);

    ret += fcgi_add_param(resp, 
            "SERVER_SOFTWARE",
            conf->ServerSoftware.c_str(), conf->ServerSoftware.size());

    ret += fcgi_add_param(resp, 
            "GATEWAY_INTERFACE",
            "CGI/1.1", 7);

    ret += fcgi_add_param(resp, 
            "DOCUMENT_ROOT",
            resp->vhost->DocumentRoot.c_str(), resp->vhost->DocumentRoot.size());

    ret += fcgi_add_param(resp,
                    "DOCUMENT_URI",
                    resp->clean_decode_path, strlen(resp->clean_decode_path));

    ret += fcgi_add_param(resp,
                    "REQUEST_URI",
                    resp->path.c_str(), resp->path.size());

    ret += fcgi_add_param(resp,
                    "REMOTE_ADDR",
                    c->remoteAddr, strlen(c->remoteAddr));

    ret += fcgi_add_param(resp,
                    "REMOTE_PORT",
                    c->remotePort, strlen(c->remotePort));

    ret += fcgi_add_param(resp,
                    "REQUEST_METHOD",
                    get_str_method(resp->httpMethod), strlen(get_str_method(resp->httpMethod)));

    if (c->Protocol == P_HTTP2)
    {
        ret += fcgi_add_param(resp,
                    "SERVER_PROTOCOL",
                    "HTTP/2.0", 8);
    }
    else
    {
        ret += fcgi_add_param(resp,
                    "SERVER_PROTOCOL",
                    "HTTP/1.1", 8);
    }

    ret += fcgi_add_param(resp,
                    "SERVER_PORT",
                    c->ServerPort.c_str(), c->ServerPort.size());

    if (resp->host.size())
    {
        ret += fcgi_add_param(resp,
                    "HTTP_HOST",
                    resp->host.c_str(), resp->host.size());
    }

    if (resp->referer.size())
    {
        ret += fcgi_add_param(resp,
                    "HTTP_REFERER",
                    resp->referer.c_str(), resp->referer.size());
    }

    if (resp->user_agent.size())
    {
        ret += fcgi_add_param(resp,
                    "HTTP_USER_AGENT",
                    resp->user_agent.c_str(), resp->user_agent.size());
    }

    ret += fcgi_add_param(resp,
                    "SCRIPT_NAME",
                    resp->clean_decode_path, strlen(resp->clean_decode_path));

    if (resp->cgi_type == PHPFPM)
    {
        resp->cgi.path = resp->vhost->DocumentRoot;
        resp->cgi.path += resp->clean_decode_path;
        ret += fcgi_add_param(resp,
                    "SCRIPT_FILENAME",
                    resp->cgi.path.c_str(), resp->cgi.path.size());
    }

    if (resp->httpMethod == M_POST)
    {
        if (resp->sReqContentType.size())
        {
            ret += fcgi_add_param(resp,
                    "CONTENT_TYPE",
                    resp->sReqContentType.c_str(), resp->sReqContentType.size());
        }

        if (resp->sReqContentLen.size())
        {
            ret += fcgi_add_param(resp,
                        "CONTENT_LENGTH", 
                        resp->sReqContentLen.c_str(), resp->sReqContentLen.size());
        }
    }

    ret += fcgi_add_param(resp,
                    "QUERY_STRING",
                    resp->query_string.c_str(), resp->query_string.size());

    if (ret)
    {
        print_err(resp, "<%s:%d> Error: create fcgi param\n", __func__, __LINE__);
        return -RS500;
    }

    resp->cgi.timer = 0;
    fcgi_set_header(&resp->send_data, FCGI_PARAMS);
    return 0;
}
//======================================================================
void EventHandlerClass::fcgi_worker(Connect* c, Stream *resp, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int events = poll_fd[cgi_ind_poll].revents;
    int fd = poll_fd[cgi_ind_poll].fd;

    if (resp->cgi_status == FASTCGI_BEGIN)
    {
        if (revents == POLLOUT)
        {
            int ret = write_to_fcgi(resp->cgi.fd, resp->send_data.ptr_remain(), resp->send_data.size_remain());
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                    set_error_message(c, resp, RS502);
                return;
            }

            resp->cgi.timer = 0;
            resp->send_data.inc_offset(ret);
            if (resp->send_data.size_remain() == 0)
            {
                resp->send_data.init();
                resp->cgi_status = FASTCGI_PARAMS;
                if (fcgi_create_params(c, resp) < 0)
                {
                    print_err(resp, "<%s:%d> Error fcgi_create_params()\n", __func__, __LINE__);
                    set_error_message(c, resp, RS502);
                }
            }
        }
        else if (revents != 0)
        {
            print_err(resp, "<%s:%d> Error 0x%02X(0x%02X), fd=%d, id=%d \n", __func__, __LINE__,
                        revents, events, fd, resp->id);
            set_error_message(c, resp, RS502);
        }
    }
    else if (resp->cgi_status == FASTCGI_PARAMS)
    {
        if (revents == POLLOUT)
        {
            if (resp->send_data.size_remain() == 0)
            {
                resp->send_data.ncpy("\0\0\0\0\0\0\0\0", 8);
                fcgi_set_header(&resp->send_data, FCGI_PARAMS);
            }

            int ret = write_to_fcgi(resp->cgi.fd, resp->send_data.ptr_remain(), resp->send_data.size_remain());
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                    set_error_message(c, resp, RS502);
                return;
            }

            resp->cgi.timer = 0;
            resp->send_data.inc_offset(ret);
            if (resp->send_data.size_remain() == 0)
            {
                if (resp->send_data.size() == 8)
                {
                    resp->cgi_status = CGI_STDIN;
                    if ((resp->post_content_len == 0) && (resp->post_data.size() == 0))
                    {
                        resp->post_data.ncpy("\0\0\0\0\0\0\0\0", 8);
                        fcgi_set_header(&resp->post_data, FCGI_STDIN);
                    }
                }

                resp->send_data.init();
            }
        }
        else
        {
            print_err(resp, "<%s:%d> FASTCGI_PARAMS Error revents=0x%02X, id=%d \n",
                            __func__, __LINE__, revents, resp->id);
            set_error_message(c, resp, RS502);
        }
    }
    else if (resp->cgi_status == CGI_STDIN)
    {
        if (revents != POLLOUT)
        {
            print_err(resp, "<%s:%d> FASTCGI_STDIN Error revents=0x%02X, id=%d \n", __func__, __LINE__,
                            revents, resp->id);
            set_error_message(c, resp, RS502);
            return;
        }

        int ret = write(fd, resp->post_data.ptr_remain(), resp->post_data.size_remain());
        if (ret <= 0)
        {
            if (errno != EAGAIN)
            {
                print_err(resp, "<%s:%d> Error write()=%d: %s; id=%d \n", __func__, __LINE__,
                            ret, strerror(errno), resp->id);
                resp->post_data.init();
                set_error_message(c, resp, RS502);
            }
            return;
        }

        resp->post_data.inc_offset(ret);
        if (resp->post_data.size_remain() == 0)
        {
            resp->cgi.timer = 0;
            resp->post_data.init();
            if (resp->post_content_len <= 0)
            {
                resp->cgi_status = CGI_STDOUT;
            }
        }
        else
        {
            print_err(resp, "<%s:%d> !!! Error write()=%d(%d), id=%d \n", __func__, __LINE__,
                            ret, resp->post_data.size_remain(), resp->id);
        }
    }
    else if (resp->cgi_status == CGI_STDOUT)
    {
        if (revents != POLLIN)
        {
            print_err(resp, "<%s:%d> FASTCGI_STDOUT Error revents=0x%02X, id=%d \n", __func__, __LINE__,
                            revents, resp->id);
            set_error_message(c, resp, RS502);
            return;
        }

        if (resp->buf.size() && resp->create_headers)
            return;

        if (resp->cgi.fcgiContentLen == 0)
        {
            if (resp->cgi.fcgiPaddingLen > 0)
            {
                char s[256];
                if (resp->cgi.fcgiPaddingLen > (int)sizeof(s))
                {
                    resp->post_data.init();
                    set_error_message(c, resp, RS502);
                    return;
                }

                int ret = read(fd, s, resp->cgi.fcgiPaddingLen);
                if (ret <= 0)
                {
                    resp->post_data.init();
                    set_error_message(c, resp, RS502);
                    return;
                }

                resp->cgi.timer = 0;
                resp->cgi.fcgiPaddingLen -= ret;
                return;
            }

            resp->buf.init();
            char s[8];
            int ret = read(fd, s, 8);
            if (ret != 8)
            {
                if ((ret == -1) && (errno == EAGAIN))
                    return;
                set_error_message(c, resp, RS502);
                return;
            }

            resp->cgi.fcgi_type = s[1];
            resp->cgi.fcgiContentLen = ((unsigned char)s[4]<<8) | (unsigned char)s[5];
            resp->cgi.fcgiPaddingLen = (unsigned char)s[6];
            if (resp->cgi.fcgiContentLen == 0)
                return;

            switch (resp->cgi.fcgi_type)
            {
                case FCGI_STDOUT:
                    break;
                case FCGI_STDERR:
                    break;
                case FCGI_END_REQUEST:
                    break;
                default:
                    print_err(resp, "<%s:%d> Error fcgi type: %d\n", __func__, __LINE__, resp->cgi.fcgi_type);
                    set_error_message(c, resp, RS502);
                    return;
            }

            //return;
        }

        char buf[16384];
        int rd = (resp->cgi.fcgiContentLen > (int)c->h2->HTTP2_SendBufSize) ? c->h2->HTTP2_SendBufSize : resp->cgi.fcgiContentLen;
        int ret = read(fd, buf, rd);
        if (ret > 0)
        {
            resp->cgi.timer = 0;
            resp->cgi.fcgiContentLen -= ret;
            switch (resp->cgi.fcgi_type)
            {
                case FCGI_STDOUT:
                    resp->buf.ncat(buf, ret);
                    if (resp->create_headers == false)
                        http2_get_cgi_headers(c, resp);
                    break;
                case FCGI_STDERR:
                    fwrite(buf, 1, ret, stderr);
                    fprintf(stderr, "\n");
                    break;
                case FCGI_END_REQUEST:
                    resp->cgi.end = true;
                    break;
            }
        }
        else
        {
            if (errno != EAGAIN)
            {
                print_err(resp, "<%s:%d> Error read() = %d\n", __func__, __LINE__, ret);
                set_error_message(c, resp, RS502);
            }
        }
    }
}
