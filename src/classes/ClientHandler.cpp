#include "./ClientHandler.hpp"

std::ostream& ClientHandler::fatal(const std::string& msg) { return Logger::fatal(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_fd_) + "): " + msg); }
std::ostream& ClientHandler::error(const std::string& msg) { return Logger::error(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_fd_) + "): " + msg); }
std::ostream& ClientHandler::warning(const std::string& msg) { return Logger::warning(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_fd_) + "): " + msg); }
std::ostream& ClientHandler::info(const std::string& msg) { return Logger::info(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_fd_) + "): " + msg); }
std::ostream& ClientHandler::debug(const std::string& msg) { return Logger::debug(C_BLUE + server_.getConfig().getServerNames()[0] + C_RESET + ": ClientHandler (fd: " + Convert::ToString(this->socket_fd_) + "): " + msg); }

static pollfd createPollfd(int fd) {
	pollfd out;
	out.fd = fd;
	out.events = POLLIN | POLLOUT;
	out.revents = 0;
	return out;
}

ClientHandler::ClientHandler(Runtime& runtime, ServerManager& server, int socket_fd, sockaddr_in addr, socklen_t addrlen):	
	socket_fd_(socket_fd),
	runtime_(runtime),
	server_(server),
	address_(addr, addrlen) {
		this->runtime_.getSockets().push_back(createPollfd(this->socket_fd_));
		#if LOGGER_DEBUG > 0
			this->debug("New socket") << std::endl;
		#endif
}

ClientHandler::ClientHandler(const ClientHandler& copy):
	socket_fd_(-1),	
	runtime_(copy.runtime_),
	server_(copy.server_) {
		Logger::fatal("A client was created by copy. Client constructors by copy aren't inteeded; the class init and deconstructor interacts with runtime!") << std::endl;
}

ClientHandler& ClientHandler::operator=(const ClientHandler& assign) {
	if (this == &assign)
		return *this;
	Logger::fatal("A client was assigned (operator=). Client assignments aren't inteeded; the class init and deconstructor interacts with runtime!") << std::endl;
	return *this;
}

ClientHandler::~ClientHandler() {
	#if LOGGER_DEBUG > 0
		this->debug("Client request deconstructor") << std::endl;
	#endif
	close(this->socket_fd_);
	if (this->buffer_.fileStream) {
		this->buffer_.fileStream->close();
		delete this->buffer_.fileStream;
	}
	delete this->buffer_.requestBuffer;
	{
		bool trigger = false;
		std::vector<pollfd>& sockets_ = this->runtime_.getSockets();
		std::vector<pollfd>::iterator it_sockets = sockets_.begin();
		while (it_sockets != sockets_.end()) {
			if (it_sockets->fd == this->socket_fd_) {
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

void ClientHandler::fillRequestBuffer_() {
	char buffer[DF_MAX_BUFFER];
	if (!this->buffer_.requestBuffer)
		this->buffer_.requestBuffer = new std::string("");
	ssize_t bytesRead;
	if ((bytesRead = recv(this->socket_fd_, buffer, DF_MAX_BUFFER, 0)) > 0) {
		this->buffer_.requestBuffer->append(buffer, bytesRead);
	}
	if (bytesRead < 0) { throw std::runtime_error(EXC_SOCKET_READ); }
}

std::string ClientHandler::buildDirlist_() {
	std::ostringstream oss;

	oss	<< "<!DOCTYPE html>\n"
		<< "<html>\n"
		<< "<head></head>\n"
		<< "<body>\n"
		<< "<h1>Directory Listing</h1>\n"
		<< "</body>\n";
	return oss.str();
}

void ClientHandler::sendHeader_() {
	#if LOGGER_DEBUG > 0
		this->debug("sending header") << std::endl;
	#endif
	std::string header;
	std::ostringstream oss;
	oss << this->getResponse().str();

	if (this->buffer_.fileStream) {
		oss << "\r\n";	
		this->state_.isSending = true;
	} else {
		this->state_.isSent = true;
	}
	header = oss.str();
	if (send(this->socket_fd_, header.data(), header.size(), 0) < 0) {
		throw std::runtime_error(EXC_SEND_ERROR);
	}
	#if LOGGER_DEBUG > 0
		if (this->request_.getUrl().find_first_of(".html") != std::string::npos)
			this->debug("sended: ") << header << std::endl;
	#endif
}

void ClientHandler::sendPlayload_() {
	#if LOGGER_DEBUG > 0
		std::ostream& stream = this->debug("sending playload ");
		if (!this->getRequest().getReqLine().empty())
			stream << this->getRequest().getUrl();
		stream << std::endl;
	#endif
	std::ifstream *file = this->buffer_.fileStream;
	char buffer[DF_MAX_BUFFER] = {0};
	if (file->read(buffer, DF_MAX_BUFFER) || file->gcount() > 0) {
		if (send(this->socket_fd_, buffer, file->gcount(), 0) < 0) {
			throw std::runtime_error(EXC_SEND_ERROR);
		}
	}
	if (!*file) {
		file->close();
		delete file;
		this->buffer_.fileStream = 0;
		this->state_.isSent = true;
	}
	#if LOGGER_DEBUG > 0
		if (this->request_.getUrl().find(".html") != std::string::npos) {
			this->debug("sended: ") << buffer << std::endl;
		}
	#endif
}

void ClientHandler::sendResponse() {
	#if LOGGER_DEBUG > 0
		this->debug("sending response") << std::endl;
	#endif
	if (this->state_.isSent) return;
	if (!this->state_.isSending) {
		this->sendHeader_();
	}
	sendPlayload_();
	return;
}

int ClientHandler::getSocket() const {
	return this->socket_fd_;
}

const HttpRequest& ClientHandler::buildRequest() {
	if (this->state_.isFetched)
		return this->request_;
	this->state_.isReading = false;
	this->state_.isFetched = true;
	#if LOGGER_DEBUG > 0
		this->debug("Request: ") << std::endl << C_ORANGE << this->buffer_.requestBuffer->data() << C_RESET << std::endl;
	#endif
	this->request_ = HttpRequest(this->buffer_.requestBuffer);
	this->state_.isFetched = true;
	return this->request_;
}

const HttpRequest& ClientHandler::getRequest() const { return this->request_; }

const HttpResponse& ClientHandler::buildResponse(const HttpResponse& response) {
	std::string rootFile = this->server_.getRouteConfig()[0].getRoot() + this->request_.getUrl();

	this->buffer_.fileStream = new std::ifstream(rootFile.c_str());
	std::ifstream *fileStream = this->buffer_.fileStream;

	if (!fileStream->good()) {
		#if LOGGER_DEBUG > 0
			Logger::debug(EXC_FILE_NOT_FOUND(rootFile)) << std::endl;
		#endif
		this->response_ = HttpResponse(404);
		this->state_.hasResponse = true;
		return this->response_;
	}
	this->response_ = response;
	fileStream->seekg(0, std::ios::end);
	this->response_.getHeaders()[H_CONTENT_LENGTH] = Convert::ToString(this->buffer_.fileStream->tellg());
	fileStream->seekg(0, std::ios::beg);
	this->state_.hasResponse = true;
	return this->response_;
}

const HttpResponse& ClientHandler::getResponse() const { return this->response_; }
const char *ClientHandler::getClientIp() const { return this->address_.clientIp; }

void ClientHandler::readSocket() {
	fillRequestBuffer_();
	#if LOGGER_DEBUG > 0
		this->debug("Syncing") << std::endl;
	#endif
	this->runtime_.Sync();
}

bool ClientHandler::isFetched() const { return this->state_.isFetched; }
void ClientHandler::setFetched(bool value) { this->state_.isFetched = value; }
bool ClientHandler::isReading() const { return this->state_.isReading; }
void ClientHandler::setReading(bool value) { this->state_.isReading = value; }
bool ClientHandler::isSending() const { return this->state_.isSending; }
const ServerManager& ClientHandler::getServer() const { return this->server_; }
bool ClientHandler::isSent() const { return this->state_.isSent; }
bool ClientHandler::isDead() const { return this->state_.isDead; }
bool ClientHandler::hasResponse() const { return this->state_.hasResponse; }
std::ifstream *ClientHandler::getFileStream() { return this->buffer_.fileStream; }