#include "main.h"

using namespace std;
//======================================================================
static mutex mtx_num_conn;
static condition_variable cond_num_conn;

Socket *sockets_start;
Socket *sockets_end;

Socket *sock_struct_storage;

static int num_conn, num_sock_structs;

int create_secure_connect(Socket *s, unsigned long *allConn);

int create_nonsecure_connect(const Server *serv,
                    int clientSocket,
                    unsigned long *allConn);
static void close_connect(Socket *c);
static const int limit_number_sock_struct = 100;
//======================================================================
static void push_list(Socket *s)
{
    s->next = NULL;
    s->prev = sockets_end;
    if (sockets_end)
        sockets_end->next = s;
    sockets_end = s;
    if (!sockets_start)
        sockets_start = s;
}
//======================================================================
static void delete_from_list(Socket *s)
{
    if (s->prev)
        s->prev->next = s->next;
    else
        sockets_start = s->next;

    if (s->next)
        s->next->prev = s->prev;
    else
        sockets_end = s->prev;
    
    s->prev = NULL;
    s->next = sock_struct_storage;
    sock_struct_storage = s;
}
//======================================================================
void delete_sockets_list()
{
    if (sockets_start)
    {
        Socket *s = sockets_start, *next = NULL;
        for ( ; s; s = next)
        {
            next = s->next;
            delete s;
        }
    }

    if (sock_struct_storage)
    {
        Socket *s = sock_struct_storage, *next = NULL;
        for ( ; s; s = next)
        {
            next = s->next;
            delete s;
        }
    }
}
//======================================================================
static void start_conn()
{
mtx_num_conn.lock();
    ++num_conn;
mtx_num_conn.unlock();
}
//======================================================================
static Socket *create_sock_struct(int sock, const Server *serv)
{
    Socket *s;
    if (sock_struct_storage)
    {
        s = sock_struct_storage;
        sock_struct_storage = sock_struct_storage->next;
        if (sock_struct_storage)
            sock_struct_storage->prev = NULL;
    }
    else
    {
        s = new(nothrow) Socket;
        if (s == NULL)
            return NULL;
        ++num_sock_structs;
    }

    int flags = 1;
    if (ioctl(sock, FIONBIO, &flags) == -1)
    {
        print_err("<%s:%d> Error ioctl(, FIONBIO, 1): %s\n", __func__, __LINE__, strerror(errno));
        return NULL;
    }

    flags = fcntl(sock, F_GETFD);
    if (flags == -1)
    {
        print_err("<%s:%d> Error fcntl(, F_GETFD): %s\n", __func__, __LINE__, strerror(errno));
        return NULL;
    }

    flags |= FD_CLOEXEC;
    if (fcntl(sock, F_SETFD, flags) == -1)
    {
        print_err("<%s:%d> Error fcntl(, F_SETFD, FD_CLOEXEC): %s\n", __func__, __LINE__, strerror(errno));
        return NULL;
    }

    s->err = 0;
    s->ssl = SSL_new(serv->vhosts->ctx);
    if (s->ssl == NULL)
    {
        print_err("<%s:%d> Error SSL_new()\n", __func__, __LINE__);
        return NULL;
    }

    int ret = SSL_set_fd(s->ssl, sock);
    if (ret == 0)
    {
        s->err = SSL_get_error(s->ssl, ret);
        print_err("<%s:%d> Error SSL_set_fd(): %s\n", __func__, __LINE__, ssl_strerror(s->err));
        SSL_free(s->ssl);
        return NULL;
    }

    s->sock = sock;
    s->serv = serv;
    s->events = POLLIN | POLLOUT;
    s->SecureConnect = false;
    s->timer = time(NULL);
    start_conn();
    push_list(s);
    return s;
}
//======================================================================
void decrement_num_conn()
{
mtx_num_conn.lock();
    --num_conn;
mtx_num_conn.unlock();
    cond_num_conn.notify_one();
}
//======================================================================
static bool is_maxconn()
{
unique_lock<mutex> lk(mtx_num_conn);
    while ((num_conn >= (conf->MaxAcceptConnections - conf->num_servers)) && (sockets_start == NULL))
    {
        cond_num_conn.wait(lk);
    }

    if ((num_sock_structs >= limit_number_sock_struct) && (sock_struct_storage == NULL))
        return true;

    if (num_conn < (conf->MaxAcceptConnections - conf->num_servers))
        return false;
    else
        return true;
}
//======================================================================
void accept_connect()
{
    unsigned long allConn = 0;
    num_conn = 0;
    struct pollfd *poll_fd;

    poll_fd = new(nothrow) struct pollfd [conf->num_servers + conf->MaxAcceptConnections];
    if (!poll_fd)
    {
        print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
/*
    if (chdir(conf->DocumentRoot.c_str()))
    {
        print_err("<%s:%d> Error chdir(%s): %s\n", __func__, __LINE__, conf->DocumentRoot.c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }*/
    //------------------------------------------------------------------
    printf(" +++++ pid=%u, uid=%u, gid=%u +++++\n",
                                getpid(), getuid(), getgid());
    //------------------------------------------------------------------
    thread work_thr;
    try
    {
        work_thr = thread(event_handler);
    }
    catch (...)
    {
        print_err("<%s:%d> Error create thread(event_handler): errno=%d\n", __func__, __LINE__, errno);
        exit(errno);
    }
    //------------------------------------------------------------------
    bool run = true;
    int num_poll = 0;
    int poll_fd_index;
    int timeout = -1;

    const Server *serv = conf->servers_list;
    for ( num_poll = 0; serv; serv = serv->next, num_poll++)
    {
        poll_fd[num_poll].fd = serv->sock;
        poll_fd[num_poll].events = POLLIN;
    }

    if (num_poll != conf->num_servers)
    {
        run = false;
        print_err("<%s:%d> Error: num_poll != conf->num_servers\n", __func__, __LINE__);
        printf("<%s:%d> Error: num_poll != conf->num_servers\n", __func__, __LINE__);
    }

    while (run)
    {

        time_t timer = time(NULL);
        Socket *s = sockets_start, *next = NULL;
        for ( num_poll = conf->num_servers; s; s = next )
        {
            next = s->next;
            if ((timer - s->timer) >= 10)
            {
                print_err("<%s:%d> Error Timeout=%d sec\n", __func__, __LINE__, (int)(timer - s->timer));
                close_connect(s);
            }
            else
            {
                poll_fd[num_poll].fd = s->sock;
                poll_fd[num_poll].events = s->events;
                ++num_poll;
            }
        }

        if (is_maxconn())
        {
            poll_fd_index = conf->num_servers;
            num_poll -= poll_fd_index;
        }
        else
            poll_fd_index = 0;

        if (sockets_start)
            timeout = conf->TimeoutPoll;
        else
            timeout = -1;

        int ret_poll = poll(poll_fd + poll_fd_index, num_poll, timeout);
        if (ret_poll < 0)
        {
            print_err("<%s:%d> Error poll()=-1: %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            break;
        }
        else if (ret_poll == 0)
            continue;

        if (poll_fd_index == 0)
        {
            serv = conf->servers_list;
            for ( int num_ = 0; serv && (ret_poll > 0); serv = serv->next, num_++)
            {
                if (poll_fd[num_].fd != serv->sock)
                {
                    print_err("<%s:%d> Error server %d, socket (%d != %d)\n", __func__, __LINE__,
                                    num_, poll_fd[num_].fd, serv->sock);
                    run = false;
                    break;
                }

                if (poll_fd[num_].revents == POLLIN)
                {
                    --ret_poll;
                    int clientSocket = accept(serv->sock, NULL, NULL);
                    if (clientSocket == -1)
                    {
                        print_err("<%s:%d>  Error accept(%d): %s\n", __func__, __LINE__, serv->sock, strerror(errno));
                        if ((errno == EMFILE) || (errno == ENFILE)) // (errno == EINTR)
                        {
                            run = false;
                            break;
                        }

                        break;
                    }

                    if (serv->SecureConnect == false)
                    {
                        if (create_nonsecure_connect(serv, clientSocket, &allConn))
                        {
                            print_err("<%s:%d>  Error create_nonsecure_connect()\n", __func__, __LINE__);
                            shutdown(clientSocket, SHUT_RDWR);
                            close(clientSocket);
                        }
                    }
                    else
                    {
                        Socket *s = create_sock_struct(clientSocket, serv);
                        if (s == NULL)
                        {
                            run = false;
                            shutdown(clientSocket, SHUT_RDWR);
                            close(clientSocket);
                            break;
                        }
                    }
                }
                else if (poll_fd[num_].revents)
                {
                    print_err("<%s:%d> Error revents=0x%02X, num_=%d\n", __func__, __LINE__,
                                                    poll_fd[num_].revents, num_);
                    run = false;
                    break;
                }
            }
        }

        if (ret_poll <= 0)
            continue;
        s = sockets_start;
        next = NULL;
        for ( int i = conf->num_servers; s && (i < num_poll) && (ret_poll > 0); ++i, s = next )
        {
            next = s->next;
            int revents = poll_fd[i].revents;
            if ((s->SecureConnect == false) && (revents | POLLIN))
            {
                --ret_poll;
                char buf[16];
                int ret = recv(s->sock, buf, sizeof(buf) - 1, MSG_PEEK);
                if (ret > 0)
                {
                    buf[ret] = 0;
                    if (!strncmp(buf, "GET", 3) || !strncmp(buf, "POST", 4) || !strncmp(buf, "HEAD", 4))
                    {
                        if (s->ssl)
                        {
                            SSL_clear(s->ssl);
                            SSL_free(s->ssl);
                            s->ssl = NULL;
                        }

                        http1 *h1 = new(nothrow) http1;
                        if (h1)
                        {
                            Connect *c = &h1->c;
                            c->serv = s->serv;
                            c->clientSocket = s->sock;
                            c->ServerPort = s->serv->port;
                            c->serverSocket = s->serv->sock;
                            h1->resp.httpMethod = get_int_method(buf);
                            c->numConn = ++allConn;
                            c->Protocol = P_HTTP1;
                            c->h1 = h1;
                            c->h2 = NULL;
                            h1->resp.resp_status = RS400;
                            h1->connKeepAlive = false;
                            send_message(c, "The plain HTTP request was sent to HTTPS port");
                            push_wait_list(c);
                        }
                        else
                        {
                            print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
                        }

                        delete_from_list(s);
                        continue;
                    }
                    else
                    {
                        s->SecureConnect = true;
                    }
                }
                else if (ret < 0)
                {
                    //print_err("<%s:%d> 0x%02X read_from_client=%d, %s\n", __func__, __LINE__, poll_fd[i].revents, ret, strerror(errno));
                    continue;
                }
                else
                {
                    print_err("[%lu]<%s:%d> 0x%02X read_from_client=%d\n", allConn, __func__, __LINE__, revents, ret);
                    close_connect(s);
                    continue;
                }
            }

            if (revents & (POLLIN | POLLOUT))
            {
                --ret_poll;
                int ret = ssl_accept(s);
                if (ret == 1)
                {
                    int ret = create_secure_connect(s, &allConn);
                    delete_from_list(s);
                    if (ret < 0)
                    {
                        print_err("<%s:%d> Error Protocol: %s\n", __func__, __LINE__, get_str_protocol(s->Protocol));
                        close_connect(s);
                    }
                }
                else if (ret < 0)
                {
                    print_err("<%s:%d> Error ssl_accept()\n", __func__, __LINE__);
                    close_connect(s);
                }
            }
            else if (revents)
            {
                print_err("<%s:%d>  Error revents=0x%02X\n", __func__, __LINE__, revents);
                close_connect(s);
            }
        }
    }

    delete_sockets_list();
    close_event_handler();
    work_thr.join();
    print_err("<%s:%d> all_conn=%lu, open_conn=%d, %d\n", __func__, __LINE__, allConn, num_conn, num_sock_structs);
    if (poll_fd)
        delete [] poll_fd;
    usleep(100000);
}
//======================================================================
int create_secure_connect(Socket *s, unsigned long *allConn)
{
    ++(*allConn);
    Connect *c = NULL;
    if (s->Protocol == P_HTTP2)
    {
        http2 *h2 = new(nothrow) http2;
        if (h2)
        {
            h2->con_status = PREFACE_MESSAGE;
            c = &h2->c;
            c->h1 = NULL;
            c->h2 = h2;
        }
        else
        {
            print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }
    else if (s->Protocol == P_HTTP1)
    {
        http1 *h1 = new(nothrow) http1;
        if (h1)
        {
            h1->con_status = READ_REQUEST;
            h1->resp.numConn = *allConn;
            c = &h1->c;
            c->h2 = NULL;
            c->h1 = h1;
        }
        else
        {
            print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }

    c->numConn = *allConn;
    c->ServerPort = s->serv->port;
    c->serverSocket = s->serv->sock;
    c->clientSocket = s->sock;
    c->serv = s->serv;
    c->SecureConnect = true;
    c->Protocol = s->Protocol;
    c->tls.err = 0;
    c->tls.ssl = s->ssl;
    c->client_timer = 0;

    push_wait_list(c);

    return 0;
}
//======================================================================
int create_nonsecure_connect(const Server *serv,
                    int clientSocket,
                    unsigned long *allConn)
{
    http1 *h1 = new(nothrow) http1;
    if (h1 == NULL)
    {
        print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    Connect *c = &h1->c;
    c->numConn = ++(*allConn);
    if (serv->redirect.size())
        h1->con_status = REDIRECT;
    else
        h1->con_status = READ_REQUEST;
    h1->resp.numConn = c->numConn;

    int flags = 1;
    if (ioctl(clientSocket, FIONBIO, &flags) == -1)
    {
        print_err("<%s:%d> Error ioctl(, FIONBIO, 1): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    flags = fcntl(clientSocket, F_GETFD);
    if (flags == -1)
    {
        print_err("<%s:%d> Error fcntl(, F_GETFD): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    flags |= FD_CLOEXEC;
    if (fcntl(clientSocket, F_SETFD, flags) == -1)
    {
        print_err("<%s:%d> Error fcntl(, F_SETFD, FD_CLOEXEC): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    c->ServerPort = serv->port;
    c->serverSocket = serv->sock;
    c->clientSocket = clientSocket;
    c->serv = serv;
    c->SecureConnect = false;
    c->Protocol = P_HTTP1;
    c->h2 = NULL;
    c->h1 = h1;
    start_conn();
    push_wait_list(c);

    return 0;
}
//======================================================================
void close_connect(Socket *s)
{
    delete_from_list(s);
    if (s->ssl)
    {
        SSL_clear(s->ssl);
        SSL_free(s->ssl);
    }

    shutdown(s->sock, SHUT_RDWR);
    close(s->sock);

    decrement_num_conn();
}
