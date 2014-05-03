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
#include <stdlib.h>
#include <thread>
#ifndef WIN32
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <sys/socket.h>
#else
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#endif

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/dns.h>

#include "TCPSocketAsync.h"

using namespace std;

static const char MESSAGE[] = "Hello, World!\n";

static const int PORT = 9995;

static void listener_cb(struct evconnlistener *, evutil_socket_t,
struct sockaddr *, int socklen, void *);
static void conn_writecb(struct bufferevent *, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void signal_cb(evutil_socket_t, short, void *);

typedef void(*evcbtype)(struct bufferevent *bev, short events, void *ptr);
typedef void(*rwcbtype)(struct bufferevent *bev, void *ptr);

int
#ifdef WIN32
main1(int argc, char **argv)
#else
main(int argc, char **argv)
#endif
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct event *signal_event;

	struct sockaddr_in sin;
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0201, &wsa_data);
#endif

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent!\n");
		return 1;
	}
	
	TCPSocketAsync tcps;
	const char *host = argc > 1 ? argv[1] : "127.0.0.1";
	const char *port = argc > 2 ? argv[2] : "9995";
	printf("Connecting to %s:%s...\n", host, port);
	tcps.connect(host, port);
	int timeRetry = 0;
	while (timeRetry < 3) {
		if (TCPSocketAsync::TSSIG_DONE == tcps.checkConnectState(500))
			break;
		++timeRetry;
	}
	if (timeRetry >= 3) {
		printf("Could not connect to server!");
		return 1;
	}
	int sfd = tcps.getSocketFd();
	struct  bufferevent *bev = bufferevent_socket_new(base, sfd, BEV_OPT_CLOSE_ON_FREE);
	struct evdns_base *dns_base;
	dns_base = evdns_base_new(base, 1);
	
	static bool bContinue = true;
	static thread htd([&](void *p) -> void {
		static char buf[1024];		
		while (bContinue)
		{
			printf("Type email and password to register: ");
			memset(buf, 0, sizeof(buf));
			fgets(&buf[7], 1000, stdin);
			buf[0] = 10;
			buf[1] = 01;
			int clen = strlen(&buf[7]);
			intToByte(clen, &buf[2]);
			if (TCPSocketAsync::TSSIG_ERROR == tcps.send(buf, clen + 6))
				printf("Could not send to server!\n");
		}
	}, nullptr);
	evcbtype eventcb = [](struct bufferevent *bev, short events, void *ptr) -> void {
		if (events & BEV_EVENT_CONNECTED) {
			/*printf("Connect okay.\n");
			char buf[1024];
			int n;
			struct evbuffer *output = bufferevent_get_output(bev);
			printf("Type what you want to send: ");
			memset(buf, 0, sizeof(buf));
			fgets(buf, 1000, stdin);
			if (evbuffer_add(output, buf, strlen(buf)))
				printf("Could not add to evbuffer!\n");*/
			//htd = new thread(ufunc, bev);
		}
		else if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
			struct event_base *base = (struct event_base *)ptr;
			if (events & BEV_EVENT_ERROR) {
				int err = bufferevent_socket_get_dns_error(bev);
				if (err)
					printf("DNS error: %s\n", evutil_gai_strerror(err));
			}
			printf("Closing\n");
			//bContinue = false;
			//htd.join();
			//delete htd;
			bufferevent_free(bev);
			event_base_loopexit(base, NULL);
		}
	};

	rwcbtype readcb = [](struct bufferevent *bev, void *ptr) -> void {
		char buf[1024];
		int n;
		struct evbuffer *input = bufferevent_get_input(bev);
		printf("\nData Received: ");
		while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
			printf("Server Return Code: %d", byteToInt(buf));
			//fwrite(buf, 1, n, stdout);
		}
		printf("\nEnd Of Data Received..\n");
	};

	bufferevent_setcb(bev, readcb, NULL, eventcb, base);
	int err =  bufferevent_enable(bev, EV_READ | EV_WRITE);
	if (err) printf("Set enable failed.\n");
	//err = bufferevent_socket_connect_hostname(bev, dns_base, AF_INET, argv[1], atoi(argv[2]));
	event_base_dispatch(base);
	bContinue = false;
	htd.join();
	/*
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

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);

	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return 1;
	}

	event_base_dispatch(base);

	evconnlistener_free(listener);
	event_free(signal_event);
	event_base_free(base);*/

	printf("done\n");
	return 0;
}

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
	bufferevent_setcb(bev, NULL, conn_writecb, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_WRITE);
	bufferevent_disable(bev, EV_READ);

	bufferevent_write(bev, MESSAGE, strlen(MESSAGE));
}

static void
conn_writecb(struct bufferevent *bev, void *user_data)
{
	struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
		printf("flushed answer\n");
		bufferevent_free(bev);
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
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = (struct event_base *)user_data;
	struct timeval delay = { 2, 0 };

	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

	event_base_loopexit(base, &delay);
}


///*
//A trivial static http webserver using Libevent's evhttp.
//
//This is not the best code in the world, and it does some fairly stupid stuff
//that you would never want to do in a production webserver. Caveat hackor!
//
//*/
//
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//
//#include <sys/types.h>
//#include <sys/stat.h>
//
//#ifdef WIN32
//#include <winsock2.h>
//#include <ws2tcpip.h>
//#include <windows.h>
//#include <io.h>
//#include <fcntl.h>
//#ifndef S_ISDIR
//#define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)
//#endif
//#else
//#include <sys/stat.h>
//#include <sys/socket.h>
//#include <signal.h>
//#include <fcntl.h>
//#include <unistd.h>
//#include <dirent.h>
//#endif
//
//#include <event2/event.h>
//#include <event2/http.h>
//#include <event2/buffer.h>
//#include <event2/util.h>
//#include <event2/keyvalq_struct.h>
//
//#ifdef _EVENT_HAVE_NETINET_IN_H
//#include <netinet/in.h>
//# ifdef _XOPEN_SOURCE_EXTENDED
//#  include <arpa/inet.h>
//# endif
//#endif
//
///* Compatibility for possible missing IPv6 declarations */
//#include "../util-internal.h"
//
//#ifdef WIN32
//#define stat _stat
//#define fstat _fstat
//#define open _open
//#define close _close
//#define O_RDONLY _O_RDONLY
//#endif
//
//char uri_root[512];
//
//static const struct table_entry {
//	const char *extension;
//	const char *content_type;
//} content_type_table[] = {
//	{ "txt", "text/plain" },
//	{ "c", "text/plain" },
//	{ "h", "text/plain" },
//	{ "html", "text/html" },
//	{ "htm", "text/htm" },
//	{ "css", "text/css" },
//	{ "gif", "image/gif" },
//	{ "jpg", "image/jpeg" },
//	{ "jpeg", "image/jpeg" },
//	{ "png", "image/png" },
//	{ "pdf", "application/pdf" },
//	{ "ps", "application/postsript" },
//	{ NULL, NULL },
//};
//
///* Try to guess a good content-type for 'path' */
//static const char *
//guess_content_type(const char *path)
//{
//	const char *last_period, *extension;
//	const struct table_entry *ent;
//	last_period = strrchr(path, '.');
//	if (!last_period || strchr(last_period, '/'))
//		goto not_found; /* no exension */
//	extension = last_period + 1;
//	for (ent = &content_type_table[0]; ent->extension; ++ent) {
//		if (!evutil_ascii_strcasecmp(ent->extension, extension))
//			return ent->content_type;
//	}
//
//not_found:
//	return "application/misc";
//}
//
///* Callback used for the /dump URI, and for every non-GET request:
//* dumps all information to stdout and gives back a trivial 200 ok */
//static void
//dump_request_cb(struct evhttp_request *req, void *arg)
//{
//	const char *cmdtype;
//	struct evkeyvalq *headers;
//	struct evkeyval *header;
//	struct evbuffer *buf;
//
//	switch (evhttp_request_get_command(req)) {
//	case EVHTTP_REQ_GET: cmdtype = "GET"; break;
//	case EVHTTP_REQ_POST: cmdtype = "POST"; break;
//	case EVHTTP_REQ_HEAD: cmdtype = "HEAD"; break;
//	case EVHTTP_REQ_PUT: cmdtype = "PUT"; break;
//	case EVHTTP_REQ_DELETE: cmdtype = "DELETE"; break;
//	case EVHTTP_REQ_OPTIONS: cmdtype = "OPTIONS"; break;
//	case EVHTTP_REQ_TRACE: cmdtype = "TRACE"; break;
//	case EVHTTP_REQ_CONNECT: cmdtype = "CONNECT"; break;
//	case EVHTTP_REQ_PATCH: cmdtype = "PATCH"; break;
//	default: cmdtype = "unknown"; break;
//	}
//
//	printf("Received a %s request for %s\nHeaders:\n",
//		cmdtype, evhttp_request_get_uri(req));
//
//	headers = evhttp_request_get_input_headers(req);
//	for (header = headers->tqh_first; header;
//		header = header->next.tqe_next) {
//		printf("  %s: %s\n", header->key, header->value);
//	}
//
//	buf = evhttp_request_get_input_buffer(req);
//	puts("Input data: <<<");
//	while (evbuffer_get_length(buf)) {
//		int n;
//		char cbuf[128];
//		n = evbuffer_remove(buf, cbuf, sizeof(buf)-1);
//		if (n > 0)
//			(void)fwrite(cbuf, 1, n, stdout);
//	}
//	puts(">>>");
//
//	evhttp_send_reply(req, 200, "OK", NULL);
//}
//
///* This callback gets invoked when we get any http request that doesn't match
//* any other callback.  Like any evhttp server callback, it has a simple job:
//* it must eventually call evhttp_send_error() or evhttp_send_reply().
//*/
//static void
//send_document_cb(struct evhttp_request *req, void *arg)
//{
//	struct evbuffer *evb = NULL;
//	const char *docroot = (const char *)arg;
//	const char *uri = evhttp_request_get_uri(req);
//	struct evhttp_uri *decoded = NULL;
//	const char *path;
//	char *decoded_path;
//	char *whole_path = NULL;
//	size_t len;
//	int fd = -1;
//	struct stat st;
//
//	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
//		dump_request_cb(req, arg);
//		return;
//	}
//
//	printf("Got a GET request for <%s>\n", uri);
//
//	/* Decode the URI */
//	decoded = evhttp_uri_parse(uri);
//	if (!decoded) {
//		printf("It's not a good URI. Sending BADREQUEST\n");
//		evhttp_send_error(req, HTTP_BADREQUEST, 0);
//		return;
//	}
//
//	/* Let's see what path the user asked for. */
//	path = evhttp_uri_get_path(decoded);
//	if (!path) path = "/";
//
//	/* We need to decode it, to see what path the user really wanted. */
//	decoded_path = evhttp_uridecode(path, 0, NULL);
//	if (decoded_path == NULL)
//		goto err;
//	/* Don't allow any ".."s in the path, to avoid exposing stuff outside
//	* of the docroot.  This test is both overzealous and underzealous:
//	* it forbids aceptable paths like "/this/one..here", but it doesn't
//	* do anything to prevent symlink following." */
//	if (strstr(decoded_path, ".."))
//		goto err;
//
//	len = strlen(decoded_path) + strlen(docroot) + 2;
//	if (!(whole_path = (char *)malloc(len))) {
//		perror("malloc");
//		goto err;
//	}
//	evutil_snprintf(whole_path, len, "%s/%s", docroot, decoded_path);
//
//	if (stat(whole_path, &st)<0) {
//		goto err;
//	}
//
//	/* This holds the content we're sending. */
//	evb = evbuffer_new();
//
//	if (S_ISDIR(st.st_mode)) {
//		/* If it's a directory, read the comments and make a little
//		* index page */
//#ifdef WIN32
//		HANDLE d;
//		WIN32_FIND_DATAA ent;
//		char *pattern;
//		size_t dirlen;
//#else
//		DIR *d;
//		struct dirent *ent;
//#endif
//		const char *trailing_slash = "";
//
//		if (!strlen(path) || path[strlen(path) - 1] != '/')
//			trailing_slash = "/";
//
//#ifdef WIN32
//		dirlen = strlen(whole_path);
//		pattern = (char *)malloc(dirlen + 3);
//		memcpy(pattern, whole_path, dirlen);
//		pattern[dirlen] = '\\';
//		pattern[dirlen + 1] = '*';
//		pattern[dirlen + 2] = '\0';
//		d = FindFirstFileA(pattern, &ent);
//		free(pattern);
//		if (d == INVALID_HANDLE_VALUE)
//			goto err;
//#else
//		if (!(d = opendir(whole_path)))
//			goto err;
//#endif
//
//		evbuffer_add_printf(evb, "<html>\n <head>\n"
//			"  <title>%s</title>\n"
//			"  <base href='%s%s%s'>\n"
//			" </head>\n"
//			" <body>\n"
//			"  <h1>%s</h1>\n"
//			"  <ul>\n",
//			decoded_path, /* XXX html-escape this. */
//			uri_root, path, /* XXX html-escape this? */
//			trailing_slash,
//			decoded_path /* XXX html-escape this */);
//#ifdef WIN32
//		do {
//			const char *name = ent.cFileName;
//#else
//		while ((ent = readdir(d))) {
//			const char *name = ent->d_name;
//#endif
//			evbuffer_add_printf(evb,
//				"    <li><a href=\"%s\">%s</a>\n",
//				name, name);/* XXX escape this */
//#ifdef WIN32
//		} while (FindNextFileA(d, &ent));
//#else
//		}
//#endif
//		evbuffer_add_printf(evb, "</ul></body></html>\n");
//#ifdef WIN32
//		CloseHandle(d);
//#else
//		closedir(d);
//#endif
//		evhttp_add_header(evhttp_request_get_output_headers(req),
//			"Content-Type", "text/html");
//	}
//	else {
//		/* Otherwise it's a file; add it to the buffer to get
//		* sent via sendfile */
//		const char *type = guess_content_type(decoded_path);
//		if ((fd = open(whole_path, O_RDONLY)) < 0) {
//			perror("open");
//			goto err;
//		}
//
//		if (fstat(fd, &st)<0) {
//			/* Make sure the length still matches, now that we
//			* opened the file :/ */
//			perror("fstat");
//			goto err;
//		}
//		evhttp_add_header(evhttp_request_get_output_headers(req),
//			"Content-Type", type);
//		evbuffer_add_file(evb, fd, 0, st.st_size);
//	}
//
//	evhttp_send_reply(req, 200, "OK", evb);
//	goto done;
//err:
//	evhttp_send_error(req, 404, "Document was not found");
//	if (fd >= 0)
//		close(fd);
//done:
//	if (decoded)
//		evhttp_uri_free(decoded);
//	if (decoded_path)
//		free(decoded_path);
//	if (whole_path)
//		free(whole_path);
//	if (evb)
//		evbuffer_free(evb);
//}
//
//static void
//syntax(void)
//{
//	fprintf(stdout, "Syntax: http-server <docroot>\n");
//}
//
//int
//main(int argc, char **argv)
//{
//	struct event_base *base;
//	struct evhttp *http;
//	struct evhttp_bound_socket *handle;
//
//	unsigned short port = 0;
//#ifdef WIN32
//	WSADATA WSAData;
//	WSAStartup(0x101, &WSAData);
//#else
//	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
//		return (1);
//#endif
//	if (argc < 2) {
//		syntax();
//		return 1;
//	}
//
//	base = event_base_new();
//	if (!base) {
//		fprintf(stderr, "Couldn't create an event_base: exiting\n");
//		return 1;
//	}
//
//	/* Create a new evhttp object to handle requests. */
//	http = evhttp_new(base);
//	if (!http) {
//		fprintf(stderr, "couldn't create evhttp. Exiting.\n");
//		return 1;
//	}
//
//	/* The /dump URI will dump all requests to stdout and say 200 ok. */
//	evhttp_set_cb(http, "/dump", dump_request_cb, NULL);
//
//	/* We want to accept arbitrary requests, so we need to set a "generic"
//	* cb.  We can also add callbacks for specific paths. */
//	evhttp_set_gencb(http, send_document_cb, argv[1]);
//
//	/* Now we tell the evhttp what port to listen on */
//	handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", port);
//	if (!handle) {
//		fprintf(stderr, "couldn't bind to port %d. Exiting.\n",
//			(int)port);
//		return 1;
//	}
//
//	{
//		/* Extract and display the address we're listening on. */
//		struct sockaddr_storage ss;
//		evutil_socket_t fd;
//		ev_socklen_t socklen = sizeof(ss);
//		char addrbuf[128];
//		void *inaddr;
//		const char *addr;
//		int got_port = -1;
//		fd = evhttp_bound_socket_get_fd(handle);
//		memset(&ss, 0, sizeof(ss));
//		if (getsockname(fd, (struct sockaddr *)&ss, &socklen)) {
//			perror("getsockname() failed");
//			return 1;
//		}
//		if (ss.ss_family == AF_INET) {
//			got_port = ntohs(((struct sockaddr_in*)&ss)->sin_port);
//			inaddr = &((struct sockaddr_in*)&ss)->sin_addr;
//		}
//		else if (ss.ss_family == AF_INET6) {
//			got_port = ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
//			inaddr = &((struct sockaddr_in6*)&ss)->sin6_addr;
//		}
//		else {
//			fprintf(stderr, "Weird address family %d\n",
//				ss.ss_family);
//			return 1;
//		}
//		addr = evutil_inet_ntop(ss.ss_family, inaddr, addrbuf,
//			sizeof(addrbuf));
//		if (addr) {
//			printf("Listening on %s:%d\n", addr, got_port);
//			evutil_snprintf(uri_root, sizeof(uri_root),
//				"http://%s:%d", addr, got_port);
//		}
//		else {
//			fprintf(stderr, "evutil_inet_ntop failed\n");
//			return 1;
//		}
//	}
//
//	event_base_dispatch(base);
//
//	return 0;
//}
