#include "./Runtime.hpp"

std::ostream& Runtime::fatal(const std::string& msg) const { return Logger::fatal("Runtime: " + msg); }
std::ostream& Runtime::error(const std::string& msg) const { return Logger::error("Runtime: " + msg); }
std::ostream& Runtime::warning(const std::string& msg) const { return Logger::warning("Runtime: " + msg); }
std::ostream& Runtime::info(const std::string& msg) const { return Logger::info("Runtime: " + msg); }
std::ostream& Runtime::debug(const std::string& msg) const { return Logger::debug("Runtime: " + msg); }

Runtime *Runtime::ptr = NULL ;

Runtime::Runtime(const ConfigManager& config): config_(config) {
	ptr = this ;
	setUpSignalHandler_() ;	
	this->initializeServers_(this->config_.getServers());
}

ServerManager *Runtime::getHost(const ServerManager& ref) {
	for(std::map<int, ServerManager*>::const_iterator it = this->servers_map_.begin();
		it != this->servers_map_.end(); it++) {
			if (it->second->getConfig().getPort() == ref.getConfig().getPort()
				&& it->second->getConfig().getHost() == ref.getConfig().getHost()) {
					return it->second;
			}
		}
	return 0;
}

void Runtime::initializeServers_(const std::vector<ServerConfig>& configs) {
	for(std::vector<ServerConfig>::const_iterator config = configs.begin(); config != configs.end(); config++) {
		this->servers_.push_back(ServerManager(*config));
	}
	size_t socket_reserved = 0;
	for(std::vector<ServerManager>::iterator server = this->servers_.begin(); server != this->servers_.end(); server++) {
		try {
			server->init();
			socket_reserved += server->getConfig().getMaxClients();
			this->servers_map_[server->getSocket().fd] = &*server;
			this->sockets_.push_back(server->getSocket());
			server->getVirtualHosts().push_back(&*server);
		} catch (const std::exception& e) {
			ServerManager *host = 0;
			if (!(host = getHost(*server))) {
				this->fatal("'") << server->getConfig().getServerNames()[0] << "' : " << e.what() << std::endl;
				this->warning("if trying to use virtualhosts, the host:port need to match perfectly") << std::endl;
				continue;
			}
			host->getVirtualHosts().push_back(&*server);
			host->updateMaxBody(server->getMaxBody());
			for(std::vector<std::string>::const_iterator it = host->getConfig().getServerNames().begin();
				it != host->getConfig().getServerNames().end(); it++) {
					if (std::find(server->getConfig().getServerNames().begin(), server->getConfig().getServerNames().end(), *it) != server->getConfig().getServerNames().end())
						this->warning("conflicting server name \"" + *it + "\" on http://" + server->getConfig().getHost() + ":" + Convert::ToString(server->getConfig().getPort())) << ", ignored" << std::endl;
				}

		}
	}
	this->sockets_.reserve(socket_reserved);
}

Runtime::~Runtime() {
    #if LOGGER_DEBUG
        debug("deconstructor") << std::endl;
    #endif
    handleExit_();
}

Runtime& Runtime::operator=(const Runtime& assign) {
	if (this == &assign)
		return *this;
	return *this;
}

void Runtime::runServers() {
	if (this->servers_map_.empty()) {
		this->error("No binded servers to run") << std::endl;
		return;
	}

	signal(SIGPIPE, SIG_IGN);
	while (true) {
		if (poll(&this->sockets_[0], this->sockets_.size(), this->config_.getMinTimeout()) < 0) {
			if (errno == EINTR) break;
			else {
				this->fatal("poll fatal: ") << strerror(errno) << std::endl;
				break;
			}
		}
		struct timeval tv;
		gettimeofday(&tv, 0);
		this->lat_tick_ = tv.tv_sec * 1000 + tv.tv_usec / 1000;
		this->checkServersSocket_();
		this->checkClientsSockets_();
	}
	signal(SIGPIPE, SIG_DFL);
	return;
}

void Runtime::closeServers() {
	this->info("Closing server") << std::endl;
	this->handleExit_();
}

void Runtime::handleExit_() {
	while (!this->clients_.empty())
		delete this->clients_.begin()->second;
	this->clients_.clear();
	this->servers_.clear();
	this->servers_map_.clear();
	for(std::vector<pollfd>::iterator it = this->sockets_.begin(); it != this->sockets_.end(); it++) {
		close(it->fd);
	}
	this->sockets_.clear();
}

void Runtime::checkClientsSockets_() {
	for(size_t i = this->servers_map_.size(); i < this->sockets_.size(); i++) {
		pollfd *socket;
		ClientHandler *client;

		socket = &this->sockets_[i];
		client = this->clients_[socket->fd];
		if(this->clients_.find(this->sockets_[i].fd) != this->clients_.end()) {
			if (this->handleClientPollhup_(client, socket)
				|| this->handleClientPollin_(client, socket) < 0
				|| this->handleClientPollout_(client, socket)) {
					i--;
					continue;
			}
		}
		int clientTimeout = client->hasServer() ? client->getServerConfig().getTimeout() : client->getHostServer().getConfig().getTimeout();
		if (this->lat_tick_ >= (client->getLastAlive() + clientTimeout)) {
			#if LOGGER_DEBUG
				this->debug("throw client: reached timeout") << std::endl;
			#endif
			delete client;
			i--;
			continue;
		}
	}
}

int Runtime::handleClientPollhup_(ClientHandler *client, pollfd *socket) {
	if (socket->revents & POLLHUP) {
		delete client;
		#if LOGGER_DEBUG
			this->debug("disconnected revent") << std::endl;
		#endif
		return 1;
	}
	return 0;
}

void Runtime::checkServersSocket_() {
	for (size_t i = 0; i < this->servers_map_.size(); i++) {
		if (this->sockets_[i].revents & POLLIN) {
			int socket_fd = -1;
			sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			socket_fd = accept(this->sockets_[i].fd, (sockaddr *)&client_addr, &client_len);
			if (socket_fd < 0) {
				this->error("error on request accept(): ") << strerror(errno) << std::endl;
				continue;
			}
			this->clients_[socket_fd] = new ClientHandler(*this, *this->servers_map_.at(this->sockets_[i].fd), socket_fd, client_addr, client_len);
		}
	}
}

void Runtime::handleRequest_(ClientHandler *client) {
	// Print Request
	std::ostream& stream = this->info("") << C_BLUE << client->getServerConfig().getServerNames()[0] << C_RESET << ": Request "
		<< client->getRequest().getMethod() << " " << client->getRequest().getUrl()
		<< " client " << client->getClientIp();
	#if LOGGER_DEBUG
		stream << " (fd: " << client->getFd() << ")";
	#endif
	stream << std::endl;
	if (client->getFlags() & THROWING || client->getRequest().getAllBody()) {
		unsigned long long bodySize = Convert::ToT<unsigned long long>(client->getRequest().getHeaders().at(H_CONTENT_LENGTH));
		if (client->getFlags() & ERR_NOLENGTH)
			throw std::runtime_error(EXC_BODY_NO_SIZE);
		else if(client->getFlags() & ERR_BODYTOOBIG || bodySize > client->getServerConfig().getClientBodyLimit())
			throw std::runtime_error(EXC_BODY_TOO_LARGE);
		else if (bodySize != client->getRequest().getAllBody()->size())
			throw std::runtime_error(EXC_BODY_SIZE_MISMATCH);
	}
}

void Runtime::handleRequest_(ClientHandler *client, const std::string& exception) {
	// Called on building error -> create a new response based on throw event
	if (exception == EXC_INVALID_RL || exception == EXC_BODY_NEG_SIZE || exception == EXC_BODY_NOLIMITER || exception == EXC_HEADER_NOHOST)
		client->buildResponse(HttpResponse(client->getRequest(), 400));
	else if (exception == EXC_BODY_TOO_LARGE) client->buildResponse(HttpResponse(client->getRequest(), 413));
	else if (exception == EXC_BODY_NO_SIZE) client->buildResponse(HttpResponse(client->getRequest(), 411));
	else if (exception == EXC_BODY_SIZE_MISMATCH) client->buildResponse(HttpResponse(client->getRequest(), 400));
	else client->buildResponse(HttpResponse(client->getRequest(), 500));
}

int Runtime::handleClientPollin_(ClientHandler *client, pollfd *socket) {
	int status = 0;
	if (!(socket->revents & POLLIN) || !(client->getFlags() & READING))
		return status;
	try {
		client->readSocket();
	} catch (const std::exception& e) {
		const std::string msg(e.what());
		if (msg == EXC_NO_BUFFER) {
			delete client;
			return -1;
		} else if (msg == EXC_CLOSE) {
			delete client;
			return -1;
		}
		client->clearFlag(READING);
		client->buildResponse(HttpResponse(client->getRequest(), 500));
		socket->events = POLLOUT | POLLHUP;
		this->error("client ") << client->getFd() << ": " << e.what() << std::endl;
		status = 1;
	}
	if (client->getFlags() & READING) {
		client->updateLastAlive();
		return status;
	}
	socket->events = POLLOUT | POLLHUP;
	try {
		client->buildRequest();
		client->retrieveServer();
		this->handleRequest_(client);
	} catch (const std::exception& e) {
		std::string exception(e.what());
		this->handleRequest_(client, exception);
		this->error(e.what()) << std::endl;
		status = 1;
	}
	client->updateLastAlive();
	return status;
}

void Runtime::logResponse_(ClientHandler *client) {
	std::ostream *stream = 0;
	if (client->getResponse().getStatus() < 300) { stream = &this->info(""); }
	else if (client->getResponse().getStatus() < 400) { stream = &this->warning(""); }
	else if (client->getResponse().getStatus() < 500) { stream = &this->error(""); }
	else { stream = &this->fatal(""); }
	*stream << C_BLUE << client->getServerConfig().getServerNames()[0] << C_RESET << ": " << client->getResponse().getResLine();
	if (!client->getRequest().getReqLine().empty())
			*stream << " for " << client->getRequest().getMethod() << " " << client->getRequest().getUrl();
	*stream << " client " << client->getClientIp();
	#if LOGGER_DEBUG
		if (LOGGER_DEBUG)
			*stream << " (fd: " << client->getFd() << ")";
	#endif
	*stream << std::endl;
}

int Runtime::handleClientPollout_(ClientHandler *client, pollfd *socket) {
	if (socket->revents & POLLOUT) {
		try {
			client->sendResponse();
		} catch(const std::exception& e) {
			this->fatal("throwing client: ") << e.what() << std::endl;
			delete client;
			return -1;
		}
		if (client->getFlags() & SENT) {
			this->logResponse_(client);
			if (client->getResponse().getHeaders()[H_CONNECTION] != "keep-alive") {
				delete client;
				return 1;
			}
			client->flush();
			socket->events = POLLIN | POLLHUP;
		}
		client->updateLastAlive();
	}
	return 0;
}

std::vector<pollfd>& Runtime::getSockets() {
	return this->sockets_;
}

std::map<int, ClientHandler *>& Runtime::getClients() {
	return this->clients_;
}

pollfd *Runtime::getSocket_(int socket_fd_) {
	for(size_t i = 0; i < this->sockets_.size(); i++) {
		if(socket_fd_ == this->sockets_[i].fd) {
			return &this->sockets_[i];
		}
	}
	return 0;
}

void	Runtime::signalHandler_( int signum ) {
	(void)signum;
}

void	Runtime::setUpSignalHandler_( ) {
	struct sigaction sa ;
	sa.sa_handler = Runtime::signalHandler_ ;
	sigemptyset(&sa.sa_mask) ;
	sa.sa_flags = 0;

	if ( sigaction( SIGINT, &sa, NULL ) == -1 ) {
		Logger::fatal("Error setting signal handler.") << std::endl ;
	}

}