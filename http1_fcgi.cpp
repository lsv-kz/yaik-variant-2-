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
    resp->send_data.ncpy(s, 16);
    resp->cgi_status = FASTCGI_BEGIN;
    return 0;
}
//======================================================================
void EventHandlerClass::fcgi_worker(Connect *c, int cgi_ind_poll)
{
    int revents = poll_fd[cgi_ind_poll].revents;
    int events = poll_fd[cgi_ind_poll].revents;
    int fd = poll_fd[cgi_ind_poll].fd;

    if (c->h1->resp.cgi_status == FASTCGI_BEGIN)
    {
        if (revents == POLLOUT)
        {
            int ret = write_to_fcgi(c->h1->resp.cgi.fd, c->h1->resp.send_data.ptr_remain(), c->h1->resp.send_data.size_remain());
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                {
                    print_err(c, "<%s:%d> Error write_to_fcgi()=%d\n", __func__, __LINE__, ret);
                    c->err = -RS502;
                    http1_end_request(c);
                }
                return;
            }

            c->h1->resp.cgi.timer = 0;
            c->h1->resp.send_data.inc_offset(ret);
            if (c->h1->resp.send_data.size_remain() == 0)
            {
                c->h1->resp.send_data.init();
                c->h1->resp.cgi_status = FASTCGI_PARAMS;
                if (fcgi_create_params(c, &c->h1->resp) < 0)
                {
                    print_err(c, "<%s:%d> Error fcgi_create_params()\n", __func__, __LINE__);
                    c->err = -RS502;
                    http1_end_request(c);
                }
            }
        }
        else if (revents != 0)
        {
            print_err(c, "<%s:%d> Error 0x%02X(0x%02X), fd=%d\n", __func__, __LINE__,
                        revents, events, fd);
            c->err = -RS502;
            http1_end_request(c);
        }
    }
    else if (c->h1->resp.cgi_status == FASTCGI_PARAMS)
    {
        if (revents == POLLOUT)
        {
            if (c->h1->resp.send_data.size_remain() == 0)
            {
                c->h1->resp.send_data.ncpy("\0\0\0\0\0\0\0\0", 8);
                fcgi_set_header(&c->h1->resp.send_data, FCGI_PARAMS);
            }

            int ret = write_to_fcgi(c->h1->resp.cgi.fd, c->h1->resp.send_data.ptr_remain(), c->h1->resp.send_data.size_remain());
            if (ret < 0)
            {
                if (ret != ERR_TRY_AGAIN)
                {
                    print_err(c, "<%s:%d> Error write_to_fcgi()=%d\n", __func__, __LINE__, ret);
                    c->err = -RS502;
                    http1_end_request(c);
                }
                return;
            }

            c->h1->resp.cgi.timer = 0;
            c->h1->resp.send_data.inc_offset(ret);
            if (c->h1->resp.send_data.size_remain() == 0)
            {
                if (c->h1->resp.send_data.size() == 8)
                {
                    c->h1->resp.cgi_status = CGI_STDIN;
                    if ((c->h1->resp.post_content_len <= 0) && (c->h1->resp.post_data.size() == 0))
                    {
                        c->h1->resp.post_data.ncpy("\0\0\0\0\0\0\0\0", 8);
                        fcgi_set_header(&c->h1->resp.post_data, FCGI_STDIN);
                    }
                }

                c->h1->resp.send_data.init();
            }
        }
        else
        {
            print_err(c, "<%s:%d> FASTCGI_PARAMS Error revents=0x%02X\n", __func__, __LINE__, revents);
            c->err = -RS502;
            http1_end_request(c);
        }
    }
    else if (c->h1->resp.cgi_status == CGI_STDIN)
    {
        if (revents != POLLOUT)
        {
            print_err(c, "<%s:%d> FASTCGI_STDIN Error revents=0x%02X\n", __func__, __LINE__, revents);
            c->err = -RS502;
            http1_end_request(c);
            return;
        }

        int ret = write(fd, c->h1->resp.post_data.ptr_remain(), c->h1->resp.post_data.size_remain());
        if (ret <= 0)
        {
            if (errno != EAGAIN)
            {
                print_err(c, "<%s:%d> Error write()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                c->h1->resp.post_data.init();
                c->err = -RS502;
                http1_end_request(c);
            }
            return;
        }

        if (ret != (int)c->h1->resp.post_data.size_remain())
        {
            print_err(c, "<%s:%d> !!! Error write()=%d(%d)\n", __func__, __LINE__, ret, c->h1->resp.post_data.size_remain());
        }

        c->h1->resp.post_data.inc_offset(ret);
        c->h1->resp.cgi.timer = 0;
        if (c->h1->resp.post_data.size_remain() == 0)
        {
            c->h1->resp.post_data.init();
            if (c->h1->resp.post_content_len <= 0)
            {
                c->h1->resp.cgi_status = CGI_STDOUT;
                c->h1->con_status = SEND_RESP_HEADERS;
                c->h1->resp.cgi.fcgiContentLen = 0;
                c->h1->resp.cgi.fcgiPaddingLen = 0;
            }
        }
    }
    else if (c->h1->resp.cgi_status == CGI_STDOUT)
    {
        if (revents != POLLIN)
        {
            print_err(c, "<%s:%d> FASTCGI_STDOUT Error revents=0x%02X\n", __func__, __LINE__, revents);
            c->err = -RS502;
            http1_end_request(c);
            return;
        }

        if (c->h1->resp.cgi.fcgiContentLen == 0)
        {
            if (c->h1->resp.cgi.fcgiPaddingLen > 0)
            {
                char s[256];
                if (c->h1->resp.cgi.fcgiPaddingLen > (int)sizeof(s))
                {
                    print_err(c, "<%s:%d> Error fcgiPaddingLen=%d\n", __func__, __LINE__, c->h1->resp.cgi.fcgiPaddingLen);
                    c->h1->resp.post_data.init();
                    c->err = -RS502;
                    http1_end_request(c);
                    return;
                }

                int ret = read(fd, s, c->h1->resp.cgi.fcgiPaddingLen);
                if (ret <= 0)
                {
                    print_err(c, "<%s:%d> Error read()=%d\n", __func__, __LINE__, ret);
                    c->h1->resp.post_data.init();
                    c->err = -RS502;
                    http1_end_request(c);
                    return;
                }

                c->h1->resp.cgi.fcgiPaddingLen -= ret;
                return;
            }

            c->h1->resp.send_data.init();
            char s[8];
            int ret = read(fd, s, 8);
            if (ret != 8)
            {
                if ((ret == -1) && (errno == EAGAIN))
                    return;
                print_err(c, "<%s:%d> Error read fcgi header: %d\n", __func__, __LINE__, ret);
                c->err = -RS502;
                http1_end_request(c);
                return;
            }

            c->h1->resp.cgi.fcgi_type = s[1];
            c->h1->resp.cgi.fcgiContentLen = ((unsigned char)s[4]<<8) | (unsigned char)s[5];
            c->h1->resp.cgi.fcgiPaddingLen = (unsigned char)s[6];
            if ((c->h1->resp.cgi.fcgiContentLen > 65535) || (c->h1->resp.cgi.fcgiPaddingLen > 255))
            {
                print_err(c, "<%s:%d> Error fcgiContentLen=%d, fcgiPaddingLen=%d\n", __func__, __LINE__,
                            c->h1->resp.cgi.fcgiContentLen, c->h1->resp.cgi.fcgiPaddingLen);
                hex_print_stderr(__func__, __LINE__, s, 8);
                c->err = -1;
                http1_end_request(c);
                return;
            }
            else if (c->h1->resp.cgi.fcgiContentLen == 0)
                return;

            switch (s[1])
            {
                case FCGI_STDOUT:
                    break;
                case FCGI_STDERR:
                    break;
                case FCGI_END_REQUEST:
                    if (c->h1->chunk_mode == CHUNK)
                    {
                        char s[] = "0\r\n\r\n";
                        c->h1->resp.send_data.ncpy(s, 5);
                    }

                    break;
                default:
                    print_err(c, "<%s:%d> Error fcgi type %d\n", __func__, __LINE__, (int)s[1]);
                    hex_print_stderr(__func__, __LINE__, s, 8);
                    c->err = -RS502;
                    http1_end_request(c);
                    return;
            }

            //return;
        }

        char buf[65535]; // 16384
        int rd = (c->h1->resp.cgi.fcgiContentLen > (int)sizeof(buf)) ? sizeof(buf) : c->h1->resp.cgi.fcgiContentLen;
        int ret = read(fd, buf, rd);
        if (ret > 0)
        {
            c->h1->resp.cgi.fcgiContentLen -= ret;
            switch (c->h1->resp.cgi.fcgi_type)
            {
                case FCGI_STDOUT:
                    if (c->h1->resp.create_headers == false)
                    {
                        c->h1->resp.buf.ncat(buf, ret);
                        http1_get_cgi_headers(c);
                    }
                    else
                    {
                        if (c->h1->chunk_mode == CHUNK)
                        {
                            c->h1->resp.send_data.ncpy("01234567", 8);
                            c->h1->resp.send_data.ncat(buf, ret);
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
                        {
                            c->h1->resp.send_data.ncat(buf, ret);
                        }
                    }
                    break;
                case FCGI_STDERR:
                    fwrite(buf, 1, ret, stderr);
                    fprintf(stderr, "\n");
                    break;
                case FCGI_END_REQUEST:
                    {
                        c->h1->resp.cgi.end = true;
                        if (c->h1->resp.send_data.size_remain() == 0)
                        {
                            c->h1->resp.send_data.init();
                            http1_end_request(c);
                        }
                    }
                    break;
            }
        }
        else
        {
            if (errno != EAGAIN)
            {
                print_err(c, "<%s:%d> Error read()=%d: %s\n", __func__, __LINE__, ret, strerror(errno));
                c->err = -RS502;
                http1_end_request(c);
            }
        }
    }
}
