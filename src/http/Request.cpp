#include "Request.hpp"

#include <unistd.h>

#include <algorithm>

#include "../log/Log.hpp"

namespace http {

#define MAX_BUFFER_SIZE 8192

#define IS_METHOD_CHAR(c) (c >= 'A' && c <= 'Z')

#define IS_SEPERATOR_CHAR(c)                                                               \
    (c == '(' || c == ')' || c == '<' || c == '>' || c == '@' || c == ',' || c == ';' ||   \
     c == ':' || c == '\\' || c == '\"' || c == '/' || c == '[' || c == ']' || c == '?' || \
     c == '=' || c == '{' || c == '}' || c == ' ' || c == '\t')
#define IS_TEXT_CHAR(c) (isprint(c) || c == '\t')
#define IS_TOKEN_CHAR(c) (isprint(c) && !IS_SEPERATOR_CHAR(c))

#define IS_URI_SUBDELIM(c)                                                                \
    (c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' || \
     c == '+' || c == ',' || c == ';' || c == '=')
#define IS_UNRESERVED_URI_CHAR(c) (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
#define IS_HOST_CHAR(c) (IS_UNRESERVED_URI_CHAR(c) || IS_URI_SUBDELIM(c))

// #define IS_HEADER_KEY_CHAR(c) (IS_TOKEN_CHAR(c))
// #define IS_HEADER_VALUE_CHAR(c) (IS_TEXT_CHAR(c))

#define HEX_CHAR_TO_INT(c) (isdigit(c) ? c - '0' : tolower(c) - 87)

static const std::pair<std::string, bool> headers[] = {
    std::make_pair("host", false)
    //    std::make_pair("connection", true),
    //    std::make_pair("content-length", true)
};

Request::Request(core::ByteBuffer& buf)
    : _buf(buf),
      _request_end(0),
      _method_start(0),
      _method_end(0),
      _method(NONE),
      _version_start(0),
      _version_end(0),
      _uri_start(0),
      _uri_end(0),
      _uri_host_start(0),
      _uri_host_end(0),
      _uri_port_start(0),
      _uri_port_end(0),
      _uri_path_start(0),
      _uri_path_end(0),
      _uri_query_start(0),
      _uri_query_end(0),
      _uri_fragment_start(0),
      _uri_fragment_end(0),
      _header_start(0),
      _header_end(0),
      _header_key_start(0),
      _header_key_end(0),
      _header_value_start(0),
      _header_value_end(0),
      _chunked_body(false),
      _chunked_body_state(CHUNKED_BODY_LENGTH_START),
      _body_expected_size(0),
      _body_start(0),
      _body_end(0),
      connection_state(CONNECTION_KEEP_ALIVE),
      _state(REQUEST_LINE),
      _state_request_line(START),
      _state_header(H_KEY_START) {
    _buf.reserve(8192);
}

Request::~Request() {}

int Request::parse_input() {
    switch (_state) {
        case REQUEST_LINE:
            error = parse_request_line();
            if (error == 0) {
                _state = REQUEST_HEADER;
            } else if (error > 99) {
                std::cerr << "Error: " << error << '\n';
                _state = REQUEST_ERROR;
                return error;
            }
            break;
        case REQUEST_HEADER:
            error = parse_header();
            if (error == 0) {
                error = _analyze_request();
                if (error != 0)
                    return error;
                if (_chunked_body)
                    _state = REQUEST_BODY_CHUNKED;
                else
                    _state = REQUEST_BODY;
            } else if (error > 99) {
                std::cerr << "Error: " << error << '\n';
                _state = REQUEST_ERROR;
                return error;
            }
            break;
        case REQUEST_BODY:
            if (_body_expected_size <= _buf.size() - _buf.pos) {
                print();
                _state = REQUEST_DONE;
                _body_start = _buf.pos;
                _request_end = _body_end = _body_start + _body_expected_size;
                // std::cout << "Body:\n\'";
                // for (std::size_t i = _body_start; i < _body_end; _buf.pos++, i++) {
                //     std::cout << _buf[i];
                // }
                // std::cout << "\'\n";
            }
            break;
        case REQUEST_BODY_CHUNKED:
            std::cerr << "parse chunked body\n";
            error = parse_chunked_body();
            if (error == 0) {
                _state = REQUEST_DONE;
                std::cout << "Body:\n\'";
                for (std::size_t i = 0; i < _chunked_body_buf.size(); i++) {
                    std::cout << _chunked_body_buf[i];
                }
                std::cout << "\'\n";
            } else if (error > 99) {
                std::cerr << "Error: " << error << '\n';
                _state = REQUEST_DONE;
                return error;
            }
            break;
        case REQUEST_ERROR:
            break;
        case REQUEST_DONE:
            break;
    }
    return 0;
}

inline int Request::parse_chunked_body() {
    // if (_chunked_body_buf.size() > MAX_CLIENT_BODY)
    // return -1;

    char c;
    for (std::size_t i = _buf.pos; i < _buf.size(); _buf.pos++, i++) {
        c = _buf[i];
        switch (_chunked_body_state) {
            case CHUNKED_BODY_LENGTH_START:
                switch (c) {
                    case '0':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_0;
                        break;
                    default:
                        if (!isxdigit(c))
                            return 400;
                        _chunked_body_state = CHUNKED_BODY_LENGTH;
                        _chunk_size = HEX_CHAR_TO_INT(c);
                        break;
                }
                break;
            case CHUNKED_BODY_LENGTH:
                switch (c) {
                    case '\r':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_ALMOST_DONE;
                        break;
                    case '\n':
                        _chunked_body_state = CHUNKED_BODY_DATA_SKIP;
                        break;
                    case ';':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_EXTENSION;
                        break;
                    default:
                        if (!isxdigit(c))
                            return 400;
                        _chunk_size = _chunk_size * 16 + HEX_CHAR_TO_INT(c);
                        // if (_chunk_size > max_client_body)
                        //     return 413;
                        break;
                }
                break;
            case CHUNKED_BODY_LENGTH_EXTENSION:
                switch (c) {
                    case '\r':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_ALMOST_DONE;
                        break;
                    case '\n':
                        _chunked_body_state = CHUNKED_BODY_DATA_SKIP;
                        break;
                    default:
                        break;
                }
                break;
            case CHUNKED_BODY_LENGTH_ALMOST_DONE:
                if (c != '\n')
                    return 400;
                _chunked_body_state = CHUNKED_BODY_DATA_SKIP;
                break;
            case CHUNKED_BODY_DATA_SKIP:
                if (_chunk_size > 0) {
                    _chunked_body_buf.push_back(c);
                    _chunk_size--;
                    break;
                }
                switch (c) {
                    case '\r':
                        _chunked_body_state = CHUNKED_BODY_DATA_ALMOST_DONE;
                        break;
                    case '\n':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_START;
                        break;
                    default:
                        return 400;
                }
                break;
            case CHUNKED_BODY_DATA_ALMOST_DONE:
                if (c != '\n')
                    return 400;
                _chunked_body_state = CHUNKED_BODY_LENGTH_START;
                break;
            case CHUNKED_BODY_LENGTH_0:
                switch (c) {
                    case '\r':
                        _chunked_body_state = CHUNKED_BODY_LENGTH_0_ALMOST_DONE;
                        break;
                    case '\n':
                        _chunked_body_state = CHUNKED_BODY_DATA_0;
                        break;
                    default:
                        return 400;
                }
                break;
            case CHUNKED_BODY_LENGTH_0_ALMOST_DONE:
                if (c != '\n')
                    return 400;
                _chunked_body_state = CHUNKED_BODY_DATA_0;
                break;
            case CHUNKED_BODY_DATA_0:
                switch (c) {
                    case '\r':
                        _chunked_body_state = CHUNKED_ALMOST_DONE;
                        break;
                    case '\n':
                        _chunked_body_state = CHUNKED_DONE;
                        return 0;
                    default:
                        return 400;
                }
                break;
            case CHUNKED_ALMOST_DONE:
                if (c != '\n')
                    return 400;
                _chunked_body_state = CHUNKED_DONE;
                return 0;
            case CHUNKED_DONE:
                return 0;
        }
    }
    return 1;
}

inline int Request::_parse_method() {
    switch (_method_end - _method_start) {
        case 3:
            if (_buf.equal(_buf.begin() + _method_start, "GET", 3)) {
                _method = GET;
                break;
            }
            if (_buf.equal(_buf.begin() + _method_start, "PUT", 3)) {
                return 501;
            }
            return 401;
        case 4:
            if (_buf.equal(_buf.begin() + _method_start, "HEAD", 4)) {
                _method = HEAD;
                break;
            }
            if (_buf.equal(_buf.begin() + _method_start, "POST", 4)) {
                _method = POST;
                break;
            }
            return 402;
        case 5:
            if (_buf.equal(_buf.begin() + _method_start, "TRACE", 5)) {
                return 501;
            }
            return 403;
        case 6:
            if (_buf.equal(_buf.begin() + _method_start, "DELETE", 6)) {
                _method = DELETE;
                break;
            }
            return 404;
        case 7:
            if (_buf.equal(_buf.begin() + _method_start, "CONNECT", 7)) {
                return 501;
            }
            if (_buf.equal(_buf.begin() + _method_start, "OPTIONS", 7)) {
                return 501;
            }
            return 405;
    }
    return 0;
}

int Request::_analyze_request() {
    if (_m_header.find("host") == _m_header.end())
        return 400;
    std::map<std::string, std::string>::iterator it_content_len = _m_header.find("content-length");
    if (it_content_len != _m_header.end()) {
        _body_expected_size = atoi(it_content_len->second.c_str());  // error handling? / c style
        // if (_body_expected_size > CLIENTMAXSIZE)
        // return ERROR;
    }
    std::map<std::string, std::string>::iterator it_connection = _m_header.find("connection");
    if (it_connection != _m_header.end()) {
        if (it_connection->second == "close")
            connection_state = CONNECTION_CLOSE;
    }
    std::map<std::string, std::string>::iterator it_transfer_encoding =
        _m_header.find("transfer-encoding");
    if (it_content_len != _m_header.end() && it_transfer_encoding != _m_header.end()) {
        return 400;
    } else if (it_transfer_encoding != _m_header.end()) {
        if (it_transfer_encoding->second == "chunked")
            _chunked_body = true;
        else
            return 501;
    }
    return 0;
}

int Request::parse_request_line() {
    char c;
    int  error;
    for (std::size_t i = _buf.pos; i < _buf.size(); _buf.pos++, i++) {
        c = _buf[i];
        switch (_state_request_line) {
            case START:
                switch (c) {
                    case '\r':
                        break;
                    case '\n':
                        break;
                    default:
                        if (!IS_METHOD_CHAR(c))
                            return 490;
                        _method_start = _buf.pos;
                        _state_request_line = METHOD;
                        break;
                }
                break;
            case METHOD:
                if (IS_METHOD_CHAR(c))
                    break;
                if (c != ' ')
                    return 490;
                _method_end = _buf.pos;
                _state_request_line = AFTER_METHOD;
                error = _parse_method();
                if (error > 0)
                    return error;
                break;
            case AFTER_METHOD:
                switch (c) {
                    case ' ':
                        break;
                    case '/':
                        _uri_start = _uri_path_start = _buf.pos;
                        _state_request_line = URI_SLASH;
                        break;
                    case 'h':
                        _uri_start = _buf.pos;
                        _state_request_line = URI_HT;
                        break;
                    default:
                        return 493;
                }
                break;
            case URI_HT:
                switch (c) {
                    case 't':
                        _state_request_line = URI_HTT;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTT:
                switch (c) {
                    case 't':
                        _state_request_line = URI_HTTP;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTTP:
                switch (c) {
                    case 'p':
                        _state_request_line = URI_HTTP_COLON;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTTP_COLON:
                switch (c) {
                    case ':':
                        _state_request_line = URI_HTTP_COLON_SLASH;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTTP_COLON_SLASH:
                switch (c) {
                    case '/':
                        _state_request_line = URI_HTTP_COLON_SLASH_SLASH;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTTP_COLON_SLASH_SLASH:
                switch (c) {
                    case '/':
                        _state_request_line = URI_HTTP_COLON_SLASH_SLASH_HOST;
                        _uri_host_start = _uri_host_end = _buf.pos + 1;
                        break;
                    default:
                        return 400;
                }
                break;
            case URI_HTTP_COLON_SLASH_SLASH_HOST:
                _uri_host_end = _buf.pos;
                switch (c) {
                    case ' ':
                        _state_request_line = AFTER_URI;
                        break;
                    case '%':
                        _state_request_line = URI_HOST_ENCODE_1;
                        break;
                    case '/':
                        _uri_path_start = _uri_path_end = _buf.pos;
                        _state_request_line = URI_SLASH;
                        break;
                    case ':':
                        _uri_port_start = _uri_port_end = _buf.pos + 1;
                        _state_request_line = URI_HOST_PORT;
                        break;
                    default:
                        if (!IS_HOST_CHAR(c))
                            return 400;
                        break;
                }
                break;
            case URI_HOST_ENCODE_1:
                if (!isxdigit(c))
                    return 400;
                _state_request_line = URI_HOST_ENCODE_2;
                break;
            case URI_HOST_ENCODE_2:
                if (!isxdigit(c))
                    return 400;
                _state_request_line = URI_HTTP_COLON_SLASH_SLASH_HOST;
                break;
            case URI_HOST_PORT:
                _uri_port_end = _buf.pos;
                switch (c) {
                    case ' ':
                        _state_request_line = AFTER_URI;
                        break;
                    case '/':
                        _uri_path_start = _uri_path_end = _buf.pos;
                        _state_request_line = URI_SLASH;
                        break;
                    default:
                        if (!isdigit(c))
                            return 400;
                        break;
                }
                break;
            case URI_SLASH:
                switch (c) {
                    case ' ':
                        _uri_end = _buf.pos;
                        _uri_path_end = _buf.pos;
                        _state_request_line = AFTER_URI;
                        break;
                    case '%':
                        _state_request_line = URI_ENCODE_1;
                        break;
                    case '?':
                        _uri_path_end = _buf.pos;
                        _uri_query_start = _buf.pos + 1;
                        _state_request_line = URI_QUERY;
                        break;
                    case '#':
                        _uri_path_end = _buf.pos;
                        _uri_fragment_start = _buf.pos + 1;
                        _state_request_line = URI_FRAGMENT;
                        break;
                    default:
                        if (!isprint(c))
                            return 494;
                        break;
                }
                break;
            case URI_ENCODE_1:
                if (!isxdigit(c))
                    return 495;
                _state_request_line = URI_ENCODE_2;
                break;
            case URI_ENCODE_2:
                if (!isxdigit(c))
                    return 496;
                _state_request_line = URI_SLASH;
                break;
            case URI_QUERY:
                switch (c) {
                    case ' ':
                        _uri_end = _buf.pos;
                        _uri_query_end = _buf.pos;
                        _state_request_line = AFTER_URI;
                        break;
                    case '#':
                        _uri_query_end = _buf.pos;
                        _uri_fragment_start = _buf.pos + 1;
                        _state_request_line = URI_FRAGMENT;
                    default:
                        if (!isprint(c))
                            return 497;
                        break;
                }
                break;
            case URI_FRAGMENT:
                switch (c) {
                    case ' ':
                        _uri_end = _buf.pos;
                        _uri_fragment_end = _buf.pos;
                        _state_request_line = AFTER_URI;
                        break;
                    default:
                        if (!isprint(c))
                            return 498;
                        break;
                }
                break;
            case AFTER_URI:
                switch (c) {
                    case ' ':
                        break;
                    case 'H':
                        _state_request_line = VERSION_HT;
                        _version_start = _buf.pos;
                        break;
                    default:
                        return 499;
                }
                break;
            case VERSION_HT:
                if (c == 'T')
                    _state_request_line = VERSION_HTT;
                else
                    return 4952;
                break;
            case VERSION_HTT:
                if (c == 'T')
                    _state_request_line = VERSION_HTTP;
                else
                    return 4953;
                break;
            case VERSION_HTTP:
                if (c == 'P')
                    _state_request_line = VERSION_HTTP_SLASH;
                else
                    return 4954;
                break;
            case VERSION_HTTP_SLASH:
                if (c == '/')
                    _state_request_line = VERSION_HTTP_SLASH_MAJOR;
                else
                    return 4955;
                break;
            case VERSION_HTTP_SLASH_MAJOR:
                switch (c) {
                    case '1':
                        _state_request_line = VERSION_HTTP_SLASH_MAJOR_DOT;
                        break;
                    default:
                        if (isdigit(c))
                            return 501;
                        return 4957;
                }
                break;
            case VERSION_HTTP_SLASH_MAJOR_DOT:
                switch (c) {
                    case '.':
                        _state_request_line = VERSION_HTTP_SLASH_MAJOR_DOT_MINOR;
                        break;
                    default:
                        if (isdigit(c))
                            return 501;
                        return 4958;
                }
                break;
            case VERSION_HTTP_SLASH_MAJOR_DOT_MINOR:
                switch (c) {
                    case '1':
                        _state_request_line = AFTER_VERSION;
                        break;
                    default:
                        if (isdigit(c))
                            return 501;
                        return 4959;
                }
                _version_end = _buf.pos + 1;
                break;
            case AFTER_VERSION:
                switch (c) {
                    case ' ':
                        break;
                    case '\r':
                        _state_request_line = ALMOST_DONE;
                        break;
                    case '\n':
                        _state_request_line = DONE;
                        break;
                    default:
                        if (isdigit(c))
                            return 501;
                        return 496;
                }
                break;
            case ALMOST_DONE:
                if (c == '\n')
                    _state_request_line = DONE;
                else
                    return 497;
                break;
            case DONE:
                break;
        }
        if (_state_request_line == DONE) {
            _buf.pos++;
            _request_end = _header_start = _buf.pos;
            return 0;
        }
    }
    return 1;
}

void print_header_pair(const core::ByteBuffer& buf, size_t key_start, size_t key_end,
                       size_t value_start, size_t value_end) {
    std::cout << "Key: \'";
    for (; key_start != key_end; key_start++) {
        std::cout << buf[key_start];
    }
    std::cout << "\'\n";
    std::cout << "Value: \'";
    for (; value_start != value_end; value_start++) {
        std::cout << buf[value_start];
    }
    std::cout << "\'\n\n";
}

int Request::_add_header() {
    std::string key(_buf.begin() + _header_key_start, _buf.begin() + _header_key_end);
    std::transform(key.begin(), key.end(), key.begin(),
                   ::tolower);  // c tolower ???
    std::string value(_buf.begin() + _header_value_start, _buf.begin() + _header_value_end);
    std::pair<std::map<std::string, std::string>::iterator, bool> ret;
    ret = _m_header.insert(std::make_pair(key, value));
    if (ret.second == false) {
        for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
            if (key == headers[i].first)
                return 489;
        }
        (*ret.first).second = value;
    }
    _header_key_start = _header_key_end = 0;
    _header_value_start = _header_value_end = 0;
    return 0;
}

int Request::parse_header() {
    char c;
    int  error;
    for (std::size_t i = _buf.pos; i < _buf.size(); _buf.pos++, i++) {
        c = _buf[i];
        switch (_state_header) {
            case H_KEY_START:
                _header_key_start = _header_key_end = _buf.pos;
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER;
                        break;
                    case '\n':
                        _buf.pos++;
                        _header_end = _buf.pos;
                        return 0;
                    default:
                        if (!IS_TOKEN_CHAR(c))
                            return 481;
                        _state_header = H_KEY;
                        break;
                }
                break;
            case H_KEY:
                _header_key_end = _buf.pos;
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        error = _add_header();
                        if (error)
                            return error;
                        break;
                    case ':':
                        _state_header = H_VALUE_START;
                        break;
                    default:
                        if (!IS_TOKEN_CHAR(c))
                            return 482;
                        break;
                }
                break;
            case H_VALUE_START:
                _header_value_start = _header_value_end = _buf.pos;
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        error = _add_header();
                        if (error)
                            return error;
                        break;
                    case '\t':
                    case ' ':
                        break;
                    default:
                        if (!IS_TEXT_CHAR(c))
                            return 483;
                        _state_header = H_VALUE;
                        break;
                }
                break;
            case H_VALUE:
                _header_value_end = _buf.pos;
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        error = _add_header();
                        if (error)
                            return error;
                        break;
                    default:
                        if (!IS_TEXT_CHAR(c))
                            return 484;
                        break;
                }
                break;
            case H_ALMOST_DONE_HEADER_LINE:
                if (c != '\n')
                    return 485;
                _state_header = H_KEY_START;
                error = _add_header();
                if (error)
                    return error;
                break;
            case H_ALMOST_DONE_HEADER:
                if (c != '\n')
                    return 487;
                _buf.pos++;
                _header_end = _buf.pos;
                return 0;
        }
    }
    return 1;
}

void Request::print() {
    std::cout << "\nREQUEST LINE: \n";
    std::cout << "\'" << log::COLOR_RE;
    for (size_t i = _method_start; i < _method_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    // std::cerr << "Host: " << _uri_host_start << "   " << _uri_host_end << '\n';
    std::cout << "\'" << log::COLOR_PL;
    for (size_t i = _uri_host_start; i < _uri_host_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    // std::cerr << "Port: " << _uri_port_start << "   " << _uri_port_end << '\n';
    std::cout << "\'" << log::COLOR_GR;
    for (size_t i = _uri_port_start; i < _uri_port_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    std::cout << "\'" << log::COLOR_CY;
    for (size_t i = _uri_path_start; i < _uri_path_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    std::cout << "\'" << log::COLOR_GR;
    for (size_t i = _uri_query_start; i < _uri_query_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    std::cout << "\'" << log::COLOR_YE;
    for (size_t i = _uri_fragment_start; i < _uri_fragment_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\' ";

    std::cout << "\'" << log::COLOR_BL;
    for (size_t i = _version_start; i < _version_end; i++) {
        std::cout << _buf[i];
    }
    std::cout << log::COLOR_NO << "\'";
    std::cout << "\n\n";

    if (!_m_header.empty()) {
        std::cout << "HEADER [" << _m_header.size() << "]:\n";
        for (std::map<std::string, std::string>::iterator it = _m_header.begin();
             it != _m_header.end(); it++) {
            std::cout << "\'" << log::COLOR_GR;
            std::cout << (*it).first;
            std::cout << log::COLOR_NO << "\'";
            std::cout << " = ";
            std::cout << "\'" << log::COLOR_BL;
            std::cout << (*it).second;
            std::cout << log::COLOR_NO << "\'";
            std::cout << "\n";
        }
    }
    if (_body_expected_size)
        std::cout << "\nExpected body size: " << _body_expected_size << "\n";
    std::cout << "\n";
}

bool Request::done() { return _state == REQUEST_DONE; }

}  // namespace http
