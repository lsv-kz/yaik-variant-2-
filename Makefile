CFLAGS = -Wall -O2  -std=c++11

CC = c++
#CC = clang++ 
# -g

DEPS = globals.h  main.h classes.h bytes_array.h

OBJS = yaik.o \
	accept_connect.o \
	huffman_code.o \
	http1_cgi.o \
	http1_fcgi.o \
	http2_cgi.o \
	scgi.o \
	http2_fcgi.o \
	http1.o \
	http2.o \
	event_handler.o \
	config.o \
	functions.o \
	classes.o \
	ssl.o \
	socket.o \
	percent_coding.o \
	index.o \
	log.o \

yaik: $(OBJS)
	$(CC) $(CFLAGS) -o $@  $(OBJS) -lpthread -L/usr/local/lib/ -L/usr/local/lib64/ -lssl -lcrypto

yaik.o: yaik.cpp $(DEPS)
	$(CC) $(CFLAGS) -c yaik.cpp -o $@

huffman_code.o: huffman_code.cpp $(DEPS)
	$(CC) $(CFLAGS) -c huffman_code.cpp -o $@

http1_fcgi.o: http1_fcgi.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http1_fcgi.cpp -o $@

http2_fcgi.o: http2_fcgi.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http2_fcgi.cpp -o $@

ssl.o: ssl.cpp $(DEPS)
	$(CC) $(CFLAGS) -c ssl.cpp -o $@

scgi.o: scgi.cpp $(DEPS)
	$(CC) $(CFLAGS) -c scgi.cpp -o $@

http2_cgi.o: http2_cgi.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http2_cgi.cpp -o $@

http1_cgi.o: http1_cgi.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http1_cgi.cpp -o $@

config.o: config.cpp  $(DEPS)
	$(CC) $(CFLAGS) -c config.cpp -o $@

http1.o: http1.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http1.cpp -o $@

http2.o: http2.cpp $(DEPS)
	$(CC) $(CFLAGS) -c http2.cpp -o $@

classes.o: classes.cpp $(DEPS)
	$(CC) $(CFLAGS) -c classes.cpp -o $@

accept_connect.o: accept_connect.cpp $(DEPS) 
	$(CC) $(CFLAGS) -c accept_connect.cpp -o $@

event_handler.o: event_handler.cpp $(DEPS)
	$(CC) $(CFLAGS) -c event_handler.cpp -o $@

socket.o: socket.cpp $(DEPS)
	$(CC) $(CFLAGS) -c socket.cpp -o $@

percent_coding.o: percent_coding.cpp $(DEPS)
	$(CC) $(CFLAGS) -c percent_coding.cpp -o $@

functions.o: functions.cpp $(DEPS)
	$(CC) $(CFLAGS) -c functions.cpp -o $@

index.o: index.cpp $(DEPS)
	$(CC) $(CFLAGS) -c index.cpp -o $@

log.o: log.cpp  $(DEPS)
	$(CC) $(CFLAGS) -c log.cpp -o $@

clean:
	rm -f yaik
	rm -f *.o
