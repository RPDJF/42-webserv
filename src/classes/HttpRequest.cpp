#include "./HttpRequest.hpp"

int HttpRequest::parseRequestLine_(const std::string& line) {
	int exception = 0;

	int iter = 0;
	size_t old_pos = 0;

	while (iter < 3) {
		size_t pos = line.find_first_of(' ', old_pos);
		if ((iter != 2 && pos == line.npos) || (iter == 2 && pos != line.npos)) {
			exception = 400;
			break;
		}
		switch(iter) {
			case 0:
				this->method_ = line.substr(old_pos, pos);
				break;
			case 1:
				this->url_ = line.substr(old_pos, pos - old_pos);
				if (this->url_.at(0) != '/')
					this->url_ = "/" + this->url_;
				break;
			default:
				this->httpVersion_ = line.substr(old_pos, line.size() - old_pos);
				break;
		}
		old_pos = pos + 1;
		iter++;
	}
	if ((this->method_ != "GET" && this->method_ != "POST") ||
		this->httpVersion_ != "HTTP/1.1") {
			std::cout << static_cast<int>(this->httpVersion_[this->httpVersion_.size() - 0]) << std::endl;
			exception = 400;
	}
	return exception;
}

// TODO: Handle body if any
int HttpRequest::parseBuffer_(char *buffer) {
	int exception = 0;

	std::string *request = new std::string(buffer);
	std::stringstream ss(*request);
	std::string line;
	bool isBody = false;

	size_t idx = 0;
	while (!isBody && std::getline(ss, line)) {
		line = line.substr(0, line.size() - 1);
		if (!idx) {
			exception = parseRequestLine_(line);
			if (exception) {
				Logger::error("RequestLine not valid") << std::endl;
				delete request;
				return exception;
			}
			Logger::debug("RequestLine validated") << std::endl;
		} else {
			if (line.empty()) {
				isBody = true;
				continue;
			}
			size_t sep = line.find_first_of(':', 0);
			if (sep != line.npos) {
				std::string key = line.substr(0, sep);
				std::string value = line.substr(sep + 2, line.size() - sep - 2);
				this->headers_.insert(std::make_pair(key, value));
			}
		}
		idx++;
	}
	delete request;
	if (isBody) {
		std::map<std::string, std::string>::iterator it_contentlen = this->headers_.find(H_CONTENT_LENGTH);
		if (it_contentlen != this->headers_.end()) {
			long long bodySize = Convert::ToInt(this->headers_[H_CONTENT_LENGTH]);
			if (bodySize) {
				if (bodySize < 0) {
					Logger::debug("Body size is negative");
					return (exception = 400);
				}
				this->body_ = new unsigned char[bodySize];
				char *bodySep = std::strstr(buffer, "\r\n\r\n");
				if (!bodySep) {
					Logger::debug("Request should contain body but delimiter '\\r\\n\\r\\n' was not found") << std::endl;
					return (exception = 400);
				} else {
					unsigned char *data = reinterpret_cast<unsigned char *>(bodySep + 2);
					try {
						std::copy(data, data + bodySize, this->body_);
					} catch(const std::exception& e) {
						Logger::error("Couldn't copy data from http request body: ") << e.what() << std::endl;
						return (exception = 400);
					}
					Logger::info("data: ") << this->getStringBody() << std::endl;	
				}
			}
		}
	}
	if (this->headers_.find(H_HOST) == this->headers_.end()) {
		return (exception = 400);
	}
	return exception;
}

HttpRequest::HttpRequest(char *buffer) {
	this->body_ = 0;
	parseBuffer_(buffer);
}

HttpRequest::HttpRequest(const HttpRequest& copy): method_(copy.method_), url_(copy.url_), httpVersion_(copy.httpVersion_), headers_(copy.headers_), body_(copy.body_) {}

HttpRequest& HttpRequest::operator=(const HttpRequest& assign) {
	if (this == &assign)
		return *this;
	this->method_ = assign.method_;
	this->url_ = assign.url_;
	this->httpVersion_ = assign.httpVersion_;
	this->headers_ = assign.headers_;
	this->body_ = assign.body_;
	return *this;
}

HttpRequest::~HttpRequest() {
	if (body_)
		delete[] body_;
}

const std::string& HttpRequest::getMethod() const {
	return this->method_;
}

const std::map<std::string, std::string>& HttpRequest::getHeaders() const {
	return this->headers_;
}

const unsigned char * HttpRequest::getBody() const {
	return this->body_;
}

const std::string HttpRequest::getStringBody() const {
	if (this->body_) {
		return std::string(reinterpret_cast<const char *>(this->body_), Convert::ToInt(this->headers_.at(H_CONTENT_LENGTH)));
	}
	return "";
}