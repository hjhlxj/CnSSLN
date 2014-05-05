#include "TCPSocketAsync.h"

#include <stdio.h>


#define MAXADDRLEN 256
#define MAXSLEEP 128
#define BUFLEN 256

using namespace std;

u_long __anoymous_iMode = 1;

static int connect_nonb(int sockfd, const struct sockaddr *addr, socklen_t alen)
{
	int n, error;
	int flags = SET_SOCKET_NONBLOCK(sockfd);
	error = 0;
	
	if ((n = connect(sockfd, addr, alen)) < 0)
	{
#ifdef WIN32
		if (n == SOCKET_ERROR && ERRNO != NEWOULDBLOCK)
#else 
		if (ERRNO != NEINPROGRESS)
#endif  
		{
			if (checkFailed(ERRNO))
				printf("error: %s\n", gai_strerror(ERRNO));
			return -1;
		}
	}
	if (n == 0)
		return 0;	
	
	return 1;
}

static int connect_retry(int sockfd, const struct sockaddr *addr, socklen_t alen)
{
	int nsec;
	
	for (nsec = 1; nsec <= MAXSLEEP; nsec <<= 1) {
		if (connect(sockfd, addr, alen) == 0) {
			return 0;
		}
		
		if (nsec <= MAXSLEEP / 2)
			sleep(nsec);
	}
	return -1;
}

TCPSocketAsync::TCPSocketAsync(void)
{
	isConnected = false;
	ntimeout = 0;
}


TCPSocketAsync::~TCPSocketAsync(void)
{
	disconnect();
}

int TCPSocketAsync::disconnect()
{
	int ret = -1;
	if (!socketfd)
		ret = close(socketfd);
#ifdef WIN32
	WSACleanup();
#endif
	return ret;
}

int TCPSocketAsync::connect(const char *host, const char *service, int timeoutns)
{
	struct addrinfo *ailist, *aip;
	struct addrinfo hint;
	int err, cret;
	
	this->hostName = host;
	this->serviceName = service;
	this->timeout = timeoutns;
	
#ifdef WIN32
	int iResult;
	WSADATA wsaData;

	// Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
#endif

	hint.ai_flags = 0;
	hint.ai_family = 0;
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_protocol = 0;
	hint.ai_addrlen = 0;
	hint.ai_canonname = NULL;
	hint.ai_addr = NULL;
	hint.ai_next = NULL;
	
	if ((err = getaddrinfo(host, service, &hint, &ailist)) != 0)
		printf("getaddrinfo error: %s", gai_strerror(err));
	else
	{
		for (aip = ailist; aip != NULL; aip = aip->ai_next) {
			if ((socketfd = socket(aip->ai_family, SOCK_STREAM, 0)) < 0)
				err = ERRNO;
			if ((cret = connect_nonb(socketfd, aip->ai_addr, aip->ai_addrlen)) < 0) 
				err = ERRNO;
			else 
			{
				if (cret == 0)
					isConnected = true;
				return 0;			
			}
		}
	}
	printf("can't connect to server: %s\n", strerror(err));
	return err;
}

int TCPSocketAsync::send(const char *content, int len)
{
	int iret = checkConnectState(0);
	if (iret != TSSIG_DONE)
		return TSSIG_ERROR;
		
	if (FD_ISSET(socketfd, &wset))
	{
		iret = ::send(socketfd, content, len, 0);
		if (checkFailed(iret) && ERRNO != NEWOULDBLOCK)
		{
			printf("can't send to server: %s\n", strerror(iret = ERRNO));
			return TSSIG_ERROR;
		}
		else if (ERRNO == NEWOULDBLOCK)
			return TSSIG_TIMEOUT;
	}
	else
	{
		printf("socket does not set as writable.\n");
		return TSSIG_ERROR;
	}
	
	return iret;
}

int TCPSocketAsync::recv(char *buf, int buflen)
{
	int iret = checkConnectState(0);
	if (iret != TSSIG_DONE)
		return iret;
		
	if (FD_ISSET(socketfd, &rset))
	{
		iret = ::recv(socketfd, buf, buflen, 0);
		if (checkFailed(iret) && ERRNO != NEWOULDBLOCK)
		{
			printf("can't recv from server: %s\n", strerror(iret = ERRNO));
			return TSSIG_ERROR;
		}
		else if (ERRNO == NEWOULDBLOCK)
			return TSSIG_TIMEOUT;
	}
	else
	{
		printf("socket does not set as readable.\n");
		return TSSIG_ERROR;
	}
	
	return iret;
}

int TCPSocketAsync::checkConnectState(int timeoutus)
{
	int error = 0, n;
	socklen_t len;
	struct timeval tval;
	
	if (isConnected)
		return TSSIG_DONE;
	
	
	FD_ZERO(&rset);
	FD_SET(socketfd, &rset);
	wset = rset;
	tval.tv_sec = 0;
	tval.tv_usec = timeoutus;
	
	if ( (n = select(socketfd + 1, &rset, &wset, NULL, &tval)) == 0)
	{
		//timeout
		++ntimeout;
		return TSSIG_TIMEOUT;
	}
	
	if (FD_ISSET(socketfd, &rset) || FD_ISSET(socketfd, &wset)) 
	{
		len = sizeof(error);
		if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, (char *)&error, &len) < 0)
			return TSSIG_ERROR;
		else
		{
			//connect success
			isConnected = true;
			return TSSIG_DONE;
		}
	}
	
	if (error)	// just in case
	{
		close(socketfd);
	}
		
	return TSSIG_ERROR;
}


int byteToInt(const char *str)
{
	int ret = 0;
	for (int i = 0; i < 4; ++i)
	{
		ret <<= 8 * i;
		ret |= str[i] & 0xff;
	}
	return ret;
}

char *intToByte(int number, char *str)
{
	for (int i = 0; i < 4; ++i)
		str[3 - i] = (number >> (8 * i)) & 0xff;

	return str;
};

int SecurityManager::encrypt(const char *src, int srcLen, char *dest, int destLen)
{
	/*
		Naive implementation of encryption algorithm.
			For odd index:
				1. Take one's complement;
				2. Left shift 2 bits;
				3. Exclusive OR dest[0]
			For even index:
				1. Exclusive OR 0x7c
				2. Right shift 3 bits;
	*/
	int i, maxLen = srcLen < destLen ? srcLen : destLen;
	char xorkey = getXORKey();
	for (i = 0; i < maxLen; i += 2)
	{
		dest[i] = src[i] ^ xorkey;
		dest[i] = (dest[i] >> 3 & 0x1f) | (dest[i] << 5 & 0xE0);
	}
	for (i = 1; i < maxLen; i += 2)
	{
		dest[i] = ~src[i];
		dest[i] = (dest[i] << 2 & 0xfc) | (dest[i] >> 6 & 0x03);
		dest[i] ^= dest[0];
	}

	return maxLen;
}

int SecurityManager::decrypt(const char *src, int srcLen, char *dest, int destLen)
{
	/*
		Naive implementation of the decryption algorithm
		Reverse the encrypt steps.
	*/
	int i, maxLen = srcLen < destLen ? srcLen : destLen;
	char xorkey = getXORKey();
	for (i = 1; i < maxLen; i += 2)
	{
		dest[i] = src[i] ^ src[0];
		dest[i] = (dest[i] >> 2 & 0x3f) | (dest[i] << 6 & 0xC0);
		dest[i] = ~dest[i];
	}
	for (i = 0; i < maxLen; i += 2)
	{
		dest[i] = (src[i] << 3 & 0xf8) | (src[i] >> 5 & 0x07);
		dest[i] ^= xorkey;
	}	

	return maxLen;
}