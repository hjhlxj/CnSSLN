LIBS = -levent -lhiredis
LPATH = -L3rd/libevent-2.0.21-stable/.libs -L3rd/redis/libs
IPATH = -I3rd/libevent-2.0.21-stable/include -I3rd/redis/include/hiredis
OBJCOMM = tcpasync.o
OBJS = setupTest.o
OBJC = setupTestc.o
SERVERNAME = server
CLIENTNAME = client
WARNINGS = -Wall -ggdb
main: ${OBJCOMM} setupTest.o
	g++ ${WARNINGS} -static -std=c++11 -o ${SERVERNAME} ${OBJCOMM} ${OBJS} ${LIBS} ${LPATH}
	g++ ${WARNINGS} -static -std=c++11 -o ${CLIENTNAME} ${OBJCOMM} ${OBJC} ${LIBS} ${LPATH}
clean:
	rm -f ${SERVERNAME} ${CLIENTNAME} ${OBJS} ${OBJCOMM}
setupTest.o: ./projMob/setupTest.cpp
	g++ ${WARNINGS} -c -std=c++11 ./projMob/setupTest.cpp -o ${OBJS} ${IPATH}
	g++ ${WARNINGS} -c -std=c++11 ./projMob/SetuptestClient.cpp -o ${OBJC} ${IPATH}
${OBJCOMM}: ./projMob/TCPSocketAsync.cpp
	g++ ${WARNINGS} -c -std=c++11 ./projMob/TCPSocketAsync.cpp -o tcpasync.o ${IPATH}