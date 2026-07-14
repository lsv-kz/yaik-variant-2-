#ifndef SERVER_H_
#define SERVER_H_
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <climits>
#include <iomanip>
#include <vector>

#include <mutex>
#include <thread>
#include <condition_variable>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <sys/resource.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "classes.h"

//======================================================================
extern const Config* const conf;
//======================================================================
//--------------------- accept_connect.cpp -----------------------------
void decrement_num_conn();
void accept_connect();
//------------------------ socket.cpp-----------------------------------
int create_server_socket(const char *addr, const char *port);
int create_fcgi_socket(const char *);
int write_to_client(Connect *c, const char *buf, int len, int id);
int read_from_client(Connect *c, char *buf, int len);
int peek(Connect *c, char *buf, int len);
int socket_read_line(Connect *c);
int write_to_fcgi(int fd, const char *buf, int len);
int get_size_sock_buf(int domain, int optname, int type, int protocol);
//-------------------------- http1.cpp ---------------------------------
int create_response_headers(Connect *c);
int send_message(Connect *c, const char *msg);
int read_post_data(Connect *c);
const char *http1_status_response(int st);
//------------------------ http1_cgi.cpp -------------------------------
void kill_chld(pid_t pid);
const char *get_script_name(const char *name);
const char *base_name(const char *path);
int cgi_set_size_chunk(BytesArray *ba);
int cgi_parse_headers(Connect* c, Stream *resp, bool lower_case);
//------------------------ http1_fcgi.cpp ------------------------------
void fcgi_set_header(BytesArray* ba, unsigned char type);
void fcgi_set_header(char *s, unsigned char type, int dataLen);
int fcgi_create_connect(Connect *c, Stream *r);
//------------------------ http2_cgi.cpp -------------------------------
int is_cgi(Stream *resp);
//------------------------ http2_fcgi.cpp ------------------------------
int fcgi_create_params(Connect *c, Stream *r);
//------------------------ scgi.cpp-------------------------------
int scgi_create_connect(Connect *c, Stream *r);
//-------------------------- config.cpp --------------------------------
int read_conf_file(const char *path_conf);
void free_fcgi_list();
//------------------------- index_dir.cpp ------------------------------
int index_dir(Connect *c, const char *path, const char *uri, BytesArray *b);
//----------------------- percent_coding.cpp----------------------------
int encode(const char *s_in, char *s_out, int len_out);
int decode(const char *s_in, int len_in, std::string& s_out);
//------------------------- function.cpp -------------------------------
std::string get_time();
std::string get_time(time_t t);
std::string log_time();
std::string log_time(time_t t);

const char *strstr_case(const char * s1, const char *s2);
int strlcmp_case(const char *s1, const char *s2, int len);
int strcmp_case(const char *s1, const char *s2);

int bytes_to_int(unsigned char prefix, int pref_len, const char *s, int size, int *len);
int int_to_bytes(BytesArray& buf, int data, int pref_len, int mask);

HTTP_METHOD get_int_method(const char *s);
const char *get_str_method(int i);
const char *get_str_protocol(PROTOCOL p);
const char *get_str_frame_type(HTTP2_FRAME_TYPE);
const char *get_cgi_type(CGI_TYPE n);
const char *get_cgi_status(CGI_STATUS s);
const char *get_http2_error(int err);
const char *get_str_sourcedata(SOURCE_DATA n);
const char *content_type(const char *s);

int clean_path(char *path, int len);
long long file_size(const char *s);
SOURCE_DATA get_content_type(const char *path);
int parse_range(const char *s, long long file_size, long long *offset, long long *content_length);
void set_error_message(Connect *c, Stream *resp, int err);
void hex_print_stderr(const char *s, int line, const void *p, int n);
const char *http2_status_resonse(int st);
//--------------------------- http2.cpp --------------------------------
void set_frame_headers(Stream *r);
void set_frame_flags(BytesArray *ba, int flags);
void set_frame_data(Stream *resp, int len, int flag);
void add_header(Stream *r, int ind);
void add_header(BytesArray& ba, int ind, const char *val);
void add_header(BytesArray& ba, const char *name, const char *val);
void add_cgi_headers(Stream *r);
void set_frame_goaway(Connect *c, HTTP2_ERRORS error);
void set_rst_stream(Connect *c, Stream *resp, HTTP2_ERRORS error);
//--------------------------- log.cpp ----------------------------------
void create_logfiles(const std::string &);
void close_logs();
void print_err(const char *format, ...);
void print_err(Connect *c, const char *format, ...);
void print_err(Stream *r, const char *format, ...);
void print_log(Connect *c, Stream *r);
void print_log(Connect *c);
//----------------------- huffman_code.cpp -----------------------------
int huffman_encode(const char *s, BytesArray& out);
int huffman_decode(const char *s, int len, std::string& s_out);
//----------------------- event_handler.cpp ----------------------------
void push_wait_list(Connect *c);
void event_handler();
void close_event_handler();
//---------------------------- ssl.cpp ---------------------------------
void init_openssl();
int alpn_select_proto_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                                const unsigned char *in, unsigned int inlen, void *arg);
int sni_callback(SSL *ssl, int *al, void *arg);
SSL_CTX *create_context(VHost *vhost);
const char *ssl_strerror(int err);
int ssl_read(Connect *c, char *buf, int len);
int ssl_peek(Connect *c, char *buf, int len);
int ssl_write(Connect *c, const char *buf, int len, int id);
int ssl_accept(SecureAccept *s);
//----------------------------------------------------------------------


#endif
