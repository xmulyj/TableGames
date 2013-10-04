INCLUDE=-I/usr/include/easynet
LIBS=-leasynet -lpthread -llog4cplus
FLAGS=-g -D__LOG4CPLUS__

all:TestInterface

TestInterface:TestInterface.cpp
	g++ ${INCLUDE} $^ ${LIBS} ${FLAGS} -o $@

clean:
	rm TestInterface
