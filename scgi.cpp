#include "main.h"

using namespace std;
//======================================================================
static int scgi_set_size_data(BytesArray *ba)
{
    ba->set_byte(':', 7);
    int i = 6;
    int size = ba->size() - 8;
    for ( ; i >= 0; --i)
    {
        ba->set_byte((size % 10) + '0', i);
        size /= 10;
        if (size == 0)
            break;
    }

    if (size != 0)
        return -1;

    ba->ncat(",", 1);
    ba->inc_offset(i);

    return 0;
}
//======================================================================
static int scgi_add_param(Stream *resp, const char *name, const char *val, int len_val)
{
    if (name == NULL)
    {
        print_err("[%d/%d]<%s:%d> Error: name=NULL\n", resp->numConn, resp->numReq, __func__, __LINE__);
        return -1;
    }

    int len_name = strlen(name);

    if (val == NULL)
        len_val = 0;

    int len = len_name + len_val + 2;
    if ((len + resp->send_data.size()) > 16000)
    {
        print_err("[%d/%d]<%s:%d> Error: name=NULL\n", resp->numConn, resp->numReq, __func__, __LINE__);
        return -1;
    }

    resp->send_data.ncat(name, len_name);
    resp->send_data.ncat("\0", 1);

    if (len_val > 0)
    {
        resp->send_data.ncat(val, len_val);
    }

    resp->send_data.ncat("\0", 1);

    return 0;
}
//======================================================================
static int scgi_create_params(Connect *c, Stream *resp)
{
    int ret = 0;
    resp->send_data.ncpy("\0\0\0\0\0\0\0\0", 8);
    resp->send_data.reserve(768);

    if (resp->httpMethod == M_POST)
    {
        if (resp->sReqContentLen.size())
        {
            ret += scgi_add_param(resp,
                    "CONTENT_LENGTH", 
                    resp->sReqContentLen.c_str(), resp->sReqContentLen.size());
        }
        else
            ret += scgi_add_param(resp,
                    "CONTENT_LENGTH", 
                    "0", 1);

        ret += scgi_add_param(resp,
                    "CONTENT_TYPE",
                    resp->sReqContentType.c_str(), resp->sReqContentType.size());
    }
    else
    {
        ret += scgi_add_param(resp,
                    "CONTENT_LENGTH",
                    "0", 1);

        ret += scgi_add_param(resp,
                    "CONTENT_TYPE",
                    NULL, 0);
    }

    ret += scgi_add_param(resp,
                    "PATH",
                    "/bin:/usr/bin:/usr/local/bin", 28);

    ret += scgi_add_param(resp,
                    "SERVER_SOFTWARE",
                    conf->ServerSoftware.c_str(), conf->ServerSoftware.size());

    ret += scgi_add_param(resp,
                    "SCGI",
                    "1", 1);

    ret += scgi_add_param(resp,
                    "DOCUMENT_ROOT",
                    resp->vhost->DocumentRoot.c_str(), resp->vhost->DocumentRoot.size());

    ret += scgi_add_param(resp,
                    "DOCUMENT_URI",
                    resp->clean_decode_path, strlen(resp->clean_decode_path));

    ret += scgi_add_param(resp,
                    "REQUEST_URI",
                    resp->path.c_str(), resp->path.size());

    ret += scgi_add_param(resp,
                    "REMOTE_ADDR",
                    c->remoteAddr, strlen(c->remoteAddr));

    ret += scgi_add_param(resp,
                    "REMOTE_PORT",
                    c->remotePort, strlen(c->remotePort));

    ret += scgi_add_param(resp,
                    "REQUEST_METHOD",
                    get_str_method(resp->httpMethod), strlen(get_str_method(resp->httpMethod)));

    if (c->Protocol == P_HTTP2)
    {
        ret += scgi_add_param(resp,
                    "SERVER_PROTOCOL",
                    "HTTP/2.0", 8);
    }
    else
    {
        ret += scgi_add_param(resp,
                    "SERVER_PROTOCOL",
                    "HTTP/1.1", 8);
    }

    ret += scgi_add_param(resp,
                    "SERVER_PORT",
                    c->ServerPort.c_str(), c->ServerPort.size());

    if (resp->host.size())
    {
        ret += scgi_add_param(resp,
                    "HTTP_HOST",
                    resp->host.c_str(), resp->host.size());
    }

    if (resp->referer.size())
    {
        ret += scgi_add_param(resp,
                    "HTTP_REFERER",
                    resp->referer.c_str(), resp->referer.size());
    }

    if (resp->user_agent.size())
    {
        ret += scgi_add_param(resp,
                    "HTTP_USER_AGENT",
                    resp->user_agent.c_str(), resp->user_agent.size());
    }

    ret += scgi_add_param(resp,
                    "SCRIPT_NAME",
                    resp->clean_decode_path, strlen(resp->clean_decode_path));

    if (resp->query_string.size())
    {
        ret += scgi_add_param(resp,
                    "QUERY_STRING",
                    resp->query_string.c_str(), resp->query_string.size());
    }
    else
    {
        ret += scgi_add_param(resp,
                    "QUERY_STRING",
                    NULL, 0);
    }

    if (ret)
    {
        print_err(c, "[%d]<%s:%d> Error scgi_set_param()\n", resp->numReq, __func__, __LINE__);
        return -RS502;
    }

    if (scgi_set_size_data(&resp->send_data) < 0)
    {
        print_err(resp, "<%s:%d> Error scgi_set_size_data()\n", __func__, __LINE__);
        return -RS502;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::scgi_worker(Connect* c, Stream *resp, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int events = poll_fd[cgi_ind_poll].revents;
    if (resp->cgi_status == SCGI_PARAMS)
    {
        if (revents == POLLOUT)
        {
            int ret = write_to_fcgi(resp->cgi.fd, resp->send_data.ptr_remain(), resp->send_data.size_remain());
            if (ret < 0)
            {
                if (ret == ERR_TRY_AGAIN)
                    return 0;
                else
                    return -RS502;
            }

            resp->cgi.timer = 0;
            resp->send_data.inc_offset(ret);
            if (resp->send_data.size_remain() == 0)
            {
                resp->send_data.init();
                if (resp->httpMethod == M_POST)
                {
                    if ((resp->post_content_len <= 0) && (resp->post_data.size() == 0))
                        resp->cgi_status = CGI_STDOUT;
                    else
                        resp->cgi_status = CGI_STDIN;
                }
                else
                {
                    resp->post_data.init();
                    resp->cgi_status = CGI_STDOUT;
                }
            }
        }
        else
        {
            print_err(resp, "<%s:%d> Error 0x%02X(0x%02X)\n", __func__, __LINE__, revents, events);
            return -RS502;
        }
    }

    return 0;
}
//======================================================================
int scgi_create_connect(Connect *c, Stream *resp)
{
    resp->cgi.fd = create_fcgi_socket(resp->cgi.socket->c_str());
    if (resp->cgi.fd < 0)
    {
        print_err(resp, "<%s:%d> Error connect to scgi\n", __func__, __LINE__);
        return resp->cgi.fd;
    }

    int ret = scgi_create_params(c, resp);
    if (ret < 0)
        return ret;
    else
    {
        resp->cgi.timer = 0;
        c->client_timer = 0;
        int opt = 1;
        ioctl(resp->cgi.fd, FIONBIO, &opt);
    }

    resp->cgi_status = SCGI_PARAMS;
    return 0;
}
