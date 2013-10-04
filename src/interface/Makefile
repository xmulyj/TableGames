INCLUDE=-I/usr/include/easynet
LIBS=-leasynet -lpthread -llog4cplus
FLAGS=-g -D__LOG4CPLUS__

all:bin/interface

bin/interface:*.cpp *.h
	g++ ${INCLUDE} $^ ${LIBS} ${FLAGS} -o $@

clean:
	rm bin/*
