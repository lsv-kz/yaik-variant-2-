#include "main.h"

using namespace std;
//======================================================================
void fcgi_set_header(BytesArray* ba, unsigned char type)
{
    int dataLen = ba->size() - 8;

    ba->set_byte(FCGI_VERSION_1, 0);
    ba->set_byte((unsigned char)type, 1);
    ba->set_byte((unsigned char) ((1 >> 8) & 0xff), 2);
    ba->set_byte((unsigned char) ((1) & 0xff), 3);
    ba->set_byte((unsigned char) ((dataLen >> 8) & 0xff), 4);
    ba->set_byte((unsigned char) ((dataLen) & 0xff), 5);
    ba->set_byte(0, 6);
    ba->set_byte(0, 7);
}
//======================================================================
void fcgi_set_header(BytesArray* ba, int offset, unsigned char type)
{
    int dataLen = ba->size() - 8 - offset;
    ba->set_byte(FCGI_VERSION_1, 0 + offset);
    ba->set_byte((unsigned char)type, 1 + offset);
    ba->set_byte((unsigned char) ((1 >> 8) & 0xff), 2 + offset);
    ba->set_byte((unsigned char) ((1) & 0xff), 3 + offset);
    ba->set_byte((unsigned char) ((dataLen >> 8) & 0xff), 4 + offset);
    ba->set_byte((unsigned char) ((dataLen) & 0xff), 5 + offset);
    ba->set_byte(0, 6 + offset);
    ba->set_byte(0, 7 + offset);
}
//======================================================================
void fcgi_set_header(char *s, unsigned char type, int dataLen)
{
    s[0] = (unsigned char)FCGI_VERSION_1;
    s[1] = (unsigned char)type;
    s[2] = (unsigned char)((1 >> 8) & 0xff);
    s[3] = (unsigned char)((1) & 0xff);
    s[4] = (unsigned char)((dataLen >> 8) & 0xff);
    s[5] = (unsigned char)((dataLen) & 0xff);
    s[6] = 0;
    s[7] = 0;
}
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
    resp->send_data.ncat("\0\0\0\0\0\0\0\0", 8);

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
    resp->cgi_status = FASTCGI_PARAMS;
    fcgi_set_header(&resp->send_data, 16, FCGI_PARAMS);
    resp->send_data.ncat("\x01\x04\x00\x01\x00\x00\x00\x00", 8);
    return 0;
}
//======================================================================
int fcgi_create_connect(Connect *c, Stream *resp)
{
    if ((resp->cgi_type != PHPFPM) && (resp->cgi_type != FASTCGI))
    {
        print_err(resp, "<%s:%d> ? req->scriptType=%d \n", __func__, __LINE__, resp->cgi_type);
        return -1;
    }

    if (resp->cgi_type == PHPFPM)
        resp->cgi.socket = &conf->PathPHP;

    resp->cgi.fd = create_fcgi_socket(resp->cgi.socket->c_str());
    if (resp->cgi.fd < 0)
    {
        print_err(resp, "<%s:%d> Error connect to fcgi\n", __func__, __LINE__);
        return -1;
    }

    char s[16];
    s[0] = FCGI_VERSION_1;
    s[1] = FCGI_BEGIN_REQUEST;
    s[2] = (unsigned char) ((1 >> 8) & 0xff);
    s[3] = (unsigned char) ((1) & 0xff);
    s[4] = (unsigned char) ((8 >> 8) & 0xff);
    s[5] = (unsigned char) ((8) & 0xff);
    s[6] = 0;
    s[7] = 0;

    s[8] = (unsigned char) ((FCGI_RESPONDER >> 8) & 0xff);
    s[9] = (unsigned char) (FCGI_RESPONDER        & 0xff);
    s[10] = (unsigned char) 0;
    memset(s + 11, 0, 5);
    resp->send_data.reserve(4096);
    resp->send_data.ncpy(s, 16);
    return fcgi_create_params(c, resp);
}
//======================================================================
int EventHandlerClass::fcgi_worker_(Connect* c, Stream *resp, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int fd = poll_fd[cgi_ind_poll].fd;

	if (resp->cgi_status == FASTCGI_PARAMS)
    {
        if (revents == POLLOUT)
        {
            int ret = write_to_fcgi(resp->cgi.fd, resp->send_data.ptr_remain(), resp->send_data.size_remain());
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                    return RS502;
                return 0;
            }

            resp->cgi.timer = 0;
            resp->send_data.inc_offset(ret);
            if (resp->send_data.size_remain() == 0)
            {
                resp->send_data.init();
                resp->cgi_status = CGI_STDIN;
                if ((resp->post_content_len == 0) && (resp->post_data.size() == 0))
                {
                    resp->post_data.ncpy("\0\0\0\0\0\0\0\0", 8);
                    fcgi_set_header(&resp->post_data, FCGI_STDIN);
                }
            }
        }
        else
        {
            print_err(resp, "<%s:%d> FASTCGI_PARAMS Error revents=0x%02X\n", __func__, __LINE__, revents);
            return RS502;
        }
    }
    else if (resp->cgi_status == CGI_STDIN)
    {
        if (revents != POLLOUT)
        {
            print_err(resp, "<%s:%d> FASTCGI_STDIN Error revents=0x%02X\n", __func__, __LINE__, revents);
            return RS502;
        }

        int ret = write(fd, resp->post_data.ptr_remain(), resp->post_data.size_remain());
        if (ret <= 0)
        {
            if (errno != EAGAIN)
            {
                print_err(resp, "<%s:%d> Error write()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                resp->post_data.init();
                return RS502;
            }
            return 0;
        }

        resp->post_data.inc_offset(ret);
        if (resp->post_data.size_remain() == 0)
        {
            resp->cgi.timer = 0;
            resp->post_data.init();
            if (resp->post_content_len <= 0)
            {
                resp->cgi_status = CGI_STDOUT;
                resp->cgi.fcgiContentLen = 0;
				resp->cgi.fcgiPaddingLen = 0;
                if (c->Protocol == P_HTTP1)
                {
					c->h1->con_status = SEND_RESP_HEADERS;
				}
            }
        }
        else
        {
            print_err(resp, "<%s:%d> !!! Error write()=%d(%d)\n", __func__, __LINE__,
                            ret, resp->post_data.size_remain());
        }
    }
    else if (resp->cgi_status == CGI_STDOUT)
    {
        if (revents != POLLIN)
        {
            print_err(resp, "<%s:%d> FASTCGI_STDOUT Error revents=0x%02X\n", __func__, __LINE__, revents);
            return RS502;
        }

        int ret = fcgi_stdout(c, resp, fd);
        if (ret == ERR_TRY_AGAIN)
        {
            print_err(c, "<%s:%d> cgi_stdout()=ERR_TRY_AGAIN\n", __func__, __LINE__);
        }
        else if (ret < 0)
        {
            resp->post_data.init();
            return RS502;
        }
        else
        {
            if (resp->create_headers == false)
            {
                if (c->Protocol == P_HTTP2)
                {
					http2_get_cgi_headers(c, resp);
				}
				else
				{
					http1_get_cgi_headers(c);
				}
            }
            else
            {
                if ((c->Protocol == P_HTTP1) && (ret == 1))
                {
	                if (c->h1->chunk_mode == CHUNK)
	                {
	                    resp->send_data.ncpy("01234567", 8);
	                    resp->send_data.ncat(resp->buf.ptr_remain(), resp->buf.size_remain());
	                    int ret = cgi_set_size_chunk(&resp->send_data);
	                    if (ret < 0)
	                    {
	                        print_err(c, "<%s:%d> Error cgi_set_size_chunk()\n", __func__, __LINE__);
	                        return RS502;
	                    }
	                }
	                else
	                    resp->send_data.ncpy(resp->buf.ptr_remain(), resp->buf.size_remain());
	                resp->buf.init();
				}
            }
        }
    }
   
    return 0;
}
//======================================================================
int EventHandlerClass::fcgi_stdout(Connect *c, Stream *s, int fd)
{
    if (s->cgi.fcgiContentLen == 0)
    {
        if (s->cgi.fcgiPaddingLen > 0)
        {
            char buf[256];
            int ret = read(fd, buf, s->cgi.fcgiPaddingLen);
            if (ret <= 0)
            {
                return -1;
            }

            s->cgi.timer = time(NULL);
            s->cgi.fcgiPaddingLen -= ret;
            if (s->cgi.fcgiPaddingLen > 0)
            {
                return 0;
			}
        }
        
        char buf[8];
        int ret = read(fd, buf, 8);
        if (ret != 8)
        {
            if ((ret == -1) && (errno == EAGAIN))
            {
                return 0;
			}
            return -1;
        }
        
        s->cgi.fcgi_type = buf[1];
        s->cgi.fcgiContentLen = ((unsigned char)buf[4]<<8) | (unsigned char)buf[5];
        s->cgi.fcgiPaddingLen = (unsigned char)buf[6];
        switch (s->cgi.fcgi_type)
        {
            case FCGI_STDOUT:
                if (s->cgi.fcgiContentLen == 0)
					return 0;
                if (s->create_headers && s->buf.size_remain())
                {
					print_err("<%s:%d>!!! ---- FCGI_STDOUT 0 bytes ----\n", __func__, __LINE__);
					return 0;
				}
                break;
            case FCGI_STDERR:
                break;
            case FCGI_END_REQUEST:
                break;
            default:
                print_err("<%s:%d> Error fcgi type: %d\n", __func__, __LINE__, s->cgi.fcgi_type);
                return -1;
        }
    }

	char buf[16384];
	//char buf[256];
	int num_read = s->cgi.fcgiContentLen;
	if (num_read > (int)sizeof(buf))
		num_read = sizeof(buf);

	int ret = read(fd, buf, num_read);
	if (ret > 0)
	{
		s->cgi.timer = time(NULL);
		s->cgi.fcgiContentLen -= ret;
		switch (s->cgi.fcgi_type)
		{
			case FCGI_STDOUT:
				s->buf.ncat(buf, ret);
				return 1;
				break;
			case FCGI_STDERR:
				fwrite(buf, 1, ret, stderr);
				fprintf(stderr, "\n");
				break;
			case FCGI_END_REQUEST:
				if (s->cgi.fcgiContentLen <= 0)
				{
					s->cgi.end = true;
					return 1;
				}
				break;
		}
	}
	else
		return -1;
    return 0;
}
//======================================================================
void EventHandlerClass::fcgi_worker(Connect* c, Stream *resp, int cgi_ind_poll)
{
	int err = fcgi_worker_(c, resp, cgi_ind_poll);
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
        
        
        

