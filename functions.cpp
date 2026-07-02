#include "main.h"

using namespace std;
//======================================================================
string get_time()
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    gmtime_r(&now, &t);
    strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S %Z", &t);
    return s;
}
//======================================================================
string get_time(time_t now)
{
    struct tm t;
    char s[40];

    gmtime_r(&now, &t);
    strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S %Z", &t);
    return s;
}
//======================================================================
string log_time()
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    localtime_r(&now, &t);
    strftime(s, sizeof(s), "%d/%b/%Y:%H:%M:%S %Z", &t);
    return s;
}
//======================================================================
string log_time(time_t now)
{
    struct tm t;
    char s[40];

    localtime_r(&now, &t);
    strftime(s, sizeof(s), "%d/%b/%Y:%H:%M:%S %Z", &t);
    return s;
}
//======================================================================
const char *strstr_case(const char *s1, const char *s2)
{
    const char *p1, *p2;
    char c1, c2;

    if (!s1 || !s2) return NULL;
    if (*s2 == 0) return s1;

    int diff = ('a' - 'A');

    for (; ; ++s1)
    {
        c1 = *s1;
        if (!c1) break;
        c2 = *s2;
        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
        if (c1 == c2)
        {
            p1 = s1++;
            p2 = s2 + 1;

            for (; ; ++s1, ++p2)
            {
                c2 = *p2;
                if (!c2) return p1;

                c1 = *s1;
                if (!c1) return NULL;

                c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
                c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
                if (c1 != c2)
                    break;
            }
        }
    }

    return NULL;
}
//======================================================================
int strlcmp_case(const char *s1, const char *s2, int len)
{
    char c1, c2;

    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    int diff = ('a' - 'A');

    for (; len > 0; --len, ++s1, ++s2)
    {
        c1 = *s1;
        c2 = *s2;
        if (!c1 && !c2) return 0;

        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;

        if (c1 != c2) return (c1 - c2);
    }

    return 0;
}
//======================================================================
int strcmp_case(const char *s1, const char *s2)
{
    char c1, c2;

    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    int diff = ('a' - 'A');

    for (; (*s1) && (*s2); ++s1, ++s2)
    {
        c1 = *s1;
        c2 = *s2;
        if (!c1 && !c2) return 0;

        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;

        if (c1 != c2) return (c1 - c2);
    }

    return (*s1 - *s2);
}
//======================================================================
static int pow_(int x, int y)
{
    if (y < 0)
        return -1;
    int m = 1;
    for (int i = 0; i < y; ++i)
        m = m * x;
    return m;
}
//======================================================================
HTTP_METHOD get_int_method(const char *s)
{
    if (!strlcmp_case(s, "GET", 3))
        return M_GET;
    else if (!strlcmp_case(s, "POST", 4))
        return M_POST;
    else if (!strlcmp_case(s, "HEAD", 4))
        return M_HEAD;
    else
        return M_NULL;
}
//======================================================================
const char *get_str_method(int i)
{
    switch (i)
    {
        case M_GET:
            return "GET";
        case M_POST:
            return "POST";
        case M_HEAD:
            return "HEAD";
    }

    return "";
}
//======================================================================
const char *get_str_protocol(PROTOCOL p)
{
    switch (p)
    {
        case P_HTTP1:
            return "HTTP1";
        case P_HTTP2:
            return "HTTP2";
    }

    return "Protocol unknown";
}
//======================================================================
const char *get_str_frame_type(HTTP2_FRAME_TYPE t)
{
    switch (t)
    {
        case DATA: // 0
            return "DATA";
        case HEADERS: // 1
            return "HEADERS";
        case PRIORITY: // 2
            return "PRIORITY";
        case RST_STREAM: // 3
            return "RST_STREAM";
        case SETTINGS: // 4
            return "SETTINGS";
        case PUSH_PROMISE: // 5
            return "PUSH_PROMISE";
        case PING: // 6
            return "PING";
        case GOAWAY: // 7
            return "GOAWAY";
        case WINDOW_UPDATE: // 8
            return "WINDOW_UPDATE";
        case CONTINUATION: // 9
            return "CONTINUATION";
        case ALTSVC: // 10 (0xA)
            return "ALTSVC";
        case ORIGIN: // 12 (0xC)
            return "ORIGIN";
        case CACHE_DIGEST: // 13 0x0D,
            return "CACHE_DIGEST";
        case PRIORITY_UPDATE: // 16 (0x10)
            return "PRIORITY_UPDATE";
    }

    switch ((int)t)
    {
        case 11:
            return "11 (0XB)";
        case 14:
            return "14 (0xE)";
        case 15:
            return "15 (0xF)";
    }

    return "?";
}
//======================================================================
const char *get_cgi_type(CGI_TYPE t)
{
    switch (t)
    {
        case CGI:
            return "CGI";
        case PHPCGI:
            return "PHPCGI";
        case PHPFPM:
            return "PHPFPM";
        case FASTCGI:
            return "FASTCGI";
        case SCGI:
            return "SCGI";
    }

    return "?";
}
//======================================================================
const char *get_cgi_status(CGI_STATUS s)
{
    switch (s)
    {
        case NO_CGI:
            return "NO_CGI";
        case CGI_CREATE:
            return "CGI_CREATE";
        case FASTCGI_BEGIN:
            return "FASTCGI_BEGIN";
        case FASTCGI_PARAMS:
            return "FASTCGI_PARAMS";
        case SCGI_PARAMS:
            return "SCGI_PARAMS";
        case CGI_STDIN:
            return "CGI_STDIN";
        case CGI_STDOUT:
            return "CGI_STDOUT";
    }

    return "?";
}
//======================================================================
const char *get_http2_error(int err)
{
    switch (err)
    {
        case NO_ERROR: // 0
            return "NO_ERROR";
        case PROTOCOL_ERROR: // 1
            return "PROTOCOL_ERROR";
        case INTERNAL_ERROR: // 2
            return "INTERNAL_ERROR";
        case FLOW_CONTROL_ERROR: // 3
            return "FLOW_CONTROL_ERROR";
        case SETTINGS_TIMEOUT: // 4
            return "SETTINGS_TIMEOUT";
        case STREAM_CLOSED: // 5
            return "STREAM_CLOSED";
        case FRAME_SIZE_ERROR: // 6
            return "FRAME_SIZE_ERROR";
        case REFUSED_STREAM: // 7
            return "REFUSED_STREAM";
        case CANCEL: // 8
            return "CANCEL";
        case COMPRESSION_ERROR: // 9
            return "COMPRESSION_ERROR";
        case CONNECT_ERROR: // 10 (0xA)
            return "CONNECT_ERROR";
        case ENHANCE_YOUR_CALM: // 11 (0xB)
            return "ENHANCE_YOUR_CALM";
        case INADEQUATE_SECURITY: // 12 (0xC)
            return "INADEQUATE_SECURITY";
        case HTTP_1_1_REQUIRED: // 13 (0xD)
            return "HTTP_1_1_REQUIRED";
    }

    switch ((int)err)
    {
        case 14:
            return "14 (0XE)";
        case 15:
            return "15 (0xF)";
        case 16:
            return "16 (0x10)";
    }

    return "?";
}
//======================================================================
const char *get_str_sourcedata(SOURCE_DATA n)
{
    switch (n)
    {
        case NO_SOURCE:
            return "NO_SOURCE";
        case DIRECTORY:
            return "DIRECTORY";
        case FROM_FILE:
            return "FROM_FILE";
        case FROM_DATA_BUFFER:
            return "FROM_DATA_BUFFER";
        case DYN_PAGE:
            return "DYN_PAGE";
    }

    return "?";
}
//======================================================================
const char *content_type(const char *s)
{
    const char *p = strrchr(s, '.');

    if (!p)
        goto end;

    //       video
    if (!strlcmp_case(p, ".ogv", 4))
        return "video/ogg";
    else if (!strlcmp_case(p, ".mp4", 4))
        return "video/mp4";
    else if (!strlcmp_case(p, ".avi", 4))
        return "video/x-msvideo";
    else if (!strlcmp_case(p, ".mov", 4))
        return "video/quicktime";
    else if (!strlcmp_case(p, ".mkv", 4))
        return "video/x-matroska";
    else if (!strlcmp_case(p, ".flv", 4))
        return "video/x-flv";
    else if (!strlcmp_case(p, ".mpeg", 5) || !strlcmp_case(p, ".mpg", 4))
        return "video/mpeg";
    else if (!strlcmp_case(p, ".asf", 4))
        return "video/x-ms-asf";
    else if (!strlcmp_case(p, ".wmv", 4))
        return "video/x-ms-wmv";
    else if (!strlcmp_case(p, ".swf", 4))
        return "application/x-shockwave-flash";
    else if (!strlcmp_case(p, ".3gp", 4))
        return "video/video/3gpp";

    //       sound
    else if (!strlcmp_case(p, ".mp3", 4))
        return "audio/mpeg";
    else if (!strlcmp_case(p, ".wav", 4))
        return "audio/x-wav";
    else if (!strlcmp_case(p, ".ogg", 4))
        return "audio/ogg";
    else if (!strlcmp_case(p, ".pls", 4))
        return "audio/x-scpls";
    else if (!strlcmp_case(p, ".aac", 4))
        return "audio/aac";
    else if (!strlcmp_case(p, ".aif", 4))
        return "audio/x-aiff";
    else if (!strlcmp_case(p, ".ac3", 4))
        return "audio/ac3";
    else if (!strlcmp_case(p, ".voc", 4))
        return "audio/x-voc";
    else if (!strlcmp_case(p, ".flac", 5))
        return "audio/flac";
    else if (!strlcmp_case(p, ".amr", 4))
        return "audio/amr";
    else if (!strlcmp_case(p, ".au", 3))
        return "audio/basic";

    //       image
    else if (!strlcmp_case(p, ".gif", 4))
        return "image/gif";
    else if (!strlcmp_case(p, ".svg", 4) || !strlcmp_case(p, ".svgz", 5))
        return "image/svg+xml";
    else if (!strlcmp_case(p, ".png", 4))
        return "image/png";
    else if (!strlcmp_case(p, ".ico", 4))
        return "image/vnd.microsoft.icon";
    else if (!strlcmp_case(p, ".jpeg", 5) || !strlcmp_case(p, ".jpg", 4))
        return "image/jpeg";
    else if (!strlcmp_case(p, ".djvu", 5) || !strlcmp_case(p, ".djv", 4))
        return "image/vnd.djvu";
    else if (!strlcmp_case(p, ".tiff", 5))
        return "image/tiff";
    //       text
    else if (!strlcmp_case(p, ".txt", 4))
        return "text/plain; charset=UTF-8";
    else if (!strlcmp_case(p, ".html", 5) || !strlcmp_case(p, ".htm", 4) || !strlcmp_case(p, ".shtml", 6))
        return "text/html; charset=UTF-8";
    else if (!strlcmp_case(p, ".css", 4))
        return "text/css";

    //       application
    else if (!strlcmp_case(p, ".pdf", 4))
        return "application/pdf";
    else if (!strlcmp_case(p, ".gz", 3))
        return "application/gzip";
end:
    return NULL;
}
//======================================================================
int clean_path(char *path, int len)
{
    int i = 0, j = 0, level_dir = 0;
    char ch;
    char prev_ch = '\0';
    int index_slash[64] = {0};

    while ((ch = *(path + j)) && (len > 0))
    {
        if (prev_ch == '/')
        {
            if (ch == '/')
            {
                --len;
                ++j;
                continue;
            }

            if (ch == '.')
            {
                switch (len)
                {
                    case 1:
                        --len;
                        ++j;
                        continue;
                    case 2:
                        if (!memcmp(path + j, "..", 2))
                        {
                            if (level_dir > 1)
                            {
                                j += 2;
                                len -= 2;
                                --level_dir;
                                i = index_slash[level_dir];
                                continue;
                            }
                            else
                            {
                                return -RS400;
                            }
                        }
                        else if (!memcmp(path + j, "./", 2))
                        {
                            len -= 2;
                            j += 2;
                            continue;
                        }
                        else // hidden file
                        {
                            return -RS404;
                        }
                    case 3:
                        if (!memcmp(path + j, "../", 3))
                        {
                            if (level_dir > 1)
                            {
                                j += 3;
                                len -= 3;
                                --level_dir;
                                i = index_slash[level_dir];
                                continue;
                            }
                            else
                            {
                                return -RS400;
                            }
                        }
                        else if (!memcmp(path + j, "./.", 3))
                        {
                            len -= 3;
                            j += 3;
                            continue;
                        }
                        else if (!memcmp(path + j, ".//", 3))
                        {
                            len -= 3;
                            j += 3;
                            continue;
                        }
                        else // hidden file
                        {
                            return -RS404;
                        }
                    default:
                        if (!memcmp(path + j, "../", 3))
                        {
                            if (level_dir > 1)
                            {
                                j += 3;
                                len -= 3;
                                --level_dir;
                                i = index_slash[level_dir];
                                continue;
                            }
                            else
                            {
                                return -RS400;
                            }
                        }
                        else if (!memcmp(path + j, "./", 2))
                        {
                            len -= 2;
                            j += 2;
                            continue;
                        }
                        else // hidden file
                        {
                            return -RS404;
                        }
                }
            }
        }

        *(path + i) = ch;
        ++i;
        ++j;
        --len;
        prev_ch = ch;
        if (ch == '/')
        {
            if (level_dir >= (int)(sizeof(index_slash)/sizeof(int)))
                return -RS404;
            ++level_dir;
            index_slash[level_dir] = i;
        }
    }

    *(path + i) = 0;
    return i;
}
//======================================================================
long long file_size(const char *s)
{
    struct stat st;

    if (!stat(s, &st))
        return st.st_size;
    else
        return -1;
}
//======================================================================
int bytes_to_int(unsigned char prefix, int pref_len, const char *s, int size, int *len)
{
    int data = pow_(2, pref_len) - 1;
    if (prefix < data)
        data = prefix;
    else
    {
        unsigned char ch;
        for (int i = 0; (*len) < size; ++i)
        {
            ch = s[(*len)++];
            data = data + ((ch & 0x7f)<<(i*7));
            if (!(ch & 0x80))
                break;
        }
    }

    return data;
}
//======================================================================
int int_to_bytes(BytesArray& buf, int data, int pref_len, int huff_coding_mask)
{
    int ret = 0;

    if (data < (pow_(2, pref_len) - 1))
    {
        buf.bytecat((data | huff_coding_mask));
        ++ret;
    }
    else
    {
        buf.bytecat((pow_(2, pref_len) - 1) | huff_coding_mask);
        ++ret;
        data = data - (pow_(2, pref_len) - 1);
        while (data > 128)
        {
            buf.bytecat(data % 128 + 128);
            ++ret;
            data = data / 128;
        }

        buf.bytecat((char)data);
        ++ret;
    }

    return ret;
}
//======================================================================
SOURCE_DATA get_content_type(const char *path)
{
    struct stat st;
    int ret = lstat(path, &st);
    if (ret == -1)
    {
        return NO_SOURCE;
    }

    if (S_ISDIR(st.st_mode))
        return DIRECTORY;
    else if (S_ISREG(st.st_mode))
        return FROM_FILE;
    else
        return NO_SOURCE;
}
//======================================================================
int parse_range(const char *s, long long file_size, long long *offset, long long *content_length)
{
    const char *p = strchr(s, '=');
    if (p == NULL)
        return -1;
    if (*(p + 1) == '-')
    {
        p += 2;
        char buf[32];
        buf[0] = 0;
        int i = 0;
        for ( ; *p; )
        {
            char ch = *p;
            if (isdigit(ch))
            {
                buf[i++] = ch;
                ++p;
            }
            else
                break;
        }
        buf[i] = 0;

        if (*p != 0)
            return 0;
        sscanf(buf, "%lld", content_length);
        if (*content_length > file_size)
            *content_length = file_size;
        *offset = file_size - *content_length;
    }
    else
    {
        ++p;
        char buf[32];
        buf[0] = 0;
        int i = 0;
        for ( ; *p; )
        {
            char ch = *p;
            if (isdigit(ch))
            {
                buf[i++] = ch;
                ++p;
            }
            else
                break;
        }
        buf[i] = 0;

        if (*p != '-')
            return 0;
        sscanf(buf, "%lld", offset);
        if (*offset >= file_size)
            return -RS416;
        ++p;
        buf[0] = 0;
        i = 0;
        for ( ; *p; )
        {
            char ch = *p;
            if (isdigit(ch))
            {
                buf[i++] = ch;
                ++p;
            }
            else
                break;
        }
        buf[i] = 0;

        if (*p != 0)
            return 0;

        if (strlen(buf) == 0)
        {
            if (*offset > (file_size - 1))
                return -RS416;
            *content_length = file_size - *offset;
        }
        else
        {
            long long end = 0;
            sscanf(buf, "%lld", &end);
            if (end > (file_size - 1))
                end = file_size - 1;
            *content_length = end + 1 - *offset;
        }
    }

    return 1;
}
//======================================================================
static void set_error(Stream *resp, int err)
{
    const char *status = http2_status_resonse(err);
    if (status == NULL)
    {
        resp->resp_status = RS500;
        status = "500";
    }
    else
        resp->resp_status = err;
    set_frame_headers(resp);
    add_header(resp->headers, 8, status);
    add_header(resp->headers, 54, conf->ServerSoftware.c_str());
    add_header(resp->headers, 33, get_time().c_str());
    add_header(resp->headers, 31, "text/plain");
    resp->create_headers = true;

    const char *err_msg = http1_status_response(err);
    int len = strlen(err_msg);
    set_frame_data(resp, len, FLAG_END_STREAM);
    resp->send_data.ncat(err_msg, len);
}
//======================================================================
void set_error_message(Connect *c, Stream *resp, int err)
{
    if (resp->send_headers)
    {
        set_rst_stream(c, resp, CANCEL);
    }
    else
    {
        if ((c->h2->try_again == true) && resp->headers.size())
            set_rst_stream(c, resp, CANCEL);
        else
            set_error(resp, err);
    }
}
//======================================================================
const char *http2_status_resonse(int st)
{
    switch (st)
    {
        case RS200:
            return "200";
        case RS204:
            return "204";
        case RS206:
            return "206";
        case RS301:
            return "301";
        case RS302:
            return "302";
        case RS400:
            return "400";
        case RS401:
            return "401";
        case RS402:
            return "402";
        case RS403:
            return "403";
        case RS404:
            return "404";
        case RS405:
            return "405";
        case RS406:
            return "406";
        case RS407:
            return "407";
        case RS408:
            return "408";
        case RS411:
            return "411";
        case RS413:
            return "413";
        case RS414:
            return "414";
        case RS416:
            return "416";
        case RS417:
            return "417";
        case RS418:
            return "418";
        case RS429:
            return "429";
        case RS431:
            return "431";
        case RS500:
            return "500";
        case RS501:
            return "501";
        case RS502:
            return "502";
        case RS503:
            return "503";
        case RS504:
            return "504";
        case RS505:
            return "505";
        default:
            return "500";
    }
    return NULL;
}
//======================================================================
void hex_print_stderr(const char *s, int line, const void *p, int n)
{
    int count, addr = 0, col;
    unsigned char *buf = (unsigned char*)p;
    char str[18];
    fprintf(stderr, " [%s] <%d>--------------- HEX -----------------\n", s, line);
    for(count = 0; count < n;)
    {
        fprintf(stderr, "%08X  ", addr);
        for(col = 0, addr = addr + 0x10; (count < n) && (col < 16); count++, col++)
        {
            if (col == 8) fprintf(stderr, " ");
            fprintf(stderr, "%02X ", *(buf+count));
            str[col] = (*(buf + count) >= 32 && *(buf + count) < 127) ? *(buf + count) : '.';
        }
        str[col] = 0;
        if (col <= 8) fprintf(stderr, " ");
        fprintf(stderr, "%*s  %s\n",(16 - (col)) * 3, "", str);
    }

    fprintf(stderr, "\n");
}
