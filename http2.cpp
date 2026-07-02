#include "main.h"

using namespace std;

const bool huff_coding = true;
const int hpack_mask = 0;
//======================================================================
void set_frame_headers(Stream *resp)
{
    int id = resp->id;
    char s[] = "\0\0\0\1\4\0\0\0\0";
    s[8] = id;
    s[7] = id>>=8;
    s[6] = id>>=8;
    s[5] = (id>>8) & 0x7f;

    resp->headers.ncpy(s, 9);
    if (resp->numReq == 1)
        resp->headers.bytecat(0x20);
}
//======================================================================
void set_frame_flags(BytesArray *ba, int flags)
{
    char fl = ba->get_byte(4);
    ba->set_byte(fl | flags, 4);
}
//======================================================================
void add_header(Stream *resp, int ind)
{
    resp->headers.bytecat(ind | 0x80);
}
//======================================================================
void add_header(BytesArray& ba, int ind, const char *val)
{
    int mask = hpack_mask;
    bool huffman = huff_coding;
    int len = (int)strlen(val);
    int prefix_len;
    switch (mask)
    {
        case 0x40:
            prefix_len = 6;
            break;
        case 0x10:
            prefix_len = 4;
            break;
        case 0x00:
            prefix_len = 4;
            break;
        default:
            prefix_len = 6;
            mask = 0x40;
    }

    int_to_bytes(ba, ind, prefix_len, mask);

    if (huffman)
    {
        BytesArray buf;
        buf.reserve(len);
        huffman_encode(val, buf);
        int_to_bytes(ba, buf.size(), 7, 0x80);
        ba.ncat(buf.ptr(), buf.size());
    }
    else
    {
        int_to_bytes(ba, len, 7, 0);
        ba.ncat(val, len);
    }
}
//======================================================================
void add_header(BytesArray& ba, const char *name, const char *val)
{
    bool huffman = huff_coding;
    ba.ncat("\x00", 1);
    int len = (int)strlen(name);
    if (huffman)
    {
        BytesArray buf;
        buf.reserve(len);
        huffman_encode(name, buf);
        int_to_bytes(ba, buf.size(), 7, 0x80);
        ba.ncat(buf.ptr(), buf.size());
    }
    else
    {
        int_to_bytes(ba, len, 7, 0);
        ba.ncat(name, len);
    }

    len = (int)strlen(val);
    if (huffman)
    {
        BytesArray buf;
        buf.reserve(len);
        huffman_encode(val, buf);
        int_to_bytes(ba, buf.size(), 7, 0x80);
        ba.ncat(buf.ptr(), buf.size());
    }
    else
    {
        int_to_bytes(ba, len, 7, 0);
        ba.ncat(val, len);
    }
}
//======================================================================
void add_cgi_headers(Stream *resp)
{
    resp->headers.ncat(resp->cgi_headers.ptr(), resp->cgi_headers.size());
}
//======================================================================
static void set_frame_window_update(Stream *resp, int len)// post data
{
    int id = resp->id;
    char s[] = "\x00\x00\x04\x08\x00\x00\x00\x00\x00"  // 0-8
               "\x00\x00\x00\x00";                     // 9-12
    s[8] = id;
    s[7] = id>>=8;
    s[6] = id>>=8;
    s[5] = (id>>8) & 0x7f;

    s[12] = len;
    s[11] = len>>=8;
    s[10] = len>>=8;
    s[9] = (len>>8) & 0x7f;

    resp->frame_win_update.ncpy(s, 13);
}
//======================================================================
static void set_frame_window_update(Connect *c, int len)// post data
{
    char s[] = "\x00\x00\x04\x08\x00\x00\x00\x00\x00"  // 0-8
               "\x00\x00\x00\x00";                     // 9-12
    s[12] = len;
    s[11] = len>>=8;
    s[10] = len>>=8;
    s[9] = (len>>8) & 0x7f;

    c->h2->frame_win_update.ncpy(s, 13);
}
//======================================================================
static void set_frame_goaway(Connect *c, HTTP2_ERRORS error)
{
    char s[] = "\x0\x0\x8\x7\x0\x0\x0\x0\x0"
               "\x0\x0\x0\x0\x0\x0\x0\x0";
    s[13] = (error>>24) & 0x7f;
    s[14] = error>>16;
    s[15] = error>>8;
    s[16] = error;

    c->h2->goaway.ncpy(s, 17);
}
//======================================================================
void set_rst_stream(Connect *c, Stream *resp, HTTP2_ERRORS error)
{
    resp->send_rst_stream = true;
    char s[] = "\0\0\4\3\0\0\0\0\0"
                "\0\0\0\0";
    int id = resp->id;
    s[8] = id;
    s[7] = id>>=8;
    s[6] = id>>=8;
    s[5] = (id>>8) & 0x7f;

    s[9] = (error>>24) & 0x7f;
    s[10] = error>>16;
    s[11] = error>>8;
    s[12] = error;
    
    resp->rst_stream.ncpy(s, 13);
}
//======================================================================
void set_frame_data(Stream *resp, int len, int flag)
{
    int id = resp->id;
    char s[] = "\0\0\0\0\0\0\0\0\0";
    s[2] = len;
    s[1] = len>>=8;
    s[0] = len>>8;
    s[4] = flag;
    s[8] = id;
    s[7] = id>>=8;
    s[6] = id>>=8;
    s[5] = (id>>8) & 0x7f;
    resp->send_data.ncpy(s, 9);
}
//======================================================================
static int set_frame_data(Connect *c, Stream *resp)
{
    resp->send_data.init();
    long data_len = 0;
    long min_window_size = (c->h2->connect_window_size > resp->stream_window_size) ? resp->stream_window_size : c->h2->connect_window_size;
    if (min_window_size <= 0)
    {
        print_err(resp, "<%s:%d> !!! connect_window_size=%ld, stream_window_size=%ld, id=%d \n", __func__, __LINE__,
                    c->h2->connect_window_size, resp->stream_window_size, resp->id);
        return 0;
    }

    if (resp->source_data == DYN_PAGE)
    {
        if (resp->send_headers == false)
            return 0;
        if (resp->httpMethod == M_HEAD)
        {
            resp->buf.init();
            resp->cgi.end = true;
            set_frame_data(resp, 0, FLAG_END_STREAM);
            return 0;
        }

        if (resp->buf.size_remain())
        {
            unsigned int len = resp->buf.size_remain();
            if (len > c->h2->HTTP2_SendBufSize)
                len = c->h2->HTTP2_SendBufSize;
            if (len > min_window_size)
                len = min_window_size;

            set_frame_data(resp, len, 0);
            resp->send_data.ncat(resp->buf.ptr_remain(), len);
            resp->buf.inc_offset(len);
            if (resp->buf.size_remain() == 0)
                resp->buf.init();
        }
        else
        {
            if (resp->cgi.end)
                set_frame_data(resp, 0, FLAG_END_STREAM);
            else
                return 0;
        }
    }
    else
    {
        if (resp->source_data == FROM_FILE)
        {
            if (resp->resp_content_len > c->h2->HTTP2_SendBufSize)
                data_len = c->h2->HTTP2_SendBufSize;
            else
                data_len = (int)resp->resp_content_len;

            char buf[16384];
            if (data_len > 0)
            {
                if (data_len > min_window_size)
                {
                    //print_err(resp, "<%s:%d> !!! data_len(%ld) > min_window_size(%ld), id=%d \n",
                    //            __func__, __LINE__, data_len, min_window_size, resp->id);
                    data_len = min_window_size;
                }

                int ret = read(resp->fd, buf, data_len);
                if (ret <= 0)
                {
                    print_err(resp, "<%s:%d> Error read()=%d: %s, id=%d \n", __func__, __LINE__,
                                ret, strerror(errno), resp->id);
                    close(resp->fd);
                    resp->fd = -1;
                    return -1;
                }

                data_len = ret;
            }

            resp->resp_content_len -= data_len;
            int flag = (resp->resp_content_len > 0) ? 0 : FLAG_END_STREAM;
            set_frame_data(resp, data_len, flag);
            if (data_len > 0)
                resp->send_data.ncat(buf, data_len);
            if (resp->resp_content_len == 0)
            {
                close(resp->fd);
                resp->fd = -1;
            }
        }
        else if (resp->source_data == DIRECTORY)
        {
            if (resp->resp_content_len > c->h2->HTTP2_SendBufSize)
                data_len = c->h2->HTTP2_SendBufSize;
            else
                data_len = (int)resp->resp_content_len;

            if (data_len > min_window_size)
                data_len = min_window_size;

            if ((resp->buf.get_offset() + data_len) > resp->buf.size())
            {
                print_err(resp, "<%s:%d> Error\n", __func__, __LINE__);
                return -1;
            }

            resp->resp_content_len -= data_len;
            int flag = (resp->resp_content_len > 0) ? 0 : FLAG_END_STREAM;
            set_frame_data(resp, data_len, flag);
            resp->send_data.ncat(resp->buf.ptr_remain(), data_len);
            resp->buf.inc_offset(data_len);
        }
    }

    return 1;
}
//======================================================================
static int set_response(Connect *c, Stream *resp)
{
    if (resp->host.size() == 0)
    {
        print_err(c, "<%s:%d> size Host: %d\n", __func__, __LINE__, resp->host.size());
        set_error_message(c, resp, RS400);
        return 0;
    }

    resp->vhost = NULL;
    VHost *h = c->serv->vhosts;
    for ( ; h; h = h->next)
    {
        if (!strncmp(h->hostname.c_str(), resp->host.c_str(), h->hostname.size()))
        {
            resp->vhost = h;
            break;
        }
    }

    if (resp->vhost == NULL)
        resp->vhost = c->serv->vhosts;

    resp->send_bytes = 0;
    int path_len = 0;
    resp->decode_query_string = "";
    const char *p = strchr(resp->path.c_str(), '?');
    if (p)
    {
        resp->query_string = p + 1;
        path_len = p - resp->path.c_str();
    }
    else
    {
        resp->query_string = "";
        path_len = resp->path.size();
    }

    decode(resp->path.c_str(), path_len, resp->decode_path);
    int len = resp->decode_path.size();
    if (len >= resp->clean_decode_path_size)
    {
        if (resp->clean_decode_path)
        {
            delete [] resp->clean_decode_path;
            resp->clean_decode_path = NULL;
            resp->clean_decode_path_size = 0;
        }

        resp->clean_decode_path = new(nothrow) char [len + 1];
        if (resp->clean_decode_path == NULL)
        {
            print_err(resp, "<%s:%d> Error new char [%d]: %s\n", __func__, __LINE__, len + 1, strerror(errno));
            set_error_message(c, resp, RS500);
            return 0;
        }

        resp->clean_decode_path_size = len + 1;
    }

    memcpy(resp->clean_decode_path, resp->decode_path.c_str(), len);
    resp->clean_decode_path[len] = 0;

    if (resp->query_string.size())
    {
        decode(resp->query_string.c_str(), resp->query_string.size(), resp->decode_query_string);
    }

    int err = clean_path(resp->clean_decode_path, len);
    if (err <= 0)
    {
        print_err(resp, "<%s:%d> Error: clean_path[%d], id=%d \n", __func__, __LINE__, err, resp->id);
        set_error_message(c, resp, RS400);
        return 0;
    }

    if (resp->httpMethod == M_POST)
    {
        if (resp->sReqContentType.size() == 0)
        {
            print_err(resp, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            set_error_message(c, resp, RS400);
            return 0;
        }

        if (resp->sReqContentLen.size() == 0)
        {
            print_err(resp, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            set_error_message(c, resp, RS411);
            return 0;
        }

        if (resp->post_content_len >= conf->ClientMaxBodySize)
        {
            print_err(resp, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, resp->post_content_len);
            set_error_message(c, resp, RS413);
            return 0;
        }
    }
    //-------------------------------
    string path;
    path.reserve(resp->vhost->DocumentRoot.size() + resp->clean_decode_path_size + 257);
    path = resp->vhost->DocumentRoot;

    if (!strncmp(resp->clean_decode_path, "/cgi-bin/", 9) || !strncmp(resp->clean_decode_path, "/cgi/", 5))
    {
        resp->source_data = DYN_PAGE;
        resp->cgi_type = CGI;
    }
    else if (strstr(resp->clean_decode_path, ".php"))
    {
        resp->source_data = DYN_PAGE;
        if (conf->UsePHP == "php-cgi")
            resp->cgi_type = PHPCGI;
        else if (conf->UsePHP == "php-fpm")
            resp->cgi_type = PHPFPM;
        else
        {
            set_error_message(c, resp, RS404);
            return 0;
        }
    }
    else
    {
        path += resp->clean_decode_path;
        resp->source_data = get_content_type(path.c_str());
    }

    if (resp->source_data == FROM_FILE)
    {
        resp->file_size = (long long)file_size(path.c_str());
        if (resp->file_size < 0)
        {
            print_err(resp, "<%s:%d> Error file_size(%s)\n", __func__, __LINE__, path.c_str());
            set_error_message(c, resp, RS500);
            return 0;
        }

        if (resp->range.size())
        {
            int ret = parse_range(resp->range.c_str(), resp->file_size, &resp->offset, &resp->resp_content_len);
            if (ret < 0)
            {
                print_err(resp, "<%s:%d> Error parse_range(%s)\n", __func__, __LINE__, resp->range.c_str());
                set_error_message(c, resp, RS400);
                return 0;
            }
            else if (ret == 0)
            {
                resp->resp_content_len = resp->file_size;
                resp->resp_status = RS200;
            }
            else
                resp->resp_status = RS206;
        }
        else
        {
            resp->resp_content_len = resp->file_size;
            resp->resp_status = RS200;
        }
        //----------- frame headers ----------------
        set_frame_headers(resp);
        if (resp->resp_status == RS206)
            add_header(resp, 10);                                       // "206 Partial Content"
        else
            add_header(resp, 8);                                        // "200 OK"
        add_header(resp->headers, 54, conf->ServerSoftware.c_str());    // "server"
        add_header(resp->headers, 33, get_time().c_str());              // "date"

        resp->resp_content_type = content_type(resp->path.c_str());
        if (resp->resp_content_type)
            add_header(resp->headers, 31, resp->resp_content_type);     // "content-type"

        char s[128];
        snprintf(s, sizeof(s), "%lld", resp->resp_content_len);
        add_header(resp->headers, 28, s);                               // "content-length"
        add_header(resp->headers, 18, "bytes");                         // "accept-ranges"
        add_header(resp->headers, 24, "no-cache, no-store, must-revalidate");// "cache-control"

        if ((resp->file_size == 0) || (resp->httpMethod == M_HEAD))
        {
            char flag = resp->headers.get_byte(4);
            resp->headers.set_byte(flag | FLAG_END_STREAM, 4);
            resp->create_headers = true;
            return 0;
        }

        if (resp->resp_status == RS206)
        {
            char s[128];
            snprintf(s, sizeof(s), "bytes %lld-%lld/%lld", resp->offset, resp->offset + resp->resp_content_len - 1, resp->file_size);
            resp->file_size = resp->resp_content_len;
            add_header(resp->headers, 30, s);                           // "content-range"
        }

        resp->create_headers = true;
        resp->fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (resp->fd == -1)
        {
            print_err(resp, "<%s:%d> Error open(%s): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
            if (errno == EACCES)
                set_error_message(c, resp, RS403);
            else if (errno == ENOENT)
                set_error_message(c, resp, RS404);
            else
                set_error_message(c, resp, RS500);
            return 0;
        }

        if (resp->offset > 0)
            lseek(resp->fd, resp->offset, SEEK_SET);
    }
    else if (resp->source_data == DIRECTORY)
    {
        if (resp->clean_decode_path[strlen(resp->clean_decode_path) - 1] != '/')
        {
            resp->path.insert(path_len, "/");
            set_frame_headers(resp);
            add_header(resp->headers, 8, "301");                        // "301 Moved Permanently"
            add_header(resp->headers, 54, conf->ServerSoftware.c_str());// "server"
            add_header(resp->headers, 33, get_time().c_str());          // "date"
            add_header(resp->headers, 46, resp->path.c_str());          // "location"
            add_header(resp->headers, 31, "text/plain");                // "content-type"
            resp->create_headers = true;

            const char *msg = "301 Moved Permanently\n";
            int len = strlen(msg);
            char s[32];
            snprintf(s, sizeof(s), "%d", (int)(len + resp->path.size()));
            add_header(resp->headers, 28, s);                           // "content-length"
            resp->send_data.reserve(9 + len + resp->path.size());
            set_frame_data(resp, len + resp->path.size(), FLAG_END_STREAM);
            resp->send_data.ncat(msg, len);
            resp->send_data.ncat(resp->path.c_str(), resp->path.size());
            return 0;
        }

        int err = index_dir(c, path.c_str(), resp->clean_decode_path, &resp->buf);
        if (err)
        {
            print_err(resp, "<%s:%d> Error index_dir(): %d\n", __func__, __LINE__, err);
            set_error_message(c, resp, RS500);
            return 0;
        }

        resp->resp_status = RS200;
        resp->resp_content_len = resp->buf.size();
        if (resp->httpMethod == M_HEAD)
            resp->buf.init();
        //------------- headers frame --------------
        set_frame_headers(resp);
        add_header(resp, 8);                                            // "200 OK"
        add_header(resp->headers, 54, conf->ServerSoftware.c_str());    // "server"
        add_header(resp->headers, 33, get_time().c_str());              // "date"
        add_header(resp->headers, 31, "text/html;charset=UTF-8");       // "content-type"
        add_header(resp->headers, 24, "no-cache, no-store, must-revalidate");// "cache-control"
        resp->create_headers = true;

        if (resp->httpMethod == M_HEAD)
        {
            char flag = resp->headers.get_byte(4);
            resp->headers.set_byte(flag | FLAG_END_STREAM, 4);
        }
    }
    else if (resp->source_data == DYN_PAGE)
    {
        resp->send_data.init();
        resp->cgi_status = CGI_CREATE;
        resp->resp_status = RS200;
    }
    else
    {
        if (is_cgi(resp) < 0)
        {
            print_err(resp, "<%s:%d> Error: CONTENT_TYPE %s, create_headers=%d, send_headers=%d\n",
                        __func__, __LINE__, path.c_str(), resp->create_headers, resp->send_headers);
            set_error_message(c, resp, RS404);
        }
        else
        {
            resp->send_data.init();
            resp->cgi_status = CGI_CREATE;
        }
    }

    return 0;
}
//======================================================================
int set_send_again(Connect *c, Stream *stream, HTTP2_FRAME_TYPE type, int id)
{
    if (c->h2->try_again == false)
    {
        //print_err("[%lu/%d] %s, ERR_TRY_AGAIN, id=%d \n", c->numConn, id, get_str_frame_type(type), id);
        c->h2->try_again = true;
        c->h2->send_again.stream = stream;
        c->h2->send_again.id = id;
        c->h2->send_again.type = type;
    }
    else
    {
        if ((c->h2->send_again.id == id) &&
            (c->h2->send_again.type == type) &&
            (c->h2->send_again.stream == stream)
        )
        {
            return ERR_TRY_AGAIN;
        }

        print_err("[%lu/%d] Error frame_try_again != NULL, %s, id=%d \n", c->numConn, id, get_str_frame_type(type), id);
        return -1;
    }

    return ERR_TRY_AGAIN;
}
//======================================================================
int EventHandlerClass::http2_connection(Connect *c)
{
    if (c->h2->con_status == PREFACE_MESSAGE)
    {
        char buf[25];

        int ret = read_from_client(c, buf, sizeof(buf) - 1);
        if (ret == 24)
        {
            buf[ret] = 0;
            if (memcmp(buf, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24))
            {
                print_err(c, "<%s:%d> Error ---PREFACE_MESSAGE--- %s\n", __func__, __LINE__, buf);
                ssl_shutdown(c);
                return -1;
            }

            if (conf->PrintDebugMsg)
                hex_print_stderr(__func__, __LINE__, buf, 24);
            c->client_timer = 0;
            c->h2->con_status = SET_SETTINGS;
            c->h2->init();
            c->tls.poll_events = POLLOUT;
        }
        else
        {
            if (ret == ERR_TRY_AGAIN)
            {
                return 0;
            }

            print_err(c, "<%s:%d> Error read_from_client()=%d\n", __func__, __LINE__, ret);
            ssl_shutdown(c);
            return -1;
        }
        return 0;
    }
    else if (c->h2->con_status == HTTP2_SHUTDOWN)
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
                print_err(c, "<%s:%d> SSL_SHUTDOWN: SSL_read() - %s\n", __func__, __LINE__,
                            ssl_strerror(c->tls.err));
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
        print_err(c, "<%s:%d> !!! Error: type operation (%s)\n", __func__, __LINE__,
                    c->h2->get_str_status());
        ssl_shutdown(c);
        return -1;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::recv_frame(Connect *c)
{
    int ret = recv_frame_(c);
    if (ret <= 0)
    {
        if (ret == ERR_TRY_AGAIN)
            return 0;
        if (ret == 0)
            close_connect(c);
        else
            ssl_shutdown(c);
        return -1;
    }

    ret = parse_frame(c);
    if (ret < 0)
    {
        ssl_shutdown(c);
        return -1;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::recv_frame_(Connect *c)
{
    if (c->h2->header_len < 9)
    {
        if (c->h2->header_len == 0)
            c->h2->init();
        int ret = read_from_client(c, c->h2->header + c->h2->header_len, 9 - c->h2->header_len);
        if (ret <= 0)
        {
            print_err(c, "<%s:%d> Error read_from_client()=%d\n", __func__, __LINE__, ret);
            return ret;
        }

        c->h2->header_len += ret;
        if (c->h2->header_len == 9)
        {
            c->h2->body_len = ((unsigned char)c->h2->header[0]<<16) +
                ((unsigned char)c->h2->header[1]<<8) + (unsigned char)c->h2->header[2];
            c->h2->type = (HTTP2_FRAME_TYPE)c->h2->header[3];
            c->h2->flags = c->h2->header[4];
            c->h2->id = (((unsigned char)c->h2->header[5] & 0x7f)<<16) + ((unsigned char)c->h2->header[6]<<16) +
                ((unsigned char)c->h2->header[7]<<8) + (unsigned char)c->h2->header[8];
            if (conf->PrintDebugMsg)
                hex_print_stderr(__func__, __LINE__, c->h2->header, 9);
            if (c->h2->body_len > conf->HTTP2_RecvBufSize)
            {
                print_err(c, "<%s:%d> Error frame size: %d\n", __func__, __LINE__, c->h2->body_len + 9);
                return -1;
            }
        }
        else
        {
            print_err(c, "<%s:%d> Error read frame header (%s)\n", __func__, __LINE__, c->h2->get_str_status());
            return -1;
        }
    }

    if (c->h2->body_len > 0)
    {
        char buf[16384];
        unsigned int len_rd = sizeof(buf);
        if (c->h2->body_len < len_rd)
            len_rd = c->h2->body_len;
        int ret = read_from_client(c, buf, len_rd);
        if (ret <= 0)
        {
            if (ret == ERR_TRY_AGAIN)
                print_err(c, "<%s:%d> Error (SSL_ERROR_WANT_READ) read frame %s id=%d \n",
                            __func__, __LINE__, get_str_frame_type(c->h2->type), c->h2->id);
            else
                print_err(c, "<%s:%d> Error read frame %s id=%d \n", __func__, __LINE__,
                            get_str_frame_type(c->h2->type), c->h2->id);
            return ret;
        }

        c->h2->body.ncat(buf, ret);
        c->h2->body_len -= ret;
        if (c->h2->body_len == 0)
            c->h2->header_len = 0;
        else if (c->h2->body_len > 0)
            return ERR_TRY_AGAIN;
    }
    else if (c->h2->body_len == 0)
    {
        c->h2->header_len = 0;
    }

    return 1;
}
//======================================================================
int EventHandlerClass::parse_frame(Connect *c)
{
    if (c->h2->type == SETTINGS)
    {
        c->client_timer = 0;
        if (conf->PrintDebugMsg)
            hex_print_stderr("recv SETTINGS", __LINE__, c->h2->body.ptr(), c->h2->body.size());
        if (c->h2->body.size())
        {
            for (unsigned int i = 0; i < (c->h2->body.size()/6); ++i)
            {
                int ind = i * 6;
                if (c->h2->body.get_byte(ind + 1) == 1)
                {
                    long n = (unsigned char)c->h2->body.get_byte(ind + 5);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 4)<<8);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 3)<<16);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 2)<<24);
                    if (conf->PrintDebugMsg)
                        print_err(c, "<%s:%d> SETTINGS_HEADER_TABLE_SIZE [%ld] id=%d \n",
                                        __func__, __LINE__, n, 0);
                }
                else if (c->h2->body.get_byte(ind + 1) == 4)
                {
                    long n = (unsigned char)c->h2->body.get_byte(ind + 5);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 4)<<8);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 3)<<16);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 2)<<24);
                    c->h2->init_window_size = n;
                    if (conf->PrintDebugMsg)
                        print_err(c, "<%s:%d> SETTINGS_INITIAL_WINDOW_SIZE [%ld] id=%d \n",
                                        __func__, __LINE__, c->h2->init_window_size, 0);
                }
                else if (c->h2->body.get_byte(ind + 1) == 5)
                {
                    unsigned int n = (unsigned char)c->h2->body.get_byte(ind + 5);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 4)<<8);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 3)<<16);
                    n += ((unsigned char)c->h2->body.get_byte(ind + 2)<<24);
                    if (n < c->h2->HTTP2_SendBufSize)
                        c->h2->HTTP2_SendBufSize = n;
                    if (conf->PrintDebugMsg)
                        print_err(c, "<%s:%d> SETTINGS_MAX_FRAME_SIZE [%ld], HTTP2_SendBufSize=%u, id=0 \n",
                                        __func__, __LINE__, n, c->h2->HTTP2_SendBufSize);
                }
            }

            c->h2->recv_settings = true;
            if (c->h2->settings.size() == 0)
                c->h2->settings.ncpy("\x00\x00\x00\x04\x01\x00\x00\x00\x00", 9);
        }
        else
        {
            if (c->h2->header[4] == FLAG_ACK)
            {
                if (conf->PrintDebugMsg)
                    print_err(c, "recv SETTINGS flag=ACK\n");
                c->h2->recv_settings_ack = true;
                if (c->h2->send_settings_ack)
                    c->h2->con_status = PROCESSING_REQUESTS;
            }
            else
            {
                c->h2->recv_settings = true;
                if (c->h2->settings.size() == 0)
                    c->h2->settings.ncpy("\x00\x00\x00\x04\x01\x00\x00\x00\x00", 9);
            }
        }
    }
    else if (c->h2->type == DATA)
    {
        c->client_timer = 0;
        Stream *resp = c->h2->get(c->h2->id);
        if (!resp)
        {
            print_err(c, "<%s:%d> recv DATA: Error list.get(id=%d), h2.body.size()=%d, flag=%d \n", __func__, __LINE__,
                            c->h2->id, c->h2->body.size(), (int)c->h2->header[4]);
            return 0;
        }

        int post_data_size = resp->post_data.size();
        if (post_data_size > 262144)
        {
            print_err(resp, "<%s:%d> !!! post_data_size=%d, post_content_len=%lld, id=%d \n", __func__, __LINE__,
                    post_data_size, resp->post_content_len, resp->id);
            set_error_message(c, resp, RS500);
            return 0;
        }

        if ((c->h2->body.size() == 0) && (c->h2->header[4] == FLAG_END_STREAM))
        {
            if (resp->cgi_type <= PHPCGI)
            {
                if (resp->cgi.to_script > 0)
                {
                    close(resp->cgi.to_script);
                    resp->cgi.to_script = -1;
                }
            }
            else if ((resp->cgi_type == FASTCGI) || (resp->cgi_type == PHPFPM))
            {
                resp->post_data.ncat("\1\5\0\1\0\0\0\0", 8);
                resp->post_content_len = -1; // end post data
            }

            return 0;
        }

        int body_len = c->h2->body.size();
        const char *p_buf = NULL;
        if (c->h2->header[4] & FLAG_PADDED)
        {
            unsigned int padd = (unsigned char)c->h2->body.get_byte(0);
            body_len -= padd;
            p_buf = c->h2->body.ptr() + 1;
        }
        else
            p_buf = c->h2->body.ptr();

        resp->post_content_len -= body_len;
        c->h2->cgi_window_size -= body_len;
        resp->cgi.window_size -= body_len;

        if ((resp->cgi_type <= PHPCGI) || (resp->cgi_type == SCGI))
        {
            resp->post_data.ncat(p_buf, body_len);
        }
        else if ((resp->cgi_type == FASTCGI) || (resp->cgi_type == PHPFPM))
        {
            char s[8];
            fcgi_set_header(s, FCGI_STDIN, body_len);
            resp->post_data.ncat(s, 8);
            if (body_len)
            {
                resp->post_data.ncat(p_buf, body_len);
                if (c->h2->header[4] & FLAG_END_STREAM)
                {
                    resp->post_data.ncat("\1\5\0\1\0\0\0\0", 8);
                    resp->post_content_len = -1; // end post data
                    return 0;
                }
            }
        }

        if (resp->post_content_len < 0)
        {
            print_err(resp, "<%s:%d> !!! Error: cont_length=%lld, size=%d, id=%d \n", __func__, __LINE__,
                        resp->post_content_len, resp->post_data.size(), resp->id);
            set_error_message(c, resp, RS500);
            return 0;
        }
    }
    else if (c->h2->type == HEADERS)
    {
        if (conf->PrintDebugMsg)
            hex_print_stderr(__func__, __LINE__, c->h2->body.ptr(), c->h2->body.size());
        c->client_timer = 0;
        Stream *resp = c->h2->add(c->numConn, c->numReq);
        c->numReq++;
        if (resp == NULL)
        {
            print_err(c, "<%s:%d> Error id=%d \n", __func__, __LINE__, c->h2->id);
            return 0;
        }

        if (c->h2->flags & FLAG_END_HEADERS)
        {
            set_response(c, resp);
            if (conf->PrintDebugMsg)
                print_err(resp, "\"%s\" new request headers.size=%d, id=%d \n", resp->decode_path.c_str(), resp->headers.size(), resp->id);
        }
        else
        {
            // frame CONTINUATION not support
            print_err(resp, "<%s:%d> Error: frame CONTINUATION not support, id=%d \n", __func__, __LINE__, c->h2->id);
            print_err(resp, "\"%s\" new request headers.size=%d, id=%d \n", resp->decode_path.c_str(), resp->headers.size(), c->h2->id);
            set_error_message(c, resp, RS431);
            return 0;
        }
    }
    else if (c->h2->type == CONTINUATION)
    {
        // frame CONTINUATION not support
        print_err(c, "<%s:%d> Error: frame CONTINUATION not support, id=%d \n", __func__, __LINE__, c->h2->id);
        Stream *resp = c->h2->get(c->h2->id);
        if (!resp)
        {
            print_err(c, "<%s:%d> recv DATA: Error list.get(id=%d), h2.body.size()=%d, flag=%d \n", __func__, __LINE__,
                            c->h2->id, c->h2->body.size(), (int)c->h2->header[4]);
            return 0;
        }
        set_rst_stream(c, resp, CANCEL);
        return 0;
    }
    else if (c->h2->type == GOAWAY)
    {
        if (conf->PrintDebugMsg)
        {
            print_err(c, "recv GOAWAY [%s]\n", get_http2_error(c->h2->body.get_byte(7)));
        }

        return -1;
    }
    else if (c->h2->type == RST_STREAM)
    {
        c->client_timer = 0;
        print_err(c, "recv RST_STREAM [%s] id=%d \n", get_http2_error(c->h2->body.get_byte(3)), c->h2->id);
        if (c->h2->id == 0)
        {
            set_frame_goaway(c, PROTOCOL_ERROR);
            return 0;
        }

        Stream *resp = c->h2->get(c->h2->id);
        if (resp)
        {
            if (resp->send_data.size() == 0)
            {
                print_log(c, resp);
                c->h2->close_stream(resp->id);
            }
            else
                resp->recv_rst_stream = true;
        }
        else
            print_err(c, "<%s:%d> RST_STREAM Error stream id=%d does not exist\n", __func__, __LINE__, c->h2->id);
    }
    else if (c->h2->type == PING)
    {
        if (conf->PrintDebugMsg)
            hex_print_stderr("recv PING", __LINE__, c->h2->body.ptr(), c->h2->body.size());
        print_err(c, "recv PING\n");
        c->h2->ping.ncpy("\x0\x0\x8\x6\x1\x0\x0\x0\x0", 9);
        c->h2->ping.ncat(c->h2->body.ptr(), c->h2->body.size());
    }
    else if (c->h2->type == WINDOW_UPDATE)
    {
        c->client_timer = 0;
        long n = 0;
        n += (c->h2->body.get_byte(3));
        n += (c->h2->body.get_byte(2)<<8);
        n += (c->h2->body.get_byte(1)<<16);
        n += (c->h2->body.get_byte(0)<<24);

        if (c->h2->id == 0)
        {
            if (n < 0)
            {
                print_err(c, "<%s:%d> connect_window_size=%ld, n=%ld\n", __func__, __LINE__, c->h2->connect_window_size, n);
                return -1;
            }

            c->h2->connect_window_size += n;
            if (conf->PrintDebugMsg)
                print_err("[%lu] WINDOW_UPDATE %ld[%ld] id=%d \n", c->numConn, n, c->h2->connect_window_size, c->h2->id);
        }
        else
        {
            if (n < 0)
            {
                print_err(c, "<%s:%d> stream_window_size=%ld, n=%ld, id=%d\n", __func__, __LINE__, c->h2->connect_window_size, n, c->h2->id);
                return -1;
            }

            c->h2->set_window_size(c->h2->id, n);
            if (conf->PrintDebugMsg)
                print_err("[%lu] WINDOW_UPDATE %ld id=%d \n", c->numConn, n, c->h2->id);
        }
    }
    else if (c->h2->type == PRIORITY)
    {
        c->client_timer = 0;
        if (conf->PrintDebugMsg)
            print_err(c, "<%s:%d> PRIORITY id=%d \n", __func__, __LINE__, c->h2->id);
    }
    else
    {
        c->client_timer = 0;
        //if (conf->PrintDebugMsg)
            print_err(c, "<%s:%d> frame type: %s, id=%d \n", __func__, __LINE__, get_str_frame_type(c->h2->type), c->h2->id);
    }

    return 0;
}
//======================================================================
void EventHandlerClass::send_frames(Connect *c)
{
    int ret = send_frames_(c);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
        {
            return;
        }
        ssl_shutdown(c);
            return;
    }

    c->h2->try_again = false;
}
//======================================================================
int EventHandlerClass::send_frames_(Connect *c)
{
    if (c->h2->con_status == SET_SETTINGS)
    {
        if (c->h2->goaway.size_remain())
            return send_frame_goawey(c);

        if (c->h2->settings.size() == 0)
        {
            print_err(c, "<%s:%d> !!! SEND_SETTINGS Error: settings.size() = 0\n", __func__, __LINE__);
            return -1;
        }

        return send_frame_settings(c);
    }
    else if (c->h2->con_status == PROCESSING_REQUESTS)
    {
        if (c->h2->try_again)
        {
            switch (c->h2->send_again.type)
            {
                case DATA:
                    return  send_frame_data(c, c->h2->send_again.stream);
                case HEADERS:
                    return send_frame_headers(c, c->h2->send_again.stream);
                case RST_STREAM:
                    return send_frame_rststream(c, c->h2->send_again.stream);
                case GOAWAY:
                    return send_frame_goawey(c);
                case WINDOW_UPDATE:
                    if (c->h2->send_again.id == 0)
                        return send_window_update(c);
                    else
                        return send_window_update(c, c->h2->send_again.stream);
                case PING:
                    return send_frame_ping(c);
                default:
                    print_err(c, "<%s:%d> Error stream id=%d does not exist\n", __func__, __LINE__, c->h2->send_again.id);
                    return -1;
            }
        }

        if (c->h2->goaway.size())
        {
            return send_frame_goawey(c);
        }

        if (c->h2->ping.size())
        {
            return send_frame_ping(c);
        }

        if (c->h2->frame_win_update.size() || (c->h2->cgi_window_size < 16384))
        {
            if (c->h2->cgi_window_size < 0)
            {
                print_err(c, "<%s:%d> !!! c->h2->cgi_window_size=%ld\n", __func__, __LINE__, c->h2->cgi_window_size);
            }

            if (c->h2->frame_win_update.size() == 0)
            {
                c->h2->cgi_window_update = 65535 - c->h2->cgi_window_size;
                set_frame_window_update(c, c->h2->cgi_window_update);
            }
            int ret = send_window_update(c);
            if (ret < 0)
                return ret;
        }
        //--------------------------------------------------------------
        int n = c->h2->size();
        if (n > conf->MaxConcurrentStreams)
        {
            print_err(c, "<%s:%d> ??? h2.size()=%d\n", __func__, __LINE__, n);
            return -1;
        }
        else if (n == 0)
            return 0;

        Stream *resp = c->h2->work_stream;
        if (resp == NULL)
            return 0;

        if (resp->send_rst_stream)
        {
            return send_frame_rststream(c, resp);
        }

        if (resp->frame_win_update.size() || (resp->cgi.window_size < 16384))
        {
            if (resp->cgi.window_size < 0)
            {
                if (conf->PrintDebugMsg)
                    print_err(resp, "<%s:%d> ??? resp->cgi.window_size=%ld\n", __func__, __LINE__, resp->cgi.window_size);
            }

            if (resp->frame_win_update.size() == 0)
            {
                resp->cgi.window_update = 65535 - resp->cgi.window_size;
                set_frame_window_update(resp, resp->cgi.window_update);
            }
            int ret = send_window_update(c, resp);
            if (ret < 0)
                return ret;
        }

        if (resp->headers.size() && resp->create_headers)
        {
            int ret = send_frame_headers(c, resp);
            if (ret <= 0)
                return ret;
        }

        if ((resp->send_headers) && (c->h2->connect_window_size > 0))
        {
            int ret = send_frame_data(c, resp);
            if (ret <= 0)
                return ret;
        }

        c->h2->work_stream = resp->next;
    }
    else
    {
        print_err(c, "<%s:%d> !!! Error: connections status (%s)\n", __func__, __LINE__, c->h2->get_str_status());
        return -1;
    }

    return 0;
}
//======================================================================
int EventHandlerClass::send_frame_headers(Connect *c, Stream *resp)
{
    if (resp->send_headers)
        return 0;

    if (resp->headers.size())
    {
        int len = resp->headers.size() - 9;
        resp->headers.set_byte(len, 2);
        resp->headers.set_byte(len>>=8, 1);
        resp->headers.set_byte(len>>8, 0);

        int ret = write_to_client(c, resp->headers.ptr_remain(), resp->headers.size_remain(), resp->id);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
            {
                return set_send_again(c, resp, HEADERS, resp->id);
            }
            else
            {
                print_err(resp, "<%s:%d> Error send frame HEADERS: %d, id=%d \n", __func__, __LINE__, ret, resp->id);
                return ret;
            }
        }

        c->client_timer = 0;
        resp->headers.inc_offset(ret);
        if (resp->headers.size_remain())
            return ERR_TRY_AGAIN;
        resp->send_headers = true;
        if ((resp->headers.get_byte(4) & FLAG_END_STREAM) || resp->recv_rst_stream)
        {
            if (conf->PrintDebugMsg)
            {
                print_err(resp, "<%s:%d>... send frame HEADERS, END_STREAM, [%s] send %lld bytes ... id=%d \n",
                        __func__, __LINE__, resp->clean_decode_path, resp->send_bytes, resp->id);
            }

            print_log(c, resp);
            c->h2->close_stream(resp->id);
            return 0;
        }
        else
        {
            if (conf->PrintDebugMsg)
            {
                print_err(resp, "<%s:%d> send frame HEADERS: %d, id=%d \n", __func__, __LINE__, ret, resp->id);
                hex_print_stderr(__func__, __LINE__, resp->headers.ptr(), resp->headers.size());
            }

            resp->headers.init();
            if (resp->send_rst_stream)
                return 0;
        }

        return 1;
    }
    else
    {
        print_err(resp, "<%s:%d> !!! Error id=%d \n", __func__, __LINE__, resp->id);
        return -1;
    }
}
//=============================================================================================================================
int EventHandlerClass::send_frame_data(Connect *c, Stream *resp)
{
    if (resp->send_data.size() == 0)
    {
        int ret = set_frame_data(c, resp);
        if (ret < 0)
            return ret;
        else if (ret == 0)
            return 1;
    }

    int ret = write_to_client(c, resp->send_data.ptr_remain(), resp->send_data.size_remain(), resp->id);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
        {
            //print_err(resp, "<%s:%d> Error send frame DATA: %d, %d, id=%d \n", __func__, __LINE__,
            //                                    ret, resp->send_data.size(), resp->id);
            return set_send_again(c, resp, DATA, resp->id);
        }
        else
        {
            print_err(resp, "<%s:%d> Error send frame DATA: %d, %d, send_bytes=%lld, id=%d \n", __func__, __LINE__,
                                                ret, resp->send_data.size(), resp->send_bytes, resp->id);
            resp->send_data.init();
            return ret;
        }
    }

    c->client_timer = 0;
    resp->send_data.inc_offset(ret);
    if (resp->send_data.size_remain())
        return ERR_TRY_AGAIN;
    else
    {
        resp->send_bytes += (resp->send_data.size() - 9);
        resp->stream_window_size -= (resp->send_data.size() - 9);
        c->h2->connect_window_size -= (resp->send_data.size() - 9);
    }

    if ((resp->send_data.get_byte(4) & FLAG_END_STREAM) || resp->recv_rst_stream)
    {
        if (conf->PrintDebugMsg)
        {
            print_err(resp, "~ send DATA: %d, flag=0x%02X, %lld, %ld/%ld, id=%d \n", ret, resp->send_data.get_byte(4),
                        resp->send_bytes, c->h2->connect_window_size, resp->stream_window_size, resp->id);
        }

        print_log(c, resp);
        c->h2->close_stream(resp->id);
        return 0;
    }
    else
    {
        if (conf->PrintDebugMsg)
        {
            print_err(resp, "send DATA: %d, %lld, %ld/%ld, id=%d\n", ret, resp->send_bytes, 
                        c->h2->connect_window_size, resp->stream_window_size, resp->id);
        }
        resp->send_data.init();
        if (resp->send_rst_stream)
            return 0;
    }

    return 1;
}
//======================================================================
int EventHandlerClass::send_frame_settings(Connect *c)
{
    if (c->h2->settings.size() == 0)
    {
        print_err(c, "<%s:%d> !!! SEND_SETTINGS Error: settings.size() = 0\n", __func__, __LINE__);
        return -1;
    }

    int ret = write_to_client(c, c->h2->settings.ptr_remain(), c->h2->settings.size_remain(), 0);
    if (ret < 0)
    {
        print_err(c, "<%s:%d> Error send frame SETTINGS\n", __func__, __LINE__);
        if (ret != ERR_TRY_AGAIN)
            c->h2->settings.init();
        return ret;
    }

    if (conf->PrintDebugMsg)
        hex_print_stderr(__func__, __LINE__, c->h2->settings.ptr(), c->h2->settings.size());

    c->client_timer = 0;
    c->h2->settings.inc_offset(ret);
    if (c->h2->settings.size_remain())
        return ERR_TRY_AGAIN;
    if (c->h2->settings.get_byte(4) == 1)
    {
        c->h2->send_settings_ack = true;
        c->h2->settings.init();
        if (c->h2->recv_settings_ack)
            c->h2->con_status = PROCESSING_REQUESTS;
    }
    else
    {
        if (c->h2->recv_settings)
            c->h2->settings.ncpy("\x00\x00\x00\x04\x01\x00\x00\x00\x00", 9);
        else
            c->h2->settings.init();
    }

    return 0;
}
//======================================================================
int EventHandlerClass::send_frame_ping(Connect *c)
{
    int ret = write_to_client(c, c->h2->ping.ptr_remain(), c->h2->ping.size_remain(), 0);
    if (ret < 0)
    {
        print_err(c, "<%s:%d> Error send frame PING, id=0 \n", __func__, __LINE__);
        if (ret == ERR_TRY_AGAIN)
        {
            return set_send_again(c, NULL, PING, 0);
        }
        else
        {
            c->h2->ping.init();
            return ret;
        }
    }

    if (conf->PrintDebugMsg)
        hex_print_stderr(__func__, __LINE__, c->h2->ping.ptr_remain(), c->h2->ping.size_remain());
    c->h2->ping.inc_offset(ret);
    if (c->h2->ping.size_remain())
        return ERR_TRY_AGAIN;
    c->h2->ping.init();
    return 0;
}
//======================================================================
int EventHandlerClass::send_frame_goawey(Connect *c)
{
    int ret = write_to_client(c, c->h2->goaway.ptr_remain(), c->h2->goaway.size_remain(), 0);
    if (ret < 0)
    {
        print_err(c, "<%s:%d> Error send frame GOAWAY, id=0 \n", __func__, __LINE__);
        if (ret == ERR_TRY_AGAIN)
        {
            return set_send_again(c, NULL, GOAWAY, 0);
        }
        else
        {
            c->h2->goaway.init();
            return ret;
        }
    }

    if (conf->PrintDebugMsg)
        hex_print_stderr(__func__, __LINE__, c->h2->goaway.ptr_remain(), c->h2->goaway.size_remain());
    c->client_timer = 0;
    c->h2->goaway.inc_offset(ret);
    if (c->h2->goaway.size_remain())
        return ERR_TRY_AGAIN;
    c->h2->goaway.init();
    return -1;
}
//======================================================================
int EventHandlerClass::send_frame_rststream(Connect *c, Stream *resp)
{
    if (resp->rst_stream.size() == 0)
    {
        print_err(resp, "<%s:%d> Error size frame RST_STREAM 0 bytes, id=%d \n", __func__, __LINE__, resp->id);
        return -1;
    }

    int ret = write_to_client(c, resp->rst_stream.ptr_remain(), resp->rst_stream.size_remain(), 0);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
        {
            return set_send_again(c, resp, RST_STREAM, resp->id);
        }
        else
        {
            print_err(resp, "<%s:%d> Error send frame RST_STREAM, id=%d \n", __func__, __LINE__, resp->id);
            return ret;
        }
    }

    if (conf->PrintDebugMsg)
        hex_print_stderr(__func__, __LINE__, resp->rst_stream.ptr_remain(), resp->rst_stream.size_remain());
    c->client_timer = 0;
    resp->rst_stream.inc_offset(ret);
    if (resp->rst_stream.size_remain())
        return ERR_TRY_AGAIN;
    resp->resp_status = 0;
    resp->referer = "send frame RST_STREAM";
    print_log(c, resp);
    c->h2->close_stream(resp->id);
    return 0;
}
//======================================================================
int EventHandlerClass::send_window_update(Connect *c)
{
    int ret = write_to_client(c, c->h2->frame_win_update.ptr_remain(), c->h2->frame_win_update.size_remain(), 0);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
        {
            return set_send_again(c, NULL, WINDOW_UPDATE, 0);
        }
        else
        {
            print_err(c, "<%s:%d> Error send frame WINDOW_UPDATE: %d, id=0 \n", __func__, __LINE__, ret);
            return ret;
        }
    }

    c->client_timer = 0;
    c->h2->frame_win_update.inc_offset(ret);
    if (c->h2->frame_win_update.size_remain())
        return ERR_TRY_AGAIN;
    c->h2->cgi_window_size += c->h2->cgi_window_update;
    c->h2->cgi_window_update = 0;
    c->h2->frame_win_update.init();
    return 0;
}
//======================================================================
int EventHandlerClass::send_window_update(Connect *c, Stream *resp)
{
    int ret = write_to_client(c, resp->frame_win_update.ptr_remain(), resp->frame_win_update.size_remain(), resp->id);
    if (ret < 0)
    {
        print_err(resp, "<%s:%d> Error send frame WINDOW_UPDATE: %d, %d, id=%d \n", __func__, __LINE__, ret, resp->frame_win_update.size_remain(), resp->id);
        if (ret == ERR_TRY_AGAIN)
        {
            return set_send_again(c, resp, WINDOW_UPDATE, resp->id);
        }
        else
        {
            print_err(c, "<%s:%d> Error send frame WINDOW_UPDATE: %d, id=%d \n", __func__, __LINE__, ret, resp->id);
            return ret;
        }
    }

    c->client_timer = 0;
    resp->frame_win_update.inc_offset(ret);
    if (resp->frame_win_update.size_remain())
        return ERR_TRY_AGAIN;
    resp->cgi.window_size += resp->cgi.window_update;
    resp->cgi.window_update = 0;
    resp->frame_win_update.init();
    return 0;
}
//======================================================================
const char *static_tab[][2] = {
    {"", ""},
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
    {NULL, NULL}};
