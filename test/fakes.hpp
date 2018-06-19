#ifndef FAKES_H__
#define FAKES_H__


#include <re.h>

#include <iostream>
#include <memory>
#include <map>


struct User {
	User(const std::string &email_, const std::string &password_)
		: email(email_)
		, password(password_)
	{}
	std::string email;
	std::string password;
};


struct Token {

public:
	Token(unsigned expires_in_, const std::string &access_token_)
		: expires_in(expires_in_)
		, access_token(access_token_)
		, token_type("Bearer")
	{
	}

	~Token()
	{
	}

public:
	unsigned expires_in; /* seconds */
	std::string access_token;
	std::string token_type;
};


class FakeBackend {

public:
	void init();

	FakeBackend();
	~FakeBackend();

	static void http_req_handler(struct http_conn *conn,
				     const struct http_msg *msg,
				     void *arg)
	{
		FakeBackend *backend = static_cast<FakeBackend *>(arg);

		backend->handleRequest(conn, msg);
	}

	void handleRequest(struct http_conn *conn, const struct http_msg *msg);
	void handle_login(struct http_conn *conn, const struct odict *o);
	void handle_await(struct http_conn *conn, const struct http_msg *msg);
	void handle_self(struct http_conn *conn, const struct http_msg *msg);
	void handle_users(struct http_conn *conn, const struct http_msg *msg,
			  const struct pl *userid);
	int  handle_fragment_test(struct http_conn *conn,
				  const struct http_msg *msg);
	void handle_get_clients(struct http_conn *conn,
				const struct http_msg *msg);
	void handle_post_clients(struct http_conn *conn,
				const struct http_msg *msg);

	int simulate_message(const char *content);

	void addUser(const std::string &email, const std::string &password)
	{
		users[email] = std::make_shared<User>(email, password);		
	}

	User *findUser(const std::string &email)
	{
		std::map<std::string, std::shared_ptr<User> >::iterator it;

		it = users.find(email);

		if (it != users.end()) {
			return it->second.get();
		}

		return NULL;
	}	

	Token *addToken(unsigned expires_in, const std::string &access_token)
	{
		tokens[access_token] = std::make_shared<Token>
			(expires_in, access_token);

		return tokens[access_token].get();
	}

	Token *findToken(const std::string &access_token)
	{
		std::map<std::string, std::shared_ptr<Token> >::iterator it;

		it = tokens.find(access_token);

		if (it != tokens.end())
			return it->second.get();

		return NULL;
	}

	void removeToken(const std::string &access_token)
	{
		tokens.erase(access_token);
	}

public:
	struct http_sock *httpsock = nullptr;
	struct sa laddr;
	char uri[256];

        std::map<std::string, std::shared_ptr<User> > users;
	std::map<std::string, std::shared_ptr<Token> > tokens;
	bool chunked = false;

	// XXX: only 1 Websock connection for now
	struct websock *ws = nullptr;
	struct websock_conn *ws_conn = nullptr;

	struct mbuf *mbq = nullptr;
	struct tcp_conn *tcq = nullptr;
	struct tmr tmr_send;
	unsigned frag_size = 32;

	struct odict *clients = nullptr;
};


class StunServer {

public:
	StunServer();
	~StunServer();
	void init();

public:
	struct udp_sock *us = nullptr;
	struct sa addr;
	unsigned nrecv = 0;
	bool force_error = false;
};


class TurnServer {

public:
	TurnServer();
	~TurnServer();
	void init();
	void set_sim_error(uint16_t sim_error);

public:
	struct turnd *turnd = nullptr;
	struct udp_sock *us = nullptr;
	struct sa addr;
	struct sa addr_tcp;
	struct sa addr_tls;
	unsigned nrecv = 0;
	unsigned nrecv_tcp = 0;
	unsigned nrecv_tls = 0;
};


class HttpServer {

public:
	HttpServer(bool secure = false);
	~HttpServer();
	void init(bool secure);

public:
	struct http_sock *sock = nullptr;
	struct sa addr;
	char url[256] = "";
	unsigned n_req = 0;
	unsigned n_cancel_after = 0;
};


extern const char fake_certificate_ecdsa[];


#endif
