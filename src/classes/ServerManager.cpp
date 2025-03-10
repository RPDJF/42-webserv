#include "./ServerManager.hpp"

std::ostream& ServerManager::fatal(const std::string& msg) { return Logger::fatal(C_BLUE + this->config_.getServerNames()[0] + C_RESET + ": ServerManager: " + msg); }
std::ostream& ServerManager::error(const std::string& msg) { return Logger::error(C_BLUE + this->config_.getServerNames()[0] + C_RESET + ": ServerManager: " + msg); }
std::ostream& ServerManager::warning(const std::string& msg) { return Logger::warning(C_BLUE + this->config_.getServerNames()[0] + C_RESET + ": ServerManager: " + msg); }
std::ostream& ServerManager::info(const std::string& msg) { return Logger::info(C_BLUE + this->config_.getServerNames()[0] + C_RESET + ": ServerManager: " + msg); }
std::ostream& ServerManager::debug(const std::string& msg) { return Logger::debug(C_BLUE + this->config_.getServerNames()[0] + C_RESET + ": ServerManager: " + msg); }

/**
 * newAddr: Creates a new socket address from specific port and ipv4 interface
 * @param port The port to bind to
 * @param interface The ipv4 interface address to bind to
 * @return New socket address as `sockaddr_in`
 */
static sockaddr_in newAddr(int port, std::string interface) {
	struct sockaddr_in addrv4;
	std::memset(&addrv4, 0, sizeof(addrv4));
	inet_pton(AF_INET, interface.c_str(), &addrv4.sin_addr);
	addrv4.sin_family = AF_INET;
	addrv4.sin_port = htons(port);
	return addrv4;
}

void ServerManager::init() {
	this->maxBody_ = this->config_.getClientBodyLimit();
	this->server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

	if (this->server_fd_ < 0) { throw std::logic_error("server_fd_ socket: " + std::string(strerror(errno))); }
	int opt = 1;
	if (setsockopt(this->server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { throw std::logic_error("setsockopt: " + std::string(strerror(errno))); }
	if (bind(this->server_fd_, this->address_, sizeof(addrv4_)) < 0) { throw std::logic_error("binding " + this->config_.getHost() + ":" + Convert::ToString(this->config_.getPort()) + ": " + std::string(strerror(errno))); }
	if (listen(this->server_fd_, this->config_.getMaxClients())) { throw std::logic_error("error on listen: " + std::string(strerror(errno))); }

	this->socket_.fd = this->server_fd_;
	this->socket_.events = POLLIN;
	this->socket_.revents = 0;
	this->info("Virtual host listening on: http://" + (this->config_.getHost() == "0.0.0.0" ? "127.0.0.1" : this->config_.getHost()) + ":" + std::string(Convert::ToString(this->config_.getPort()))) << std::endl;
}

ServerManager::ServerManager(const ServerConfig& config):
	config_(config),
	addrv4_(newAddr(config.getPort(), config.getHost())),
	address_((sockaddr *)&this->addrv4_),
	routeconfig_(config.getRoutes()),
	server_fd_(-1),
	maxBody_(0) {}

ServerManager::ServerManager(const ServerManager& copy):
	config_(copy.config_),
	addrv4_(copy.addrv4_),
	address_((sockaddr *)&this->addrv4_),
	routeconfig_(copy.routeconfig_),
	server_fd_(copy.server_fd_),
	socket_(copy.socket_),
	maxBody_(copy.maxBody_) {}

ServerManager& ServerManager::operator=(const ServerManager& assign) {
	if (this == &assign)
		return *this;
	return *this;
}

ServerManager::~ServerManager() {
	#if LOGGER_DEBUG
		this->debug("ServerManager deconstructor") << std::endl;
	#endif
	if (this->server_fd_ > 0)
		close(this->server_fd_);
}

const pollfd& ServerManager::getSocket() const { return this->socket_; }
const ServerConfig& ServerManager::getConfig() const { return this->config_; }
const std::vector<RouteConfig>& ServerManager::getRouteConfig() const { return this->routeconfig_; }
std::vector<ServerManager *>& ServerManager::getVirtualHosts() { return this->virtualHosts_; }
const std::vector<ServerManager *>& ServerManager::getVirtualHosts() const { return this->virtualHosts_; }
unsigned long long ServerManager::getMaxBody() const { return this->maxBody_; }
void ServerManager::updateMaxBody(unsigned long long value) { if(value > this->maxBody_) this->maxBody_ = value; }