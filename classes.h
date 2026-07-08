#ifndef CLASSES_H_
#define CLASSES_H_
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include "bytes_array.h"
#include "globals.h"
//======================================================================
struct VHost
{
    VHost *next;

    std::string hostname;
    std::string DocumentRoot;

    SSL_CTX *ctx;

    std::string Certificate;
    std::string CertificateKey;

    VHost()
    {
        next = NULL;
        ctx = NULL;
    }
};

struct Server
{
    Server *next;
    std::string ip;
    std::string port;
    std::string redirect;
    int sock;
    bool SecureConnect;
    bool EnableHTTP2;
    SSL_CTX *ctx;

    VHost *vhosts;

    Server()
    {
        next = NULL;
        SecureConnect = EnableHTTP2 = false;
        ctx = NULL;
        vhosts = NULL;
    }
};

extern const char *static_tab[][2];

void print_err(const char *format, ...);
void dec_all_cgi();
//======================================================================
typedef struct fcgi_list_addr
{
    std::string script_name;
    std::string addr;
    CGI_TYPE type;
    struct fcgi_list_addr *next;
} fcgi_list_addr;
//----------------------------------------------------------------------
class Config
{
    Config(const Config&){}
    Config& operator=(const Config&);

public:

    bool PrintDebugMsg;

    std::string ServerSoftware;

    std::string DocumentRoot;
    std::string ScriptPath;
    std::string LogPath;

    std::string UsePHP;
    std::string PathPHP;

    int MaxConcurrentStreams;
    int HeaderTableSize;

    int ListenBacklog;
    bool TcpNoDelay;

    unsigned int HTTP1_DataBufSize;
    unsigned int HTTP2_RecvBufSize;

    int MaxAcceptConnections;

    int MaxRequestsPerClient;

    int MaxCgiProc;

    int Timeout;
    int TimeoutKeepAlive;
    int TimeoutPoll;
    int TimeoutCGI;

    long int ClientMaxBodySize;

    bool ShowMediaFiles;

    std::string user;
    std::string group;

    uid_t server_uid;
    gid_t server_gid;

    Server *servers_list;
    int num_servers;

    fcgi_list_addr *fcgi_list;
    //------------------------------------------------------------------
    Config()
    {
        fcgi_list = NULL;
        num_servers = 0;
        servers_list = NULL;

        MaxConcurrentStreams = 50;
        HeaderTableSize = 0;
    }

    ~Config()
    {
        free_fcgi_list();
        free_servers();
    }

    void free_fcgi_list()
    {
        fcgi_list_addr *t;
        while (fcgi_list)
        {
            t = fcgi_list;
            fcgi_list = fcgi_list->next;
            if (t)
                delete t;
        }
    }

    void free_servers()
    {
        Server *serv = servers_list;
        for ( ; serv; serv = servers_list)
        {
            VHost *h = serv->vhosts;
            for ( ; h; h = serv->vhosts)
            {
                serv->vhosts = h->next;
                delete h;
            }

            servers_list = serv->next;
            delete serv;
        }
    }

    Server *create_server()
    {
        Server *serv;
        try
        {
            serv = new Server;
        }
        catch (...)
        {
            fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            exit(errno);
        }

        num_servers++;
        return serv;
    }
};
//======================================================================
extern const Config* const conf;
//======================================================================
struct Stream
{
    Stream *prev;
    Stream *next;

    unsigned long numConn;
    unsigned long numReq;

    VHost *vhost;

    int id;
    HTTP2_FRAME_TYPE type;
    int flags;
    time_t Time;

    HTTP_METHOD httpMethod;

    std::string path;
    std::string decode_path;

    std::string query_string;
    std::string decode_query_string;

    char *clean_decode_path;
    int clean_decode_path_size;

    std::string host;
    std::string user_agent;
    std::string referer;
    std::string range;
    std::string sReqContentType;
    std::string sReqContentLen;

    BytesArray buf;
    BytesArray headers;
    BytesArray cgi_headers;
    BytesArray send_data;
    BytesArray post_data;
    BytesArray rst_stream;
    BytesArray frame_win_update;

    int resp_status;
    const char *resp_content_type;
    long long resp_content_len;
    long long post_content_len;
    long long file_size;
    long long offset;
    long stream_window_size;

    SOURCE_DATA source_data;
    int fd;

    long long send_bytes;

    bool recv_rst_stream;
    bool send_rst_stream;
    bool create_headers;
    bool send_headers;

    CGI_TYPE cgi_type;
    CGI_STATUS cgi_status;

    struct
    {
        bool start;
        bool end;

        time_t timer;

        pid_t pid;
        std::string path;
        int to_script;
        int from_script;

        const std::string *socket;
        int fd;
        int i_param;
        int size_par;
        int fcgi_type;
        int fcgiContentLen;
        int fcgiPaddingLen;
        //-----------------------
        long window_update;
        long window_size;
    } cgi;

    Stream()
    {
        vhost = NULL;
        init();
        numConn = 0;
        numReq = 1;
        id = 0;
        Time = time(NULL);
        clean_decode_path = NULL;
        clean_decode_path_size = 0;
        stream_window_size = 0;
        recv_rst_stream = send_rst_stream = create_headers = send_headers = false;
        cgi.window_update = 0;
        cgi.window_size = 65535;
    }

    ~Stream()
    {
        if (conf->PrintDebugMsg)
        {
            if ((send_bytes != file_size) && (source_data == FROM_FILE))
                print_err("<%s:%d> !!! ~Resp(%s), send_bytes=%lld(%lld), stream_window_size=%ld, id=%d \n",
                        __func__, __LINE__, clean_decode_path, send_bytes, file_size, stream_window_size, id);
        }

        if (clean_decode_path)
            delete [] clean_decode_path;

        if (fd > 0)
        {
            close(fd);
            fd = -1;
        }

        if (cgi.fd > 0)
        {
            close(cgi.fd);
            cgi.fd = 1;
        }

        if (cgi.to_script > 0)
        {
            close(cgi.to_script);
            cgi.to_script = -1;
        }

        if (cgi.from_script > 0)
        {
            close(cgi.from_script);
            cgi.from_script = -1;
        }

        if (cgi.start)
            dec_all_cgi();
    }

    void init()
    {
        ++numReq;
        path.clear();
        decode_path.clear();
        decode_query_string.clear();
        host.clear();
        user_agent.clear();
        referer.clear();
        range.clear();

        buf.init();
        send_data.init();
        post_data.init();
        headers.init();
        cgi_headers.init();

        httpMethod = M_NULL;
        sReqContentType.clear();
        sReqContentLen.clear();
        post_content_len = 0;

        Time = 0;

        send_headers = false;
        create_headers = false;
        resp_status = 0;

        source_data = NO_SOURCE;
        fd = -1;
        file_size = 0;
        offset = 0;
        send_bytes = 0;
        resp_content_len = -1;
        resp_content_type = NULL;

        cgi_status = NO_CGI;
        cgi.timer = 0;
        cgi.start = cgi.end = false;
        cgi.socket = NULL;
        cgi.fcgiContentLen = 0;
        cgi.fcgiPaddingLen = 0;
        cgi.fd = cgi.to_script = cgi.from_script = -1;
        cgi.fcgi_type = 0;
        cgi.fcgiContentLen = cgi.fcgiPaddingLen = 0;
    }

private:
    Stream(const Stream&);
    Stream& operator=(const Stream&);
};
//======================================================================
struct Header
{
    Header *prev;
    Header *next;
    int size;
    char *name;
    char *val;
};
//----------------------------------------------------------------------
class DynamicTable
{
    void *list_start;
    void *list_end;

    int max_table_size;
    int table_size;
    int headers_num;
    int offset;
    int offs_name;

    DynamicTable();
    DynamicTable(const DynamicTable&);
    DynamicTable& operator= (const DynamicTable&);

public:

    DynamicTable(int size, int offs)
    {
        list_start = list_end = NULL;
        max_table_size = size;
        table_size = 0;
        offset = offs;
        headers_num = 0;
        offs_name = sizeof(Header);

        if (conf->PrintDebugMsg)
            fprintf(stderr, "<%s:%d> table_size=%d, offset=%d\n", __func__, __LINE__, max_table_size, offset);
    }
    //------------------------------------------------------------------
    ~DynamicTable()
    {
        if (list_start)
        {
            //fprintf(stderr, "<%s:%d> ~~~ Delete Dynamic Table\n", __func__, __LINE__);
            Header *h = (Header*)list_start, *next = NULL;
            for ( ; h; h = next)
            {
                next = h->next;
                delete [] h;
            }

            list_start = list_end = NULL;
        }
    }
    //------------------------------------------------------------------
    int add(std::string& name, std::string& val);
    //------------------------------------------------------------------
    void print()
    {
        fprintf(stderr, " -------- Dynamic table %d, size %d --------\n", headers_num, table_size);
        Header *h = (Header*)list_start, *next = NULL;
        for ( int i = offset; h; h = next, ++i)
        {
            next = h->next;
            fprintf(stderr, " %04d: %d [%s: %s]\n", i, h->size, h->name, h->val);
        }
    }
    //------------------------------------------------------------------
    Header *get(int n)
    {
        if ((n < offset) || (n >= (headers_num + offset)))
        {
            fprintf(stderr, "<%s:%d> Error out of range: index=%d, table_len=%d, offset=%d\n", __func__, __LINE__, n, headers_num, offset);
            return NULL;
        }

        if (list_start)
        {
            Header *h = (Header*)list_start, *next = NULL;
            for ( int i = offset; h; h = next, ++i)
            {
                next = h->next;
                if (i == n)
                    return h;
            }
        }

        return NULL;
    }
};
//======================================================================
struct http1;
struct http2;

struct Socket
{
    Socket *prev;
    Socket *next;
    const Server *serv;
    int sock;
    int events;
    SSL *ssl;
    int err;
    PROTOCOL Protocol;
    bool SecureConnect;
    time_t timer;
};
//----------------------------------------------------------------------
struct Connect
{
    Connect *prev;
    Connect *next;

    unsigned long numConn;
    unsigned long numReq;

    int serverSocket;
    char remoteAddr[NI_MAXHOST];
    char remotePort[NI_MAXSERV];

    PROTOCOL Protocol;
    const Server *serv;
    bool SecureConnect;

    int err;
    std::string ServerPort;
    int clientSocket;
    time_t client_timer;
    int fd_revents;

    struct
    {
        SSL *ssl;
        int err;
        int poll_events;
        time_t shutdown_timer;
    } tls;

    http1 *h1;
    http2 *h2;
    //------------------------------------------------------------------
    Connect()
    {
        numReq = 1;
        err = 0;
        client_timer = 0;
        fd_revents = 0;
        tls.ssl = NULL;
        tls.err = 0;
        tls.poll_events = 0;
        h1 = NULL;
        h2 = NULL;
        serv = NULL;
        SecureConnect = false;
        remoteAddr[0] = 0;
        remotePort[0] = 0;
    }
    
    ~Connect()
    {
        //fprintf(stderr, "<%s:%d> ~~~ %lu ~~\n", __func__, __LINE__, numConn);
    }
};
//======================================================================
struct http1
{
    Connect c;

    HTTP1_STATUS con_status;

    Stream resp;
    BytesArray hdrs;
    CHUNK_MODE chunk_mode;
    bool connKeepAlive;
    bool try_again;
    //------------------------------------------------------------------
    http1()
    {
        connKeepAlive = true;
        hdrs.init();
        chunk_mode = NO_CHUNK;
        try_again = false;
    }
    //------------------------------------------------------------------
    void init()
    {
        resp.init();
        //----------------------
        hdrs.init();
        chunk_mode = NO_CHUNK;
    }
    //------------------------------------------------------------------
    const char *get_str_status()
    {
        switch (con_status)
        {
            case REDIRECT:
                return "REDIRECT";
            case READ_REQUEST:
                return "READ_REQUEST";
            case READ_POSTDATA:
                return "READ_POSTDATA";
            case SEND_RESP_HEADERS:
                return "SEND_RESP_HEADERS";
            case SEND_ENTITY:
                return "SEND_ENTITY";
            case HTTP1_SHUTDOWN:
                return "HTTP1_SHUTDOWN";
        }

        return "?";
    }
};
//======================================================================
struct http2
{
    Connect c;

    HTTP2_STATUS con_status;

    Stream *start_stream;
    Stream *end_stream;

    Stream *work_stream;
    //-----------------------
    unsigned int body_len;
    HTTP2_FRAME_TYPE type;
    int flags;
    int id;

    unsigned int HTTP2_SendBufSize;

    long init_window_size;
    long connect_window_size;
    long max_frame_size;
    long cgi_window_update;
    long cgi_window_size;

    char header[9];
    int header_len;

    BytesArray body;
    BytesArray goaway;
    BytesArray ping;
    BytesArray settings;
    BytesArray frame_win_update;

    bool try_again;
    struct
    {
        Stream *stream;
        int id;
        HTTP2_FRAME_TYPE type;
    } send_again;

    DynamicTable *dyn_tab;

    bool recv_settings;
    bool recv_settings_ack;
    bool send_settings_ack;
    //----------------------
    void init()
    {
        header_len = 0;
        body.init();
    }

    http2();
    ~http2();
    Stream *new_stream(unsigned long, unsigned long);
    void del_from_list(Stream *r);
    int close_stream(int id);
    int set_window_size(int id, long n);
    Stream *get(int id);
    Stream *get();
    int size();
    int get_str(std::string& s, int *len);
    int get_header(int ind, std::string& name, std::string& val, int *len);
    int parse(Stream *r);
    //------------------------------------------------------------------
    const char *get_str_status()
    {
        switch (con_status)
        {
            case PREFACE_MESSAGE:
                return "PREFACE_MESSAGE";
            case SET_SETTINGS:
                return "SET_SETTINGS";
            case PROCESSING_REQUESTS:
                return "PROCESSING_REQUESTS";
            case HTTP2_SHUTDOWN:
                return "HTTP2_SHUTDOWN";
        }

        return "?";
    }
    //------------------------------------------------------------------
private:

    int max_streams;    //SETTINGS_MAX_CONCURRENT_STREAMS (0x3)
    int num_streams;
    int err;

    http2(const http2&);
    http2& operator=(const http2&);
};
//======================================================================
class EventHandlerClass
{
    std::mutex mtx_thr;
    std::condition_variable cond_thr;

    int num_poll, cgi_num_work;
    int close_thr;
    unsigned long num_request;

    Connect **conn_array;
    struct pollfd *poll_fd;
    Stream **cgi_array;

    Connect *work_list_start;
    Connect *work_list_end;

    Connect *wait_list_start;
    Connect *wait_list_end;

    char *snd_buf;

    void del_from_list(Connect *c);
    void worker(Connect *c);

    int http1_worker(Connect *con, int revents);

    int http2_connection(Connect *c);

    int recv_frame(Connect *c);
    int recv_frame_(Connect *c);
    int parse_frame(Connect *c);

    void send_frames(Connect *c);
    int send_frames_(Connect *c);

    int send_frame_settings(Connect *c);
    int send_frame_headers(Connect *c, Stream *r);
    int send_frame_data(Connect *c, Stream *r);
    int send_window_update(Connect *c);
    int send_window_update(Connect *c, Stream *r);
    int send_frame_goawey(Connect *c);
    int send_frame_ping(Connect *c);
    int send_frame_rststream(Connect *c, Stream *r);

    void http1_set_poll(Connect *c);
    void http2_set_poll(Connect *c);
    int http2_poll(Connect *c, int);

    void cgi_worker(Connect *c, Stream *r, int i);
    int cgi_create_proc(Connect *c, Stream *r);
    int cgi_fork(Connect *c, Stream *r, int* serv_cgi, int* cgi_serv);
    int cgi_stdin(Stream *r, int fd);
    int cgi_stdout(Connect *c, Stream *r, int fd);

    void http1_get_cgi_headers(Connect *c);
    void http2_get_cgi_headers(Connect *c, Stream *r);

    void cgi_worker(Connect *c, int i);
    int cgi_stdout(Connect *c, int fd);

    int scgi_worker(Connect *c, Stream *r, int i);

    void fcgi_worker(Connect *c, Stream *r, int i);

    void fcgi_worker(Connect *c, int i);

    void http1_end_request(Connect *c);

    int http1_cgi_set(Connect *c);
    void http1_cgi_poll(Connect *c, int);
    int http2_cgi_set(Connect *c);
    void http2_cgi_poll(Connect *c, int);

    void ssl_shutdown(Connect *c);
    void close_connect(Connect *c);

public:

    EventHandlerClass();
    ~EventHandlerClass();

    void init();

    int wait_connection();
    void add_work_list();
    int cgi_poll();
    void set_poll();
    int _poll();

    void dec_all_cgi();
    void push_wait_list(Connect *c);
    void close_event_handler();
    void close_connections();
};

#endif
