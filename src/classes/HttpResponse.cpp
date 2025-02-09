#include "./HttpResponse.hpp"

static std::string getHttpDate() {
    std::time_t now = std::time(0);
    std::tm *tm = std::gmtime(&now);
    char buf[30];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
    return std::string(buf);
}

static std::string getType(const std::string& url) {
    std::string contentType;
    if (url.find(".html\0") != std::string::npos) {
        contentType = "text/html";
    } else if (url.find(".css\0") != std::string::npos) {
        contentType = "text/css";
    } else if (url.find(".js\0") != std::string::npos) {
        contentType = "application/javascript";
    } else if (url.find(".json\0") != std::string::npos) {
        contentType = "application/json";
    } else if (url.find(".xml\0") != std::string::npos) {
        contentType = "application/xml";
    } else if (url.find(".jpg\0") != std::string::npos || url.find(".jpeg\0") != std::string::npos) {
        contentType = "image/jpeg";
    } else if (url.find(".png\0") != std::string::npos) {
        contentType = "image/png";
    } else if (url.find(".gif\0") != std::string::npos) {
        contentType = "image/gif";
    } else if (url.find(".txt\0") != std::string::npos) {
        contentType = "text/plain";
    } else if (url.find(".svg\0") != std::string::npos) {
		contentType = "image/svg+xml";
	} else if (url.find(".ico\0") != std::string::npos) {
		contentType = "image/x-icon";
	} else {
        contentType = "application/octet-stream";
    }
    return contentType;
}

HttpResponse::HttpResponse():
	version_("HTTP/1.1"),
	status_(0) {
}

static std::string checkStatus(int status) {
	switch (status) {
		case 100: return "Continue";
		case 200: return "OK";
		case 201: return "Created";
		case 400: return "Bad Request";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 404: return "Not Found";
		case 500: return "Internal Server Error";
		case 503: return "Service Unavailable";
		default: return "Unknown";
	}
}

// TODO: use default errors when possible
HttpResponse::HttpResponse(int status):
	version_("HTTP/1.1"),
	status_(status) {
		this->status_msg_ = checkStatus(status);
		this->headers_.insert(std::make_pair("Date", getHttpDate()));
		this->headers_.insert(std::make_pair("Server", "SIR Webserver/1.0"));
}

HttpResponse::HttpResponse(int status, const std::string& fileName, const std::string& url):
	version_("HTTP/1.1"),
	status_(status),
	fileName_(fileName) {
		this->status_msg_ = checkStatus(status);
		this->headers_.insert(std::make_pair("Date", getHttpDate()));
		this->headers_.insert(std::make_pair("Server", "SIR Webserver/1.0"));
		this->headers_.insert(std::make_pair("Content-Type", getType(url)));
}

HttpResponse::HttpResponse(const HttpResponse& copy):
	version_(copy.version_),
	status_(copy.status_),
	status_msg_(copy.status_msg_),
	headers_(copy.headers_),
	fileName_(copy.fileName_) {}

HttpResponse& HttpResponse::operator=(const HttpResponse& assign) {
	if (this == &assign)
		return *this;
	this->status_ = assign.status_;
	this->status_msg_ = assign.status_msg_;
	this->headers_ = assign.headers_;
	this->fileName_ = assign.fileName_;
	return *this;
}

HttpResponse::~HttpResponse() {

}

const std::string HttpResponse::str() const {
	std::string resp;
	std::ostringstream oss;

	oss	<< this->version_ << " " << this->status_ << " " << this->status_msg_ << "\r\n";
	for(std::map<std::string, std::string>::const_iterator it = this->headers_.begin(); it != this->headers_.end(); it++) {
		oss << it->first << ": " << it->second << "\r\n";
	}
	if (!this->fileName_.empty())
		oss << "\r\n";
	resp = oss.str();
	return resp;
}

void HttpResponse::sendResp(int socket_fd) {
	std::ifstream file(this->fileName_.c_str());

	if (!this->fileName_.empty()) {
		if (!file.good()) { throw std::runtime_error(EXC_FILE_NOT_FOUND(this->fileName_)); }
		file.seekg(0, std::ios::end);
		this->headers_.insert(std::make_pair("Content-Length", Convert::ToString(file.tellg())));
		file.seekg(0, std::ios::beg);
	}
	if (send(socket_fd, this->str().data(), this->str().size(), 0) < 0) {
		throw std::runtime_error(EXC_SEND_ERROR);
	}
	if(!this->fileName_.empty()) {
		char buffer[DF_MAX_BUFFER];
		while (file.read(buffer, DF_MAX_BUFFER) || file.gcount() > 0) {
			if(send(socket_fd, buffer, file.gcount(), 0) < 0) {
				throw std::runtime_error(EXC_SEND_ERROR);
			}
		}
		file.close();
	}
}

int HttpResponse::getStatus() const {
	return this->status_;
}

const std::string& HttpResponse::getStatusMsg() const {
	return this->status_msg_;
}

const std::map<std::string, std::string>& HttpResponse::getHeaders() const {
	return this->headers_;
}