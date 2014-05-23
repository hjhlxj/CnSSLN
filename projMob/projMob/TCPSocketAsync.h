#pragma once

#ifdef WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment( lib, "rpcrt4.lib" )

extern u_long __anoymous_iMode;

#define sleep(t) Sleep(t * 1000)
#define ERRNO WSAGetLastError()
#define close closesocket
#define checkFailed(ret) (ret == SOCKET_ERROR)
#define SET_SOCKET_NONBLOCK(fd) (ioctlsocket(fd, FIONBIO, &__anoymous_iMode))

#define NEWOULDBLOCK	WSAEWOULDBLOCK
#define NEINPROGRESS	WSAEINPROGRESS

typedef int socklen_t;

#else
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

#define INVALID_SOCKET -1
#define checkFailed(ret) (ret < 0)
#define ERRNO errno
#define NEWOULDBLOCK	EWOULDBLOCK
#define NEINPROGRESS	EINPROGRESS
#define SET_SOCKET_NONBLOCK(sockfd) (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK))
#endif

#include <string>
#include <cstring>

class SecurityManager
{
public:
	/** Encrypt a string src, with length srcLen, the encryt string will be stored
		in dest, with max length specified by destMaxLen
	*/
	virtual int encrypt(const char *src, int srcLen, char *dest, int destMaxLen);
	/** Decrypt a string src, with length srcLen, the encryt string will be stored
	in dest, with max length specified by destMaxLen
	*/
	virtual int decrypt(const char *src, int srcLen, char *dest, int destMaxLen);
protected:
	char getXORKey() const { return 0x7c; }
};

class MACAddressUtility
{
public:
	static long GetMACAddress(unsigned char * result);
private:
#if defined(WIN32) || defined(UNDER_CE)
	static long GetMACAddressMSW(unsigned char * result);
#elif defined(__APPLE__)
	static long GetMACAddressMAC(unsigned char * result);
#elif defined(LINUX) || defined(linux)
	static long GetMACAddressLinux(unsigned char * result);
#endif
};

class TCPSocketAsync
{
public:
enum TCPSocketAsync_SIGNAL
{
	TSSIG_DONE = 0,
	TSSIG_TIMEOUT = -2,
	TSSIG_ERROR = -1,
}; 
public:
	TCPSocketAsync(void);
	~TCPSocketAsync(void);
	int disconnect(); 
	int connect(const char *host, const char *service, int timeoutns = 500);
	int send(const char *content, int len);
	int recv(char *buf, int buflen);
	int checkConnectState(int timeoutus = 0);
	int getSocketFd() const { return this->socketfd; }
	static int getMacAddr(unsigned char *buf);
private:
	int checkConnectStateR(int timeoutus = 0);
	int socketfd;
	bool isConnected;
	std::string hostName;
	std::string serviceName;
	fd_set rset, wset;
	int timeout;
	int ntimeout;
	SecurityManager sm;
};

//utility functions
int byteToInt(const char *str);

char *intToByte(int number, char *str);
