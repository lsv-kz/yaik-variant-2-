#include "main.h"

using namespace std;

static Config c;
const Config* const conf = &c;
static int set_max_fd(int max_open_fd);
//======================================================================
enum { CURRENT_DIR, PARENT_DIR, ADD_FOLDER };
//======================================================================
static int parse_folder(const char *s, int i)// "../", "/../", "/.."
{
    if (!strncmp(s, "../", i) )
        return PARENT_DIR;
    else if (!strncmp(s, "/../", i))
        return PARENT_DIR;
    else if (!strncmp(s, "/..", i))
        return PARENT_DIR;
    else if (!strncmp(s, "./", i))
        return CURRENT_DIR;
    else if (!strncmp(s, "/.", i))
        return CURRENT_DIR;
    else if (!strncmp(s, "/./", i))
        return CURRENT_DIR;
    return ADD_FOLDER;
}
//======================================================================
static void *memrchr_(const void *s, char c, int n)
{
    if (n > 0)
    {
        const char *p = (const char *)s + n;
        while (--n > 0)
        {
            if (*(--p) == c)
                return (void *)p;
        }
    }
    return NULL;
}
//======================================================================
static int absolute_path(string& path)
{
    struct stat st;
    int ret = stat(path.c_str(), &st);
    if (ret == -1)
    {
        fprintf(stderr, "<%s:%d> Error stat(%s): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
        return -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "<%s:%d> [%s] is not directory\n", __func__, __LINE__, path.c_str());
        return -1;
    }

    if (path[0] == '/')
    {
        if (path[path.size() - 1] == '/')
            path.resize(path.size() - 1);

        return 0;
    }

    char cwd[8192];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        fprintf(stderr, "<%s:%d> Error getcwd(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    int cwd_len = strlen(cwd);
    if (cwd[cwd_len - 1] == '/')
    {
        cwd[cwd_len - 1] = 0;
        cwd_len--;
    }

    int path_len = path.size();
    int i_path = 0;
    int anchor[2] = {0};
    char ch;
    char prev_ch = '\0';

    while (path_len > 0)
    {
        --path_len;
        anchor[1] = i_path;
        ch = path[i_path++];

        if (ch == '/')
        {
            if (prev_ch != '/')
            {
                int ret = parse_folder(path.c_str() + anchor[0], anchor[1] - anchor[0] + 1);
                if (ret == PARENT_DIR)// [/../], [../]
                {
                    char *p = (char*)memrchr_(cwd, '/', cwd_len);
                    if (p)
                    {
                        if (p == cwd)
                        {
                            cwd_len = 1;
                            cwd[cwd_len] = 0;
                        }
                        else
                        {
                            *p = 0;
                            cwd_len = p - cwd;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "<%s:%d> Error: '/' not found\n", __func__, __LINE__);
                        return -1;
                    }
                }
                else if (ret == ADD_FOLDER)
                {
                    int len = anchor[1] - anchor[0];
                    if (path[anchor[0]] != '/')
                    {
                        if ((cwd_len + len + 1) >= (int)sizeof(cwd))
                        {
                            fprintf(stderr, "<%s:%d> Error: danger of overflow (%d => %d)\n", __func__, __LINE__, cwd_len + len + 1, (int)sizeof(cwd));
                            return -1;
                        }
                        *(cwd + cwd_len) = '/';
                        ++cwd_len;
                        cwd[cwd_len] = 0;
                    }
                    else
                    {
                        if ((cwd_len + len) >= (int)sizeof(cwd))
                        {
                            fprintf(stderr, "<%s:%d> Error: danger of overflow (%d => %d)\n", __func__, __LINE__, cwd_len + len, (int)sizeof(cwd));
                            return -1;
                        }
                    }

                    memcpy(cwd + cwd_len, path.c_str() + anchor[0], len);
                    cwd_len += len;
                    *(cwd + cwd_len) = 0;
                }
            }

            anchor[0] = anchor[1];
        }
        else
        {
            if (path_len == 0)
            {
                int ret = parse_folder(path.c_str() + anchor[0], anchor[1] - anchor[0] + 1);
                if (ret == PARENT_DIR)// [/../], [../]
                {
                    char *p = (char*)memrchr_(cwd, '/', cwd_len);
                    if (p)
                    {
                        if (p == cwd)
                        {
                            cwd_len = 1;
                            cwd[cwd_len] = 0;
                        }
                        else
                        {
                            *p = 0;
                            cwd_len = p - cwd;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "<%s:%d> Error: '/' not found\n", __func__, __LINE__);
                        return -1;
                    }
                }
                else if (ret == ADD_FOLDER)
                {
                    int len = anchor[1] - anchor[0] + 1;
                    if (path[anchor[0]] != '/')
                    {
                        if ((cwd_len + len + 1) >= (int)sizeof(cwd))
                        {
                            fprintf(stderr, "<%s:%d> Error: danger of overflow (%d => %d)\n", __func__, __LINE__, cwd_len + len + 1, (int)sizeof(cwd));
                            return -1;
                        }
                        *(cwd + cwd_len) = '/';
                        ++cwd_len;
                        cwd[cwd_len] = 0;
                    }
                    else
                    {
                        if ((cwd_len + len) >= (int)sizeof(cwd))
                        {
                            fprintf(stderr, "<%s:%d> Error: danger of overflow (%d => %d)\n", __func__, __LINE__, cwd_len + len, (int)sizeof(cwd));
                            return -1;
                        }
                    }

                    memcpy(cwd + cwd_len, path.c_str() + anchor[0], len);
                    cwd_len += len;
                    *(cwd + cwd_len) = 0;
                }
            }
        }

        prev_ch = ch;
    }

    path = cwd;
    if (stat(path.c_str(), &st) == -1)
    {
        fprintf(stderr, "<%s:%d> Error stat(%s): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
        return -1;
    }
    return 0;
}
//======================================================================
static void create_conf_file(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        fprintf(stderr, "<%s> Error create conf file (%s): %s\n", __func__, path, strerror(errno));
        exit(1);
    }

    const char *conf_file =
    "PrintDebugMsg        off       # on, off\n\n"
    "ServerSoftware       ?\n\n"
    "LogPath              ?\n"
    "server {\n"
    "    ip             0.0.0.0\n"
    "    ServerPort     80\n"
    "    Redirect       https://my-example.com\n"
    "}\n\n"
    "server {\n"
    "    ip             0.0.0.0\n"
    "    ServerPort     443\n"
    "    SecureConnect  on\n"
    "    EnableHTTP2    on\n"
    "    vhost {\n"
    "        HostName         my-example.com\n"
    "        DocumentRoot     /?/my-example.com\n"
    "        Certificate      ?\n"
    "        CertificateKey   ?\n"
    "    }\n"
    "}\n\n"
    "server {\n"
    "    ip             0.0.0.0\n"
    "    ServerPort     8080\n"
    "    vhost {\n"
    "        HostName         ?\n"
    "        DocumentRoot     ?\n"
    "    }\n"
    "}\n\n"
    "ListenBacklog         4096\n"
    "TcpNoDelay            on\n\n"
    "HeaderTableSize       0\n"
    "MaxConcurrentStreams  10\n\n"
    "MaxAcceptConnections  10000\n\n"
    "MaxRequestsPerClient  1000\n\n"
    "HTTP1_DataBufSize     262144\n"
    "HTTP2_RecvBufSize     16384\n\n"
    "Timeout               35  # seconds\n"
    "TimeoutKeepAlive      120 # seconds\n"
    "TimeoutPoll           10  # milliseconds\n\n"
    "ShowMediaFiles        off\n\n"
    "User                  root\n"
    "Group                 www-data\n";

    fwrite(conf_file, 1, strlen(conf_file), f);
    fclose(f);
}
//======================================================================
static int line_ = 1, line_inc = 0;
static char bracket = 0;
//----------------------------------------------------------------------
static int getLine(FILE *f, char *str, int size)
{
    if ((str == NULL) || (size == 0))
    {
        return -1;
    }

    int offset = 0;
    str[offset] = 0;

    if (bracket)
    {
        str[offset++] = bracket;
        str[offset] = 0;
        bracket = 0;
        return 1;
    }

    int ch, len = 0, numWords = 0, wr = 1, wrSpace = 0;

    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '\n')
        {
            if (len)
            {
                line_inc = 1;
                return ++numWords;
            }
            else
            {
                ++line_;
                wr = 1;
                offset = 0;
                str[offset] = 0;
                wrSpace = 0;
                continue;
            }
        }
        else if (wr == 0)
            continue;
        else if ((ch == ' ') || (ch == '\t'))
        {
            if (len)
                wrSpace = 1;
        }
        else if (ch == '#')
            wr = 0;
        else if ((ch == '{') || (ch == '}'))
        {
            if (len)
                bracket = (char)ch;
            else
            {
                str[offset++] = (char)ch;
                str[offset] = 0;
                ++len;
            }
            return ++numWords;
        }
        else if (ch != '\r')
        {
            if (wrSpace)
            {
                str[offset++] = ' ';
                str[offset] = 0;
                ++len;
                ++numWords;
                wrSpace = 0;
            }

            str[offset++] = (char)ch;
            str[offset] = 0;
            ++len;
        }
    }

    if (len)
        return ++numWords;
    return -1;
}
//======================================================================
static int is_number(const char *s)
{
    if (!s)
        return 0;
    int n = isdigit((int)*(s++));
    while (*s && n)
        n = isdigit((int)*(s++));
    return n;
}
//======================================================================
static int find_bracket(FILE *f, char c)
{
    if (bracket)
    {
        if (c != bracket)
        {
            bracket = 0;
            return 0;
        }

        bracket = 0;
        return 1;
    }

    int ch, grid = 0;
    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '#')
        {
            grid = 1;
        }
        else if (ch == '\n')
        {
            grid = 0;
            ++line_;
        }
        else if ((ch == '{') && (grid == 0))
            return 1;
        else if ((ch != ' ') && (ch != '\t') && (grid == 0))
            return 0;
    }

    return 0;
}
//======================================================================
static void create_fcgi_list(fcgi_list_addr **l, const char *s1, const char *s2, CGI_TYPE type)
{
    if (l == NULL)
    {
        fprintf(stderr, "<%s:%d> Error pointer = NULL\n", __func__, __LINE__);
        exit(errno);
    }

    fcgi_list_addr *t;
    try
    {
        t = new fcgi_list_addr;
    }
    catch (...)
    {
        fprintf(stderr, "<%s:%d> Error new(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    t->script_name = s1;
    t->addr = s2;
    t->type = type;
    t->next = *l;
    *l = t;
}
//======================================================================
static int read_conf_file(FILE *fconf)
{
    char str[1024];
    bool set_default_server = false;
    bool SecureConnect = false;
    Server *prev_server = NULL;

    int n;
    while ((n = getLine(fconf, str, sizeof(str) - 1)) > 0)
    {
        if (n == 2)
        {
            char s1[512], s2[512];
            if (sscanf(str, "%s %s", s1, s2) != 2)
            {
                fprintf(stderr, "<%s:%d> Error sscanf(%s) != 2\n", __func__, __LINE__, str);
                return -1;
            }

            if (!strcmp(s1, "PrintDebugMsg"))
            {
                if (!strcmp_case(s2, "on"))
                    c.PrintDebugMsg = true;
                else if (!strcmp_case(s2, "off"))
                    c.PrintDebugMsg = false;
                else
                {
                    fprintf(stderr, "<%s:%d> Error config file line <%d> \"%s\": [on | off]\n",
                            __func__, __LINE__, line_, str);
                    return -1;
                }
            }
            else if (!strcmp(s1, "ServerSoftware"))
                c.ServerSoftware = s2;
            else if ((!strcmp(s1, "HeaderTableSize")) && is_number(s2))
                c.HeaderTableSize = atoi(s2);
            else if ((!strcmp(s1, "MaxConcurrentStreams")) && is_number(s2))
                c.MaxConcurrentStreams = atoi(s2);
            else if (!strcmp(s1, "TcpNoDelay"))
            {
                if (!strcmp_case(s2, "on"))
                    c.TcpNoDelay = true;
                else if (!strcmp_case(s2, "off"))
                    c.TcpNoDelay = false;
                else
                {
                    fprintf(stderr, "<%s:%d> Error config file line <%d> \"%s\": [on | off]\n",
                            __func__, __LINE__, line_, str);
                    return -1;
                }
            }
            else if ((!strcmp(s1, "ListenBacklog")) && is_number(s2))
                c.ListenBacklog = atoi(s2);
            else if ((!strcmp(s1, "HTTP1_DataBufSize")) && is_number(s2))
                c.HTTP1_DataBufSize = atoi(s2);
            else if ((!strcmp(s1, "HTTP2_RecvBufSize")) && is_number(s2))
            {
                c.HTTP2_RecvBufSize = atoi(s2);
                if ((c.HTTP2_RecvBufSize <= 0) || (c.HTTP2_RecvBufSize > 16384))
                {
                    fprintf(stderr, "<%s:%d> Error read config file: HTTP2_RecvBufSize > 16384, [%s], line <%d>\n", __func__, __LINE__, str, line_);
                    return -1;
                }
            }
            else if ((!strcmp(s1, "MaxAcceptConnections")) && is_number(s2))
                c.MaxAcceptConnections = atoi(s2);
            else if ((!strcmp(s1, "MaxRequestsPerClient")) && is_number(s2))
                c.MaxRequestsPerClient = atoi(s2);
            else if ((!strcmp(s1, "TimeoutPoll")) && is_number(s2))
                c.TimeoutPoll = atoi(s2);
            else if (!strcmp(s1, "ScriptPath"))
                c.ScriptPath = s2;
            else if (!strcmp(s1, "LogPath"))
                c.LogPath = s2;
            else if ((!strcmp(s1, "MaxCgiProc")) && is_number(s2))
                c.MaxCgiProc = atoi(s2);
            else if ((!strcmp(s1, "Timeout")) && is_number(s2))
                c.Timeout = atoi(s2);
            else if ((!strcmp(s1,"TimeoutKeepAlive")) && is_number(s2))
                c.TimeoutKeepAlive = atoi(s2);
            else if ((!strcmp(s1,"TimeoutCGI")) && is_number(s2))
                c.TimeoutCGI = atoi(s2);
            else if (!strcmp(s1, "UsePHP"))
            {
                if ((!strcmp(s2, "off")) || (!strcmp(s2, "php-fpm")) || (!strcmp(s2, "php-cgi")))
                    c.UsePHP = s2;
                else
                {
                    fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
                    return -1;
                }
            }
            else if (!strcmp(s1, "PathPHP"))
                c.PathPHP = s2;
            else if (!strcmp(s1, "ShowMediaFiles"))
            {
                if (!strcmp_case(s2, "on"))
                    c.ShowMediaFiles = true;
                else if (!strcmp_case(s2, "off"))
                    c.ShowMediaFiles = false;
                else
                {
                    fprintf(stderr, "<%s:%d> Error config file line <%d> \"%s\": [on | off]\n",
                            __func__, __LINE__, line_, str);
                    return -1;
                }
            }
            else if ((!strcmp(s1, "ClientMaxBodySize")) && is_number(s2))
                c.ClientMaxBodySize = atoi(s2);
            else if (!strcmp(s1, "User"))
                c.user = s2;
            else if (!strcmp(s1, "Group"))
                c.group = s2;
            else
            {
                fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
                return -1;
            }
        }
        else if (n == 1)
        {
            if (!strcmp(str, "ServerSoftware"))
                c.ServerSoftware = "";
            else if (!strcmp(str, "server"))
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }

                Server *serv = c.create_server();
                if (conf->servers_list == NULL)
                    c.servers_list = prev_server = serv;
                else
                {
                    prev_server->next = serv;
                    prev_server = serv;
                }

                VHost *default_vhost = NULL, *prev_vhost = NULL;
                while ((n = getLine(fconf, str, sizeof(str) - 1)) > 0)
                {
                    if (n == 2)
                    {
                        char s1[512], s2[512];
                        if (sscanf(str, "%s %s", s1, s2) != 2)
                        {
                            fprintf(stderr, "<%s:%d> Error sscanf(%s) != 2\n", __func__, __LINE__, str);
                            return -1;
                        }

                        if (!strcmp(s1, "ip"))
                            serv->ip = s2;
                        else if ((!strcmp(s1, "ServerPort")) && is_number(s2))
                            serv->port = s2;
                        else if (!strcmp(s1, "SecureConnect"))
                        {
                            if (!strcmp_case(s2, "on"))
                            {
                                serv->SecureConnect = true;
                                if (SecureConnect == false)
                                {
                                    init_openssl();
                                    SecureConnect = true;
                                }
                            }
                            else if (!strcmp_case(s2, "off"))
                                serv->SecureConnect = false;
                            else
                            {
                                fprintf(stderr, "<%s:%d> Error config file line <%d> \"%s\": [on | off]\n",
                                        __func__, __LINE__, line_, str);
                                return -1;
                            }
                        }
                        else if (!strcmp(s1, "EnableHTTP2"))
                        {
                            if (!strcmp_case(s2, "on"))
                                serv->EnableHTTP2 = true;
                            else if (!strcmp_case(s2, "off"))
                                serv->EnableHTTP2 = false;
                            else
                            {
                                fprintf(stderr, "<%s:%d> Error config file line <%d> \"%s\": [on | off]\n",
                                        __func__, __LINE__, line_, str);
                                return -1;
                            }
                        }
                        else if (!strcmp(s1, "Redirect"))
                            serv->redirect = s2;
                        else
                        {
                            fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
                            return -1;
                        }
                    }
                    else if (n == 1)
                    {
                        if (!strcmp(str, "vhost"))
                        {
                            VHost *vhost;
                            try
                            {
                                vhost = new VHost;
                            }
                            catch (...)
                            {
                                fprintf(stderr, "<%s:%d> Error new(): %s\n", __func__, __LINE__, strerror(errno));
                                exit(errno);
                            }

                            if (default_vhost == NULL)
                            {
                                serv->vhosts = default_vhost = prev_vhost = vhost;
                            }
                            else
                            {
                                prev_vhost->next = vhost;
                                prev_vhost = vhost;
                            }

                            if (find_bracket(fconf, '{') == 0)
                            {
                                fprintf(stderr, "<%s:%d> Error not found \"{\", line <%d>\n", __func__, __LINE__, line_);
                                return -1;
                            }

                            while (getLine(fconf, str, sizeof(str) - 1) == 2)
                            {
                                char s1[512], s2[512];
                                if (sscanf(str, "%s %s", s1, s2) != 2)
                                {
                                    fprintf(stderr, "<%s:%d> Error sscanf(%s) != 2\n", __func__, __LINE__, str);
                                    return -1;
                                }

                                if (!strcmp(s1, "HostName"))
                                    vhost->hostname = s2;
                                else if (!strcmp(s1, "DocumentRoot"))
                                {
                                    vhost->DocumentRoot = s2;
                                    if (set_default_server == false)
                                    {
                                        c.DocumentRoot = vhost->DocumentRoot;
                                        set_default_server = true;
                                    }
                                }
                                else if (!strcmp(s1, "Certificate"))
                                    vhost->Certificate = s2;
                                else if (!strcmp(s1, "CertificateKey"))
                                    vhost->CertificateKey = s2;
                                else
                                {
                                    fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
                                    return -1;
                                }
                            }

                            if (strcmp(str, "}"))
                            {
                                fprintf(stderr, "<%s:%d> Error not found \"}\", line <%d>\n", __func__, __LINE__, line_);
                                return -1;
                            }
                        }
                        else if (!strcmp(str, "}"))
                            break;
                        else
                        {
                            fprintf(stderr, "<%s:%d> Error not found \"}\", [%s], line <%d>\n", __func__, __LINE__, str, line_);
                            return -1;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
                        return -1;
                    }
                }

                /*if (strcmp(str, "}"))
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }*/
            }
            else if (!strcmp(str, "fastcgi"))
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }

                while (getLine(fconf, str, sizeof(str) - 1) == 2)
                {
                    char s1[512], s2[512];
                    if (sscanf(str, "%s %s", s1, s2) != 2)
                    {
                        fprintf(stderr, "<%s:%d> Error sscanf(%s) != 2\n", __func__, __LINE__, str);
                        return -1;
                    }
                    create_fcgi_list(&c.fcgi_list, s1, s2, FASTCGI);
                }

                if (strcmp(str, "}"))
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }
            }
            else if (!strcmp(str, "scgi"))
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }

                while (getLine(fconf, str, sizeof(str) - 1) == 2)
                {
                    char s1[512], s2[512];
                    if (sscanf(str, "%s %s", s1, s2) != 2)
                    {
                        fprintf(stderr, "<%s:%d> Error sscanf(%s) != 2\n", __func__, __LINE__, str);
                        return -1;
                    }
                    create_fcgi_list(&c.fcgi_list, s1, s2, SCGI);
                }

                if (strcmp(str, "}"))
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line <%d>\n", __func__, __LINE__, line_);
                    return -1;
                }
            }
            else
            {
                fprintf(stderr, "<%s:%d> Error read config file: [%s] line <%d>\n", __func__, __LINE__, str, line_);
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "<%s:%d> Error read config file: [%s], line <%d>\n", __func__, __LINE__, str, line_);
            return -1;
        }
    }

    if (!feof(fconf))
    {
        fprintf(stderr, "<%s:%d> Error read config file\n", __func__, __LINE__);
        return -1;
    }

    fclose(fconf);
    //------------------------------------------------------------------
    if (absolute_path(c.LogPath) == -1)
    {
        fprintf(stderr, "<%s:%d> !!! Error LogPath [%s]\n", __func__, __LINE__, conf->LogPath.c_str());
        return -1;
    }
    //------------------------------------------------------------------
    if (absolute_path(c.ScriptPath) == -1)
    {
        c.ScriptPath = "";
        fprintf(stderr, "<%s:%d> !!! Error ScriptPath [%s]\n", __func__, __LINE__, conf->ScriptPath.c_str());
    }
    //------------------------------------------------------------------
    if (conf->MaxCgiProc <= 0)
    {
        fprintf(stderr, "<%s:%d> Error: MaxCgiProc=%d\n", __func__, __LINE__, conf->MaxCgiProc);
        return -1;
    }

    if (conf->MaxAcceptConnections <= 0)
    {
        fprintf(stderr, "<%s:%d> Error: MaxAcceptConnections=%d\n", __func__, __LINE__, conf->MaxAcceptConnections);
        return -1;
    }

    if (conf->MaxAcceptConnections <= conf->num_servers)
    {
        fprintf(stderr, "<%s:%d> Error: MaxAcceptConnections < num_servers\n", __func__, __LINE__);
        return -1;
    }

    if (conf->MaxCgiProc > conf->MaxAcceptConnections)
    {
        fprintf(stderr, "<%s:%d> Error: MaxCgiProc[%d] > MaxAcceptConnections[%d]\n", __func__, __LINE__, conf->MaxCgiProc, conf->MaxAcceptConnections);
        return -1;
    }
    //------------------------------------------------------------------
    const int fd_std = 3, fd_logs = 2, fd_sock = 1;
    long min_open_fd = fd_std + fd_logs + fd_sock;
    int max_fd = min_open_fd + conf->MaxAcceptConnections + conf->MaxAcceptConnections * conf->MaxConcurrentStreams + conf->MaxCgiProc * 2;
    n = set_max_fd(max_fd);
    if (n == -1)
        return -1;
    else if (n < max_fd)
    {
        fprintf(stderr, "<%s:%d> Error config file: max_open_fd=%d, max_fd=%d\n", __func__, __LINE__, n, max_fd);
        return -1;
    }
    //------------------------------------------------------------------
    if (absolute_path(c.DocumentRoot) == -1)
    {
        fprintf(stderr, "<%s:%d> !!! Error DocumentRoot [%s]\n", __func__, __LINE__, conf->DocumentRoot.c_str());
        return -1;
    }

    if (conf->servers_list)
    {
        Server *serv = conf->servers_list;
        for ( ; serv; serv = serv->next)
        {
            serv->sock = create_server_socket(serv->ip.c_str(), serv->port.c_str());
            if (serv->sock == -1)
            {
                return -1;
            }

            if (serv->redirect.size() && serv->SecureConnect)
            {
                fprintf(stderr, "<%s:%d> !!! Error host [%s:%s]: Redirect only from HTTP requests\n", __func__, __LINE__,
                                    serv->ip.c_str(), serv->port.c_str());
                return -1;
            }

            if ((serv->vhosts == NULL) && ((serv->redirect.size() == 0) || serv->SecureConnect))
            {
                fprintf(stderr, "<%s:%d> !!! Error create host for socket [%s:%s]\n", __func__, __LINE__,
                                    serv->ip.c_str(), serv->port.c_str());
                return -1;
            }

            VHost *h = serv->vhosts;
            for ( ; h; h = h->next)
            {
                if (absolute_path(h->DocumentRoot) == -1)
                {
                    fprintf(stderr, "<%s:%d> !!! Error DocumentRoot [%s], [%s]\n", __func__, __LINE__,
                                    h->DocumentRoot.c_str(), h->hostname.c_str());
                    return -1;
                }

                if (serv->SecureConnect)
                {
                    struct stat st;
                    if (lstat(h->Certificate.c_str(), &st) < 0)
                    {
                        fprintf(stderr, "<%s:%d> !!! Error Certificate file [%s] not found\n", __func__, __LINE__, h->Certificate.c_str());
                        return -1;
                    }

                    if (lstat(h->CertificateKey.c_str(), &st) < 0)
                    {
                        fprintf(stderr, "<%s:%d> !!! Error CertificateKey file [%s] not found\n", __func__, __LINE__, h->CertificateKey.c_str());
                        return -1;
                    }

                    if (serv->ctx == NULL)
                    {
                        serv->ctx = create_context(serv->vhosts);
                        if (serv->ctx == NULL)
                            return -1;
                        h->ctx = serv->ctx;
                        if (serv->EnableHTTP2)
                            SSL_CTX_set_alpn_select_cb(serv->ctx, alpn_select_proto_cb, NULL);
                        SSL_CTX_set_tlsext_servername_callback(serv->ctx, sni_callback);
                        SSL_CTX_set_tlsext_servername_arg(serv->ctx, serv->vhosts);

                        SSL *ssl = SSL_new(h->ctx);
                        if (ssl)
                        {
                            cout << "SSL version: " << SSL_get_version(ssl) << "\n";
                            SSL_free(ssl);
                        }
                    }
                    else
                    {
                        h->ctx = create_context(h);
                        if (h->ctx == NULL)
                            return -1;
                        if (serv->EnableHTTP2)
                            SSL_CTX_set_alpn_select_cb(h->ctx, alpn_select_proto_cb, NULL);
                    }
                }
            }
        }
    }

    return 0;
}
//======================================================================
int read_conf_file(const char *path_conf)
{
    FILE *fconf = fopen(path_conf, "r");
    if (!fconf)
    {
        if (errno == ENOENT)
        {
            fprintf(stderr, " Error: config file %s not found\n", path_conf);
            char s[8];
            printf(" Create config file? [y/n]: ");
            fflush(stdout);
            fgets(s, sizeof(s), stdin);
            if ((s[0] == 'y') || (s[0] == 'Y'))
            {
                create_conf_file(path_conf);
                fprintf(stderr, " Correct the configuration file %s\n", path_conf);
            }
        }
        else
            fprintf(stderr, "<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path_conf, strerror(errno));
        return -1;
    }

    return read_conf_file(fconf);
}
//======================================================================
int set_uid()
{
    uid_t uid = getuid();
    if (uid == 0)
    {
        char *p;
        c.server_uid = strtol(c.user.c_str(), &p, 0);
        if (*p == '\0')
        {
            struct passwd *passwdbuf = getpwuid(c.server_uid);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwuid(%u)=%p: %s\n", __func__, __LINE__, c.server_uid, passwdbuf, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct passwd *passwdbuf = getpwnam(c.user.c_str());
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwnam(%s)=%p: %s\n", __func__, __LINE__, c.user.c_str(), passwdbuf, strerror(errno));
                return -1;
            }
            c.server_uid = passwdbuf->pw_uid;
        }

        c.server_gid = strtol(c.group.c_str(), &p, 0);
        if (*p == '\0')
        {
            struct group *groupbuf = getgrgid(c.server_gid);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrgid(%u)=%p: %s\n", __func__, __LINE__, c.server_gid, groupbuf, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct group *groupbuf = getgrnam(c.group.c_str());
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrnam(%s)=%p: %s\n", __func__, __LINE__, c.group.c_str(), groupbuf, strerror(errno));
                return -1;
            }
            c.server_gid = groupbuf->gr_gid;
        }
        //--------------------------------------------------------------
        if (c.server_gid != getgid())
        {
            if (setgid(c.server_gid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setgid(%u): %s\n", __func__, __LINE__, c.server_gid, strerror(errno));
                return -1;
            }
        }

        if (c.server_uid != uid)
        {
            if (setuid(c.server_uid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setuid(%u): %s\n", __func__, __LINE__, c.server_uid, strerror(errno));
                return -1;
            }
        }
    }
    else
    {
        c.server_uid = getuid();
        c.server_gid = getgid();
    }

    return 0;
}
//======================================================================
static int set_max_fd(int max_open_fd)
{
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
    {
        fprintf(stderr, "<%s:%d> Error getrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    else
    {
        if (max_open_fd > (long)lim.rlim_cur)
        {
            if (max_open_fd > (long)lim.rlim_max)
                lim.rlim_cur = lim.rlim_max;
            else
                lim.rlim_cur = max_open_fd;

            if (setrlimit(RLIMIT_NOFILE, &lim) == -1)
                fprintf(stderr, "<%s:%d> Error setrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
            max_open_fd = sysconf(_SC_OPEN_MAX);
            if (max_open_fd < 0)
            {
                fprintf(stderr, "<%s:%d> Error sysconf(_SC_OPEN_MAX): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
            else
                fprintf(stdout, "<%s:%d> sysconf(_SC_OPEN_MAX): %d\n", __func__, __LINE__, max_open_fd);
        }
    }

    return max_open_fd;
}
//======================================================================
void free_fcgi_list()
{
    c.free_fcgi_list();
}
