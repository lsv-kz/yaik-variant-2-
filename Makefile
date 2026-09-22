CXXFLAGS = -Wall -g -O2  -std=c++11

CXX = c++
#CXX = clang++ 

DEPS = globals.h  main.h classes.h bytes_array.h

OBJS = yaik.o \
	accept_connect.o \
	huffman_code.o \
	cgi.o \
	scgi.o \
	fcgi.o \
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
	$(CXX) $(CXXFLAGS) -o $@  $(OBJS) -lpthread -lssl -lcrypto

yaik.o: yaik.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c yaik.cpp -o $@

huffman_code.o: huffman_code.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c huffman_code.cpp -o $@

cgi.o: cgi.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c cgi.cpp -o $@

scgi.o: scgi.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c scgi.cpp -o $@

fcgi.o: fcgi.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c fcgi.cpp -o $@

ssl.o: ssl.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c ssl.cpp -o $@

config.o: config.cpp  $(DEPS)
	$(CXX) $(CXXFLAGS) -c config.cpp -o $@

http1.o: http1.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c http1.cpp -o $@

http2.o: http2.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c http2.cpp -o $@

classes.o: classes.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c classes.cpp -o $@

accept_connect.o: accept_connect.cpp $(DEPS) 
	$(CXX) $(CXXFLAGS) -c accept_connect.cpp -o $@

event_handler.o: event_handler.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c event_handler.cpp -o $@

socket.o: socket.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c socket.cpp -o $@

percent_coding.o: percent_coding.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c percent_coding.cpp -o $@

functions.o: functions.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c functions.cpp -o $@

index.o: index.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c index.cpp -o $@

log.o: log.cpp  $(DEPS)
	$(CXX) $(CXXFLAGS) -c log.cpp -o $@

clean:
	rm -f yaik
	rm -f *.o
