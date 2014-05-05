/*
This exmple program provides a trivial server program that listens for TCP
connections on port 9995.  When they arrive, it writes a short message to
each client connection, and closes each connection once it is flushed.

Where possible, it exits cleanly in response to a SIGINT (ctrl-c).
*/


#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <thread>
#include <unordered_map>
#include <memory>
#include <iostream>
#ifndef WIN32
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <sys/socket.h>
#include <boost/regex.hpp>
using namespace std;
#else
#include <regex>
#endif

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include "hiredis.h"

#ifdef WIN32
extern "C"{
#include "win32fixes.h" 
}
#endif

#include "TCPSocketAsync.h"

#define COMMAND_LEN_LOWERBOUND 6 //protoCode(2) + protoLen(4)

using namespace std;

static const char MESSAGE[] = "Hello, World!\n";

static const int PORT = 9995;

static void listener_cb(struct evconnlistener *, evutil_socket_t,
struct sockaddr *, int socklen, void *);
static void conn_writecb(struct bufferevent *, void *);
static void conn_readcb(struct bufferevent *, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void signal_cb(evutil_socket_t, short, void *);

namespace mobDev
{
	//The result struct
	class DBResultBase {
	public:
		virtual ~DBResultBase(){}
	};
	class RedisResult : public DBResultBase
	{
	public:
		RedisResult(const RedisResult&) = delete;
		RedisResult &operator=(const RedisResult&) = delete;
		RedisResult(redisReply *_p) : reply(_p) {}
		~RedisResult() {
			if (nullptr != reply)
				freeReplyObject(reply);
		}
		redisReply *reply;
	};
	class DBDelegate
	{
	public:
		DBDelegate() = default;
		DBDelegate(const DBDelegate&) = delete;
		DBDelegate &operator=(const DBDelegate&) = delete;
		virtual ~DBDelegate(){}
		virtual DBResultBase *doCommand(const char *cmd) = 0;
		virtual int cleanUpResult(DBResultBase *r) = 0;
	};
	class DBDelegateRedis : public DBDelegate
	{
	public:
		DBDelegateRedis(const DBDelegateRedis&) = delete;
		DBDelegateRedis &operator=(const DBDelegateRedis&) = delete;
		virtual ~DBDelegateRedis()
		{
			if (nullptr != reply) freeReplyObject(reply);
			if (nullptr != c) redisFree(c);
		}
		DBDelegateRedis(const char *_host = "127.0.0.1", int _port = 6379,
		struct  timeval _timeout = { 1, 500000 } /* 1.5 seconds */) :
			host(_host), port(_port), timeout(_timeout), c(nullptr), reply(nullptr)
		{
			c = redisConnectWithTimeout(_host, _port, _timeout);
			if (c->err)
				printf("Connection error: %s\n", c->errstr);
			else
			{
				/* PING server */
				reply = (redisReply *)redisCommand(c, "PING");
				printf("PING: %s\n", reply->str);
				//freeReplyObject(reply);
			}
		}
		DBResultBase *doCommand(const char *cmd) {
			if (nullptr != reply) freeReplyObject(reply);
			reply = (redisReply *)redisCommand(c, cmd);

			if (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_STATUS ||
				reply->type == REDIS_REPLY_ERROR)
			{
				string s(reply->str, reply->len);
				printf("Redis Reply: %s\n", s.c_str());
			}
			else if (reply->type == REDIS_REPLY_INTEGER)
				printf("Redis Reply Int: %d\n", reply->integer);
			else if (reply->type == REDIS_REPLY_NIL)
				printf("Redis Reply NIL");
			else if (reply->type == REDIS_REPLY_ARRAY)
				printf("Redis Reply Array: len %d\n", reply->len);

			return new RedisResult(reply);
		}
		redisReply *const getReply() const { return reply; }
		int cleanUpResult(DBResultBase *r)
		{
			if (auto p = dynamic_cast<RedisResult *>(r))
			{
				freeReplyObject(p->reply);
				return 0;
			}
			return -1;
		}
	private:
		redisContext *c;
		redisReply *reply;
		string host;
		int port;
		struct  timeval timeout;
	};
	static unique_ptr<DBDelegate> dbdelegate;

	enum ErrorCode : int
	{
		OK = 0,
		WrongNumberOfArguments,
		InvalidArguments,
		DBResultNIL,
		IncorrectArguments,
		UserAlreadyExist,
		UnknownError
	};

	class ProtoHandlerBase
	{
	public:
		ProtoHandlerBase() : e(ErrorCode::UnknownError) {}
		virtual ~ProtoHandlerBase() {};
		virtual ErrorCode doCommand(const char *paramBuf, int bufLen) = 0;
		virtual const char *serializeResult(char *dest, int buflen, int *resultLen) = 0;
		inline ErrorCode getErrorCode() const { return e; }
	protected:
		virtual void setErrorCode(ErrorCode e) { this->e = e; }
		virtual DBResultBase *execDBCommand(const char *cmd) { return dbdelegate->doCommand(cmd); }
		virtual int cleanUpDBResult(DBResultBase *dbr) { return dbdelegate->cleanUpResult(dbr); }
	private:
		ErrorCode e;
	};

	class ProtoHandlerLogin : public ProtoHandlerBase
	{
	public:
		ErrorCode doCommand(const char *paramBuf, int bufLen)
		{
			string dbcmd = parseAndGetCommand(paramBuf);
			if (ErrorCode::OK == getErrorCode())
			{
				auto dbret = execDBCommand(dbcmd.c_str());
				auto ecode = parseDBResult(dbret);
				setErrorCode(ecode);
				cleanUpDBResult(dbret);
			}
			return getErrorCode();
		}
		const char *serializeResult(char *dest, int buflen, int *resultLen)
		{
			//TODO: write result to client
			return dest;
		}
	protected:
		string parseAndGetCommand(const char *buf)
		{
			string sret;
			/*int len = byteToInt(&buf[2]), i = 0;*/
			//static regex pat(R"(([\w|\.|-]*@[\w|\.|-]+)\s([^\s]*))");
			static regex pat(R"((\+?\d+)\s([^\s]*))");
			cmatch cm;
			setErrorCode(ErrorCode::OK);
			if (regex_match(buf, cm, pat))
			{
				//HEXISTS user : abc@barfoo.com password
				sret.append("HGET user:");
				sret.append(cm[1].str().c_str());
				sret.append(" password ");
				sret.append(cm[2].str().c_str());
				
				userPassword = cm[2].str();
			}
			else
			{
				setErrorCode(ErrorCode::WrongNumberOfArguments);
			}
			return sret;
		}

		ErrorCode parseDBResult(const DBResultBase * dbret)
		{
			if (auto prr = dynamic_cast<const RedisResult *>(dbret))
			{
				if (REDIS_REPLY_NIL == prr->reply->type) return ErrorCode::DBResultNIL;
				string correctPwd(prr->reply->str, prr->reply->len);
				if (userPassword == correctPwd)
					return ErrorCode::OK;
				else return ErrorCode::IncorrectArguments;
			}
			return ErrorCode::UnknownError;
		}
	private:
		string userPassword;
	};

	class ProtoHandlerRegister : public ProtoHandlerBase
	{
	public:
		ErrorCode doCommand(const char *paramBuf, int bufLen)
		{
			string dbcmd = parseAndGetCommand(paramBuf);
			static char cmdbuf[2048];
			if (ErrorCode::OK == getErrorCode())
			{
				memcpy(cmdbuf, dbcmd.c_str(), dbcmd.length());
				cmdbuf[dbcmd.length()] = '\0';
				auto dbret = execDBCommand(cmdbuf);
				auto ecode = parseDBResult(dbret);
				setErrorCode(ecode);
				cleanUpDBResult(dbret);
			}
			return getErrorCode();
		}
		const char *serializeResult(char *dest, int buflen, int *resultLen)
		{
			//TODO: write result to client
			return dest;
		}
	protected:
		string parseAndGetCommand(const char *buf)
		{
			string sret;
			/*int len = byteToInt(&buf[2]), i = 0;*/
			//static regex pat(R"(([\w|\.|-]*@[\w|\.|-]+)\s([^\s]*))");
			static regex pat(R"((\+?\d+)\s([^\s]*)\s([^\s]*))");
			cmatch cm;
			setErrorCode(ErrorCode::OK);
			if (regex_match(buf, cm, pat))
			{
				auto pwd = cm[2].str();
				if (pwd == cm[3].str())
				{
					//evaluate the register lua script
					static const char *regScript = R"(evalsha 2f7df8a90ff08242ff87b597349b897e47af2d7d 1 )";
					/*R"(eval "if 0 == redis.pcall('HEXISTS', KEYS[1], 'password') then )"
							 R"(return redis.pcall('HMSET', 'user:'..KEYS[1], 'password', ARGV[1]) else return 'AE' end" 1 )";*/

					sret.append(regScript);
					sret.append("user:");
					sret.append(cm[1].str());
					sret.append(" ");
					sret.append(pwd);
				}
				else
				{
					setErrorCode(ErrorCode::InvalidArguments);
				}
			}
			else
			{
				setErrorCode(ErrorCode::WrongNumberOfArguments);
			}

			return sret;
		}

		ErrorCode parseDBResult(const DBResultBase * dbret)
		{
			if (auto prr = dynamic_cast<const RedisResult *>(dbret))
			{
				if (nullptr != prr->reply->str)
				{
					if (0 == strncmp("OK", prr->reply->str, 2)) return ErrorCode::OK;
					else if (0 == strncmp("AE", prr->reply->str, 2)) return ErrorCode::UserAlreadyExist;
				}
			}
			return ErrorCode::UnknownError;
		}
	};

	enum class RequestType : short
	{
		Login,
		Register,
		GPSDataUpload
	};

	class ProtoFactory
	{
	public:
		static ProtoFactory *getInstance() {
			if (nullptr == pInst)
				pInst = new ProtoFactory();
			return pInst;
		}
		ProtoFactory(const ProtoFactory&) = delete;
		ProtoFactory &operator=(const ProtoFactory&) = delete;

		virtual RequestType getProtoTypeFromStream(const char *buf);
		virtual ProtoHandlerBase *getProtoHandler(RequestType rt);

		virtual ~ProtoFactory() {
			if (nullptr != pInst)
				delete pInst;
		}
	private:
		ProtoFactory(){}
		static ProtoFactory *pInst;
	};

	RequestType ProtoFactory::getProtoTypeFromStream(const char *buf)
	{
		if (01 == buf[1] && 10 == buf[0])
			return RequestType::Login;
		else if (02 == buf[1] && 10 == buf[0])
			return RequestType::Register;
		else if (01 == buf[1] && 20 == buf[0])
			return RequestType::GPSDataUpload;
	}

	ProtoHandlerBase *ProtoFactory::getProtoHandler(RequestType rt)
	{
		switch (rt)
		{
		case mobDev::RequestType::Login:
			return new ProtoHandlerLogin();
			break;
		case mobDev::RequestType::Register:
			return new ProtoHandlerRegister();
			break;
		case mobDev::RequestType::GPSDataUpload:
			return nullptr;
			break;
		default:
			return nullptr;
			break;
		}			
	}

	ProtoFactory * ProtoFactory::pInst = nullptr;
}

using namespace mobDev;

int main()
{
	SecurityManager sm;
	char b[120];
	char a[120] = "data to encrypt";
	auto k = strlen(a);
	
	auto vl = sm.encrypt(a, k, b, 100);
	memset(a, 0, sizeof(a));
	vl = sm.decrypt(b, k, a, 100);
	cout << a;
	return 0;
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0201, &wsa_data);
	atexit((void(*)(void)) WSACleanup);
#endif
	char buf[1024], cmd[1024];
	unique_ptr<DBDelegate> _tptr(new DBDelegateRedis());
	dbdelegate = move(_tptr);
	//static char var[] = R"(eval 'if 0 == redis.pcall("HEXISTS", "user:"..KEYS[1], "password") then return redis.pcall("HMSET", "user:"..KEYS[1], "password", ARGV[1]) else return "AE" end' 1 abc@def.com 123456)";
	static char var[] = R"(evalsha 2f7df8a90ff08242ff87b597349b897e47af2d7d 1 user:abc@def.com 123456)";
	auto v = dbdelegate->doCommand(var);//R"(HGET user:abc@barfooa.com password)");
	return 0;
	//cout << dbret1;
	buf[0] = 10;
	buf[1] = 01;
	auto hdl = ProtoFactory::getInstance()->getProtoHandler(RequestType::Register);
	const char *content = "+8613933324563\t123456\t123456";
	auto r = hdl->doCommand(content, strlen(content));
	intToByte(strlen(content), &buf[2]);
	memcpy(&buf[7], content, strlen(content));
	int len = byteToInt(&buf[2]);
	buf[len + 7] = '\0';
	regex pat(R"(([\w|\.|-]*@[\w|\.|-]+)\t([^\s]*))");
	cmatch cm;
	//string s(&buf[7]);
	if (regex_match(&buf[7], cm, pat))
	{
		for (auto x : cm) 
			cout << x << '\t';
		sprintf(cmd, "HMSET user:%s password %s", cm[1].str().c_str(), cm[2].str().c_str());
		auto dbret = dbdelegate->doCommand(cmd);
		//printf("%s\n", dbret.c_str());
	}
}
#ifdef WIN32
int
mainax(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct event *signal_event;

	struct sockaddr_in sin;
	char buffer[1024];
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0201, &wsa_data);
#endif
	unique_ptr<DBDelegate> _tptr(new DBDelegateRedis());
	dbdelegate = move(_tptr);

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent!\n");
		return 1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);

	listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
		LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr*)&sin,
		sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener!\n");
		return 1;
	}
	else
	{
		const char *caddr = evutil_inet_ntop(AF_INET, &sin.sin_addr, buffer, sizeof(buffer));
		fprintf(stdout, "Server start at %s:%d\nLocal client"
			" may ping 127.0.0.1:%d...\n", caddr, PORT, PORT);
	}

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);

	if (!signal_event || event_add(signal_event, NULL) < 0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return 1;
	}

	event_base_dispatch(base);

	evconnlistener_free(listener);
	event_free(signal_event);
	event_base_free(base);

	printf("done\n");
	return 0;
}

static unordered_map<thread::id, thread *> threadpool;
static auto threadFuncAccept = [](evutil_socket_t fd,
struct sockaddr *sa, int socklen, void *user_data) -> void
{
	struct event_base *base = (struct event_base *)user_data;
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent!");
		event_base_loopbreak(base);
		return;
	}
	bufferevent_setcb(bev, conn_readcb, conn_writecb, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
	//bufferevent_disable(bev, EV_READ);
	const char *conmsg = "I can hear you now\n";
	//bufferevent_write(bev, MESSAGE, strlen(MESSAGE));
	bufferevent_write(bev, conmsg, strlen(conmsg));
};

static void
listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
struct sockaddr *sa, int socklen, void *user_data)
{
	struct event_base *base = (struct event_base *)user_data;
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent!");
		event_base_loopbreak(base);
		return;
	}
	bufferevent_setcb(bev, conn_readcb, conn_writecb, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
	//bufferevent_disable(bev, EV_READ);
	const char *conmsg = "I can hear you now\n";
	//bufferevent_write(bev, MESSAGE, strlen(MESSAGE));
	bufferevent_write(bev, conmsg, strlen(conmsg));
}

static void
conn_readcb(struct bufferevent *bev, void *user_data)
{
	/** TODO: to avoid server blocking, the operation in this
		function may need to put into a seperating thread.
		**/
	static const size_t BUFSIZE = 1024;
	char buf[BUFSIZE + 64];
	const char *pret = nullptr;
	//memset(buf, 0, sizeof(buf));
	int iRetCode = -999;
	int byteRead = bufferevent_read(bev, buf, BUFSIZE);

	//filter out invalid streams
	if (byteRead < COMMAND_LEN_LOWERBOUND)
	{
		printf("Invalid stream detected, discarding...\n");
		goto ret2client;
	}

	printf("Server Recv: \n");
	fwrite(buf, 1, byteRead, stdout);
	printf("\nServer Recv end\n");

	//TODO: decrypt incoming content:
	//StringEncryptor.decrypt(buf);

	int len = byteToInt(&buf[2]);
	if (len <=0 || byteRead < len + COMMAND_LEN_LOWERBOUND)
	{
		printf("Broken stream detected, discarding...\n");
		iRetCode = -998;
		goto ret2client;
	}
	buf[len + COMMAND_LEN_LOWERBOUND] = '\0';

	//Parse command, proto
	/**
		Proto format V1.0:
		B(byte) 1-2: proto id, 1001 for Login;
		B 3-6 (type int): proto content length in bytes, with proto id and length excluded;
		B 7- end of proto: proto content, fields may seperated by \t (tab)

		example: proto Login with email abc@foobar.com and password '123456'
		the proto string would expected to be
				10 01    21(int)    abc@foobar.com  (\t)	123456
		BYTE:   1  2     3-6        7        --------           27
		**/

	{	//#C2362 in VS2013
		iRetCode = -1;
		static auto ppf = ProtoFactory::getInstance();
		auto protoType = ppf->getProtoTypeFromStream(buf);
		unique_ptr<ProtoHandlerBase> ph(ppf->getProtoHandler(protoType));
		auto cmd = ph->doCommand(&buf[7], len);
		if (ErrorCode::OK == cmd)
		{
			pret = ph->serializeResult(buf, BUFSIZE, &len);
			//TODO: write the result to client
		}
		else
		{
			iRetCode = ph->getErrorCode();
		}
	}
	//switch (protoType)
	//{
	//case RequestType::GPSDataUpload:
	//	break;
	//case RequestType::Login:
	//{
	//	//do register
	//	int len = byteToInt(&buf[2]);
	//	buf[len + 7] = '\0';
	//	unique_ptr<ProtoHandlerBase> ph(ppf->getProtoHandler(RequestType::Login));
	//	auto cmd = ph->parseAndGetCommand(&buf[2]);
	//	if (cmd.size() > 0)
	//	{
	//		auto dbret = dbdelegate->doCommand(cmd.c_str());
	//		printf("%s\n", dbret.c_str());
	//		if (!strcmp("OK", dbret.c_str()))
	//			iRetCode = 0;
	//	}
	//}	
	//	break;
	//case RequestType::Register:
	//	break;
	//default:
	//	break;
	//}

ret2client:
	intToByte(iRetCode, buf);
	//TODO: encrypt outcoming content:
	//StringEncryptor.encrypt(buf);
	bufferevent_write(bev, buf, 4);
}

static void
conn_writecb(struct bufferevent *bev, void *user_data)
{
	struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
		printf("\nServer: flushed answer\n");
		//bufferevent_free(bev);
	}
}

static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	}
	else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on the connection: %s\n",
			strerror(errno));/*XXX win32*/
	}
	/* None of the other events can happen here, since we haven't enabled
	* timeouts */
	bufferevent_free(bev);
	/*auto this_id = std::this_thread::get_id();
	auto it = threadpool.find(this_id);
	threadpool.erase(it);*/
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = (struct event_base *)user_data;
	struct timeval delay = { 2, 0 };

	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

	event_base_loopexit(base, &delay);
}