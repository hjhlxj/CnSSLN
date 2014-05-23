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
			printf("sleeping...\n"),sleep(nsec);
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
	atexit((void (*)(void))WSACleanup);
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
			//if ((cret = connect_retry(socketfd, aip->ai_addr, aip->ai_addrlen)) < 0)
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
	int iret = checkConnectStateR(0);
	if (iret != TSSIG_DONE)
		return iret;
	auto fdret = FD_ISSET(socketfd, &rset);
	//printf("fd is: %d, fd_isset is: %d\n", socketfd, fdret);
	if (fdret)
	{
		iret = ::recv(socketfd, buf, buflen, 0);
		printf("iret = %d\n", iret);
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
	
	/*if (isConnected)
		return TSSIG_DONE;*/
	
	
	FD_ZERO(&wset);
	FD_SET(socketfd, &wset);
	//wset = rset;
	tval.tv_sec = 0;
	tval.tv_usec = timeoutus;
	
	if ( (n = select(socketfd + 1, NULL, &wset, NULL, &tval)) == 0)
	{
		//timeout
		++ntimeout;
		return TSSIG_TIMEOUT;
	}
	
	if (FD_ISSET(socketfd, &wset)) // || FD_ISSET(socketfd, &wset)) 
	{
		len = sizeof(error);
		if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, (char *)&error, &len) < 0)
		{
			printf("checkConnectionState: getsockopt failed with error %d\n", ERRNO);
			return TSSIG_ERROR;
		}
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

int TCPSocketAsync::checkConnectStateR(int timeoutus)
{
	int error = 0, n;
	socklen_t len;
	struct timeval tval;

	/*if (isConnected)
		return TSSIG_DONE;*/


	FD_ZERO(&rset);
	FD_SET(socketfd, &rset);
	//wset = rset;
	tval.tv_sec = 0;
	tval.tv_usec = timeoutus;

	if ((n = select(socketfd + 1, &rset, NULL, NULL, &tval)) == 0)
	{
		//timeout
		++ntimeout;
		return TSSIG_TIMEOUT;
	}

	if (FD_ISSET(socketfd, &rset))// || FD_ISSET(socketfd, &wset))
	{
		len = sizeof(error);
		if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, (char *)&error, &len) < 0)
		{
			printf("checkConnectionStateR: getsockopt failed with error %d\n", ERRNO);
			return TSSIG_ERROR;
		}
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

int TCPSocketAsync::getMacAddr(unsigned char *mac)
{
	return MACAddressUtility::GetMACAddress(mac);
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

long MACAddressUtility::GetMACAddress(unsigned char * result)
{
	// Fill result with zeroes
	memset(result, 0, 6);
	// Call appropriate function for each platform
#if defined(WIN32) || defined(UNDER_CE)
	return GetMACAddressMSW(result);
#elif defined(__APPLE__)
	return GetMACAddressMAC(result);
#elif defined(LINUX) || defined(linux)
	return GetMACAddressLinux(result);
#endif
	// If platform is not supported then return error code
	return -1;
}

#if defined(WIN32) || defined(UNDER_CE)

inline long MACAddressUtility::GetMACAddressMSW(unsigned char * result)
{

#if defined(UNDER_CE)
	IP_ADAPTER_INFO AdapterInfo[16]; // Allocate information
	DWORD dwBufLen = sizeof(AdapterInfo); // Save memory size of buffer
	if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_SUCCESS)
	{
		memcpy(result, AdapterInfo->Address, 6);
	}
	else return -1;
#else
	UUID uuid;
	if (UuidCreateSequential(&uuid) == RPC_S_UUID_NO_ADDRESS) return -1;
	memcpy(result, (char*)(uuid.Data4 + 2), 6);
#endif
	return 0;
}

#elif defined(__APPLE__)

static kern_return_t FindEthernetInterfaces(io_iterator_t *matchingServices)
{
	kern_return_t       kernResult;
	CFMutableDictionaryRef  matchingDict;
	CFMutableDictionaryRef  propertyMatchDict;

	matchingDict = IOServiceMatching(kIOEthernetInterfaceClass);

	if (NULL != matchingDict)
	{
		propertyMatchDict =
			CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);

		if (NULL != propertyMatchDict)
		{
			CFDictionarySetValue(propertyMatchDict,
				CFSTR(kIOPrimaryInterface), kCFBooleanTrue);
			CFDictionarySetValue(matchingDict,
				CFSTR(kIOPropertyMatchKey), propertyMatchDict);
			CFRelease(propertyMatchDict);
		}
	}
	kernResult = IOServiceGetMatchingServices(kIOMasterPortDefault,
		matchingDict, matchingServices);
	return kernResult;
}

static kern_return_t GetMACAddress(io_iterator_t intfIterator,
	UInt8 *MACAddress, UInt8 bufferSize)
{
	io_object_t     intfService;
	io_object_t     controllerService;
	kern_return_t   kernResult = KERN_FAILURE;

	if (bufferSize < kIOEthernetAddressSize) {
		return kernResult;
	}

	bzero(MACAddress, bufferSize);

	while (intfService = IOIteratorNext(intfIterator))
	{
		CFTypeRef   MACAddressAsCFData;

		// IONetworkControllers can't be found directly by the IOServiceGetMatchingServices call,
		// since they are hardware nubs and do not participate in driver matching. In other words,
		// registerService() is never called on them. So we've found the IONetworkInterface and will
		// get its parent controller by asking for it specifically.

		// IORegistryEntryGetParentEntry retains the returned object,
		// so release it when we're done with it.
		kernResult =
			IORegistryEntryGetParentEntry(intfService,
			kIOServicePlane,
			&controllerService);

		if (KERN_SUCCESS != kernResult) {
			printf("IORegistryEntryGetParentEntry returned 0x%08x\n", kernResult);
		}
		else {
			// Retrieve the MAC address property from the I/O Registry in the form of a CFData
			MACAddressAsCFData =
				IORegistryEntryCreateCFProperty(controllerService,
				CFSTR(kIOMACAddress),
				kCFAllocatorDefault,
				0);
			if (MACAddressAsCFData) {
				CFShow(MACAddressAsCFData); // for display purposes only; output goes to stderr

				// Get the raw bytes of the MAC address from the CFData
				CFDataGetBytes((CFDataRef)MACAddressAsCFData,
					CFRangeMake(0, kIOEthernetAddressSize), MACAddress);
				CFRelease(MACAddressAsCFData);
			}

			// Done with the parent Ethernet controller object so we release it.
			(void)IOObjectRelease(controllerService);
		}

		// Done with the Ethernet interface object so we release it.
		(void)IOObjectRelease(intfService);
	}

	return kernResult;
}

long MACAddressUtility::GetMACAddressMAC(unsigned char * result)
{
	io_iterator_t   intfIterator;
	kern_return_t   kernResult = KERN_FAILURE;
	do
	{
		kernResult = ::FindEthernetInterfaces(&intfIterator);
		if (KERN_SUCCESS != kernResult) break;
		kernResult = ::GetMACAddress(intfIterator, (UInt8*)result, 6);
	} while (false);
	(void)IOObjectRelease(intfIterator);
}

#elif defined(LINUX) || defined(linux)

long MACAddressUtility::GetMACAddressLinux(unsigned char * result)
{
	struct ifreq ifr;
	struct ifreq *IFR;
	struct ifconf ifc;
	char buf[1024];
	int s, i;
	int ok = 0;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
	{
		return -1;
	}

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	ioctl(s, SIOCGIFCONF, &ifc);

	IFR = ifc.ifc_req;
	for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; IFR++)
	{
		strcpy(ifr.ifr_name, IFR->ifr_name);
		if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0)
		{
			if (!(ifr.ifr_flags & IFF_LOOPBACK))
			{
				if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0)
				{
					ok = 1;
					break;
				}
			}
		}
	}

	shutdown(s, SHUT_RDWR);
	if (ok)
	{
		bcopy(ifr.ifr_hwaddr.sa_data, result, 6);
	}
	else
	{
		return -1;
	}
	return 0;
}

#endif