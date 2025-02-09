#include "./ClientHandler.hpp"

std::ostream& ClientHandler::fatal(const std::string& msg) { return Logger::fatal(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_) + "): " + msg); }
std::ostream& ClientHandler::error(const std::string& msg) { return Logger::error(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (" + Convert::ToString(this->socket_) + "): " + msg); }
std::ostream& ClientHandler::warning(const std::string& msg) { return Logger::warning(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (" + Convert::ToString(this->socket_) + "): " + msg); }
std::ostream& ClientHandler::info(const std::string& msg) { return Logger::info(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (" + Convert::ToString(this->socket_) + "): " + msg); }
std::ostream& ClientHandler::debug(const std::string& msg) { return Logger::debug(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (" + Convert::ToString(this->socket_) + "): " + msg); }

static pollfd makeClientSocket(int fd) {
	pollfd out;
	out.fd = fd;
	out.events = POLLIN | POLLOUT;
	out.revents = 0;
	return out;
}

ClientHandler::ClientHandler(Runtime& runtime, ServerManager& server, int client_socket, sockaddr_in client_addr, socklen_t len):
	runtime_(runtime),
	server_(server),
	addr_(client_addr),
	len_(len),
	headers_(0),
	fileBuffer_(0),
	isFetched_(false),
	isReading_(false) {
		runtime.getSockets().push_back(makeClientSocket(client_socket));
		this->socket_ = client_socket;
		this->debug("New socket") << std::endl;
}

ClientHandler::ClientHandler(const ClientHandler& copy):
	runtime_(copy.runtime_),
	server_(copy.server_),
	addr_(copy.addr_),
	len_(copy.len_),
	socket_(copy.socket_),
	headers_(copy.headers_),
	fileBuffer_(copy.fileBuffer_),
	req_(copy.req_),
	isFetched_(copy.fileBuffer_),
	isReading_(copy.isReading_) {
}

ClientHandler& ClientHandler::operator=(const ClientHandler& assign) {
	if (this == &assign)
		return *this;
	this->runtime_ = assign.runtime_;
	this->server_ = assign.server_;
	this->socket_ = assign.socket_;
	this->headers_ = assign.headers_;
	this->fileBuffer_ = assign.fileBuffer_;
	this->req_ = assign.req_;
	this->isFetched_ = assign.fileBuffer_;
	this->isReading_ = assign.isReading_;
	return *this;
}

ClientHandler::~ClientHandler() {
	this->debug("Client request deconstructor") << std::endl;
	close(this->socket_);
	delete this->headers_;
	delete this->fileBuffer_;
	{
		bool trigger = false;
		std::vector<pollfd>& sockets_ = this->runtime_.getSockets();
		std::vector<pollfd>::iterator it_sockets = sockets_.begin();
		while (it_sockets != sockets_.end()) {
			if (it_sockets->fd == this->socket_) {
				sockets_.erase(it_sockets);
				trigger = true;
				break;
			}
			it_sockets++;
		}
		if (!trigger)
			this->error("socket not destroyed from Runtime sockets_");
	}
	{
		bool trigger = false;
		std::vector<ClientHandler *>& clients_ = this->runtime_.getClients();
		std::vector<ClientHandler *>::iterator it_clients = clients_.begin();
		while (it_clients != clients_.end()) {
			if (*it_clients == this) {
				it_clients = clients_.erase(it_clients);
				trigger = true;
				break;
			}
			it_clients++;
		}
		if (!trigger)
			this->error("client not destroyed from Runtime clients_");
	}
}

void ClientHandler::loadHeaders_() {
	this->isReading_ = true;
	char buffer[DF_MAX_BUFFER];
	if (!this->headers_)
		this->headers_ = new std::string("");
	ssize_t bytesRead;
	if ((bytesRead = recv(this->socket_, buffer, DF_MAX_BUFFER, 0)) > 0) {
		this->headers_->append(buffer, bytesRead);
	}
	if (bytesRead < 0) { throw std::runtime_error(EXC_SOCKET_READ); }
}

void ClientHandler::buildResBody_(std::ifstream& input) {
	this->fileBuffer_ = new std::string("");
	std::string line;
	while (std::getline(input, line)) {
		this->fileBuffer_->append(line.append("\n"));
	}
	if (input.bad()) {
		throw std::runtime_error(EXC_FILE_READ);
	}
	input.close();
}

//TODO: Refactor hanlde()
//TODO: Inlude max client body size
/**
 * handle: Handle the client request
 * @attention Nasty code! Needs refactor
 */
void ClientHandler::handle() {
	if (!this->isReading_ && !this->isFetched_) {
		this->fetch();
		this->debug("Request:") << std::endl << C_ORANGE << this->headers_->data() << C_RESET << std::endl;
	} else if (this->isReading_) {
		return ;
	}

	std::string fileName = this->server_.getConfig().getRoutes()[0].getRoot() + req_.getUrl();
	std::ifstream input(fileName.c_str());
	if (input.is_open()) {
		try {
			this->buildResBody_(input);
		} catch (const std::exception& e) {
			(this->resp_ = HttpResponse(500)).sendResp(this->socket_);
			throw;
		}
		(this->resp_ = HttpResponse(200, this->fileBuffer_->data(), this->fileBuffer_->size(), req_.getUrl())).sendResp(this->socket_);
	} else {
		(this->resp_ = HttpResponse(404)).sendResp(this->socket_);
		throw std::runtime_error(EXC_FILE_NF(fileName));
	}
}

/**
 * getSocket: Get client socket
 * @return `int` fd socket reference
 */
int ClientHandler::getSocket() const {
	return this->socket_;
}

const HttpRequest& ClientHandler::fetch() {
	if (this->isFetched_)
		return this->req_;
	if (this->isReading_) {
		throw std::runtime_error("trying to fetch Client without finishing reading socket");
	}
	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(this->addr_.sin_addr), client_ip, INET_ADDRSTRLEN);
	this->clientIp_ = client_ip;
	try {
		this->req_ = HttpRequest(this->headers_->data());
		this->isFetched_ = true;
	} catch(const std::exception& e) {
		(this->resp_ = HttpResponse(400)).sendResp(this->socket_);
		throw;
	}
	return this->req_;
}

const HttpResponse& ClientHandler::getResponse() const {
	return this->resp_;
}

const std::string& ClientHandler::getClientIp() const {
	return this->clientIp_;
}

bool ClientHandler::isFetched() const {
	return this->isFetched_;
}

int ClientHandler::readSocket() {
	if (this->isFetched_) {
		this->isReading_ = false;
		return 0;
	}
	try {
		loadHeaders_();
		return (this->isReading_);
	} catch(const std::exception& e) {
		this->fatal(e.what()) << std::endl;
		(this->resp_ = HttpResponse(400)).sendResp(this->socket_);
		return -1;
	}
}

bool ClientHandler::isReading() const { return this->isReading_; }
void ClientHandler::setReading(bool value) { this->isReading_ = value; }