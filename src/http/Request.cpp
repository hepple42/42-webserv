#include "Request.hpp"

#include "../core/Address.hpp"
#include "../utils/color.hpp"
#include "../utils/str_to_num.hpp"
#include "StatusCodes.hpp"

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
#define HEX_CHAR_TO_INT(c) (isdigit(c) ? c - '0' : tolower(c) - 87)

namespace http {

Request::Request() {}

bool Request::parse(char *read_buf, size_t len, const std::vector<config::Server> &v_server,
                    const core::Address &client_addr) {
    size_t pos = 0;
    if (_state == State::REQUEST_LINE) {
        if (!_parse_request_line(read_buf, len, pos))
            return;
        _analyze_request_line();
    }
    if (_state == HEADER) {
        if (!_parse_header(read_buf, len, pos))
            return;
        _analyze_header();
        _find_server(v_server, client_addr);
        _find_location();
        if (_content == Content::CONT_LENGTH && _location->client_max_body_size < _content_len)
            throw HTTP_CONTENT_TOO_LARGE;
        _body.reserve(_content_len);
        switch (_content) {
            case Content::CONT_LENGTH:
                _state = BODY;
                break;
            case Content::CONT_CHUNKED:
                _state = BODY_CHUNKED;
                break;
            case Content::CONT_NONE:
                _state = DONE;
                break;
        }
    }
    if (_state == BODY) {
        size_t left_len = _content_len - _body.size();
        if (left_len > len - pos)
            left_len = len - pos;
        _body.append(read_buf + pos, left_len);
        if (_body.size() != _content_len)
            return;
        _state = DONE;
    }
    if (_state == BODY_CHUNKED) {
        if (!_parse_body_chunked(read_buf, len, pos))
            return;
        _state = DONE;
    }
    if (_state == DONE) {
        print();
    }
}

bool Request::_parse_request_line(char *read_buf, size_t len, size_t &pos) {
    char c;
    for (; pos < len; pos++, _info_len++) {
        if (_info_len > MAX_INFO_LEN)
            throw HTTP_BAD_REQUEST;
        c = read_buf[pos];
        switch (_state_request_line) {
            case RL_START:
                switch (c) {
                    case '\r':
                        break;
                    case '\n':
                        break;
                    default:
                        if (!IS_METHOD_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _method_str += c;
                        _state_request_line = RL_METHOD;
                        break;
                }
                break;
            case RL_METHOD:
                if (IS_METHOD_CHAR(c) && _method_str.size() < MAX_METHOD_LEN) {
                    _method_str += c;
                    break;
                } else if (c != ' ') {
                    throw HTTP_BAD_REQUEST;
                } else {
                    _state_request_line = RL_AFTER_METHOD;
                    _parse_method();
                    break;
                }
            case RL_AFTER_METHOD:
                switch (c) {
                    case ' ':
                        break;
                    case '/':
                        _path_encoded += c;
                        _state_request_line = RL_URI_PATH;
                        break;
                    case 'h':
                        _state_request_line = RL_URI_HT;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HT:
                switch (c) {
                    case 't':
                        _state_request_line = RL_URI_HTT;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HTT:
                switch (c) {
                    case 't':
                        _state_request_line = RL_URI_HTTP;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HTTP:
                switch (c) {
                    case 'p':
                        _state_request_line = RL_URI_HTTP_COLON;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HTTP_COLON:
                switch (c) {
                    case ':':
                        _state_request_line = RL_URI_HTTP_COLON_SLASH;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HTTP_COLON_SLASH:
                switch (c) {
                    case '/':
                        _state_request_line = RL_URI_HTTP_COLON_SLASH_SLASH;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HTTP_COLON_SLASH_SLASH:
                switch (c) {
                    case '/':
                        _state_request_line = RL_URI_HOST_START;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_URI_HOST_START:
                switch (c) {
                    case '%':
                        _state_request_line = RL_URI_HOST_ENCODE_1;
                        break;
                    default:
                        if (!IS_HOST_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _host_encoded += c;
                        break;
                }
                break;
            case RL_URI_HOST:
                switch (c) {
                    case ' ':
                        _state_request_line = RL_AFTER_URI;
                        break;
                    case '%':
                        _host_encoded += c;
                        _state_request_line = RL_URI_HOST_ENCODE_1;
                        break;
                    case '/':
                        _path_encoded += c;
                        _state_request_line = RL_URI_PATH;
                        break;
                    case ':':
                        _state_request_line = RL_URI_HOST_PORT;
                        break;
                    default:
                        if (!IS_HOST_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _host_encoded += c;
                        break;
                }
                break;
            case RL_URI_HOST_ENCODE_1:
                if (!isxdigit(c))
                    throw HTTP_BAD_REQUEST;
                _host_encoded += c;
                _state_request_line = RL_URI_HOST_ENCODE_2;
                break;
            case RL_URI_HOST_ENCODE_2:
                if (!isxdigit(c))
                    throw HTTP_BAD_REQUEST;
                _host_encoded += c;
                _state_request_line = RL_URI_HOST;
                break;
            case RL_URI_HOST_PORT:
                switch (c) {
                    case ' ':
                        _state_request_line = RL_AFTER_URI;
                        break;
                    case '/':
                        _path_encoded += c;
                        _state_request_line = RL_URI_PATH;
                        break;
                    default:
                        if (!isdigit(c))
                            throw HTTP_BAD_REQUEST;
                        break;
                }
                break;
            case RL_URI_PATH:
                switch (c) {
                    case ' ':
                        _state_request_line = RL_AFTER_URI;
                        break;
                    case '%':
                        _path_encoded += c;
                        _state_request_line = RL_URI_PATH_ENCODE_1;
                        break;
                    case '?':
                        _state_request_line = RL_URI_QUERY;
                        break;
                    case '#':
                        _state_request_line = RL_URI_FRAGMENT;
                        break;
                    default:
                        if (!isprint(c))
                            throw HTTP_BAD_REQUEST;
                        _path_encoded += c;
                        break;
                }
                break;
            case RL_URI_PATH_ENCODE_1:
                if (!isxdigit(c))
                    throw HTTP_BAD_REQUEST;
                _path_encoded += c;
                _state_request_line = RL_URI_PATH_ENCODE_2;
                break;
            case RL_URI_PATH_ENCODE_2:
                if (!isxdigit(c))
                    throw HTTP_BAD_REQUEST;
                _path_encoded += c;
                _state_request_line = RL_URI_PATH;
                break;
            case RL_URI_QUERY:
                switch (c) {
                    case ' ':
                        _state_request_line = RL_AFTER_URI;
                        break;
                    case '#':
                        _state_request_line = RL_URI_FRAGMENT;
                    default:
                        if (!isprint(c))
                            throw HTTP_BAD_REQUEST;
                        _query_string += c;
                        break;
                }
                break;
            case RL_URI_FRAGMENT:
                switch (c) {
                    case ' ':
                        _state_request_line = RL_AFTER_URI;
                        break;
                    default:
                        if (!isprint(c))
                            throw HTTP_BAD_REQUEST;
                        break;
                }
                break;
            case RL_AFTER_URI:
                switch (c) {
                    case ' ':
                        break;
                    case 'H':
                        _state_request_line = RL_VERSION_HT;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_VERSION_HT:
                if (c == 'T')
                    _state_request_line = RL_VERSION_HTT;
                else
                    throw HTTP_BAD_REQUEST;
                break;
            case RL_VERSION_HTT:
                if (c == 'T')
                    _state_request_line = RL_VERSION_HTTP;
                else
                    throw HTTP_BAD_REQUEST;
                break;
            case RL_VERSION_HTTP:
                if (c == 'P')
                    _state_request_line = RL_VERSION_HTTP_SLASH;
                else
                    throw HTTP_BAD_REQUEST;
                break;
            case RL_VERSION_HTTP_SLASH:
                if (c == '/')
                    _state_request_line = RL_VERSION_HTTP_SLASH_MAJOR;
                else
                    throw HTTP_BAD_REQUEST;
                break;
            case RL_VERSION_HTTP_SLASH_MAJOR:
                switch (c) {
                    case '1':
                        _state_request_line = RL_VERSION_HTTP_SLASH_MAJOR_DOT;
                        break;
                    default:
                        if (isdigit(c))
                            throw HTTP_NOT_IMPLEMENTED;
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_VERSION_HTTP_SLASH_MAJOR_DOT:
                switch (c) {
                    case '.':
                        _state_request_line = RL_VERSION_HTTP_SLASH_MAJOR_DOT_MINOR;
                        break;
                    default:
                        if (isdigit(c))
                            throw HTTP_NOT_IMPLEMENTED;
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_VERSION_HTTP_SLASH_MAJOR_DOT_MINOR:
                switch (c) {
                    case '1':
                        _state_request_line = RL_AFTER_VERSION;
                        break;
                    default:
                        if (isdigit(c))
                            throw HTTP_NOT_IMPLEMENTED;
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_AFTER_VERSION:
                switch (c) {
                    case ' ':
                        break;
                    case '\r':
                        _state_request_line = RL_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_request_line = RL_DONE;
                        break;
                    default:
                        if (isdigit(c))
                            throw HTTP_NOT_IMPLEMENTED;
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case RL_ALMOST_DONE:
                if (c == '\n')
                    _state_request_line = RL_DONE;
                else
                    throw HTTP_BAD_REQUEST;
                break;
            case RL_DONE:
                break;
        }
        if (_state_request_line == RL_DONE) {
            pos++;
            return true;
        }
    }
    return false;
}

void Request::_parse_method() {
    switch (_method_str.size()) {
        case 3:
            if (_method_str == "GET")
                _method = Request::Method::GET;
            if (_method_str == "PUT")
                throw HTTP_NOT_IMPLEMENTED;
            break;
        case 4:
            if (_method_str == "HEAD")
                _method = Request::Method::HEAD;
            if (_method_str == "POST")
                _method = Request::Method::POST;
            break;
        case 5:
            if (_method_str == "PATCH")
                throw HTTP_NOT_IMPLEMENTED;
            if (_method_str == "TRACE")
                throw HTTP_NOT_IMPLEMENTED;
            break;
        case 6:
            if (_method_str == "DELETE")
                _method = Request::Method::DELETE;
            break;
        case 7:
            if (_method_str == "CONNECT")
                throw HTTP_NOT_IMPLEMENTED;
            if (_method_str == "OPTIONS")
                throw HTTP_NOT_IMPLEMENTED;
            break;
        default:
            break;
    }
    throw HTTP_BAD_REQUEST;
}

static void uri_decode(std::string &src, std::string &dest) {
    dest.reserve(src.size());
    char c, c_decoded;
    enum StateUriDecode { CHAR, HEX_1, HEX_2 };
    StateUriDecode state = CHAR;
    for (size_t i = 0; i < src.size(); i++) {
        c = src[i];
        switch (state) {
            case CHAR:
                switch (c) {
                    case '%':
                        state = HEX_1;
                        break;
                    default:
                        dest += c;
                        break;
                }
                break;
            case HEX_1:
                c_decoded = HEX_CHAR_TO_INT(c);
                state = HEX_2;
                break;
            case HEX_2:
                c_decoded = c_decoded * 16 + HEX_CHAR_TO_INT(c);
                dest += c_decoded;
                state = CHAR;
                break;
        }
    }
}

static void uri_path_depth_check(std::string &path) {
    char c;
    int  depth = 0;
    enum StatePathCheck { SLASH, SEGMENT, DOT_1, DOT_2 };
    StatePathCheck state = SLASH;
    for (std::size_t i = 0; i < path.size(); i++) {
        c = path[i];
        switch (state) {
            case SLASH:
                switch (c) {
                    case '/':
                        break;
                    case '.':
                        state = DOT_1;
                        break;
                    default:
                        state = SEGMENT;
                        break;
                }
                break;
            case SEGMENT:
                switch (c) {
                    case '/':
                        depth++;
                        state = SLASH;
                        break;
                    default:
                        break;
                }
                break;
            case DOT_1:
                switch (c) {
                    case '/':
                        state = SLASH;
                        break;
                    case '.':
                        state = DOT_2;
                        break;
                    default:
                        state = SEGMENT;
                        break;
                }
                break;
            case DOT_2:
                switch (c) {
                    case '/':
                        depth--;
                        if (depth < 0)
                            throw HTTP_BAD_REQUEST;
                        state = SLASH;
                        break;
                    default:
                        state = SEGMENT;
                        break;
                }
                break;
        }
    }
    if (state == DOT_2 && depth == 0) {
        throw HTTP_BAD_REQUEST;
    }
}

void Request::_analyze_request_line() {
    uri_decode(_path_encoded, _path_decoded);
    uri_decode(_host_encoded, _host_decoded);
    uri_path_depth_check(_path_decoded);
}

void Request::_analyze_header() {
    typedef std::map<std::string, std::string>::const_iterator const_header_it;

    bool host_found = false;
    for (const_header_it it = _m_header.begin(); it != _m_header.end(); it++) {
        if (it->first == "HOST") {
            if (it->second.size() == 0)
                throw HTTP_BAD_REQUEST;
            host_found = true;
            if (_host_decoded.size() == 0)
                _host_decoded = it->second;
        } else if (it->first == "CONTENT-LENGTH") {
            if (_content != Content::CONT_NONE)
                throw HTTP_BAD_REQUEST;
            _content = Content::CONT_LENGTH;
            if (!utils::str_to_num_dec(it->second, _content_len))
                throw HTTP_BAD_REQUEST;
        } else if (it->first == "TRANSFER-ENCODING") {
            if (it->second == "chunked") {
                if (_content != Content::CONT_NONE)
                    throw HTTP_BAD_REQUEST;
                _content = Content::CONT_CHUNKED;
            } else {
                throw HTTP_NOT_IMPLEMENTED;
            }
        } else if (it->first == "CONNECTION") {
            if (it->second == "close") {
                _connection = Connection::CONN_CLOSE;
            } else {
                throw HTTP_BAD_REQUEST;
            }
        }
    }
    if (!host_found)
        throw HTTP_BAD_REQUEST;
}

void Request::_find_server(const std::vector<config::Server> &v_server,
                           const core::Address               &client_addr) {
    typedef std::vector<config::Server>::const_iterator const_server_it;

    _server = NULL;
    for (const_server_it it = v_server.begin(); it != v_server.end(); it++) {
        for (std::size_t i = 0; i < it->v_listen.size(); i++) {
            if (it->v_listen[i].addr == client_addr.addr &&
                it->v_listen[i].port == client_addr.port) {
                if (_server == NULL) {
                    _server = &(*it);
                } else {
                    for (std::size_t j = 0; j < it->v_server_name.size(); j++) {
                        if (it->v_server_name[j] == _host_decoded) {
                            _server = &(*it);
                        }
                    }
                }
                break;
            }
        }
    }
    if (_server == NULL)
        throw HTTP_INTERNAL_SERVER_ERROR;
}

void Request::_find_location() {
    _location = NULL;
    for (std::size_t i = 0; i < _server->v_location.size(); i++) {
        if (_path_decoded.find(_server->v_location[i].path) == 0) {
            if (_location == NULL || _location->path.size() < _server->v_location[i].path.size())
                _location = &(_server->v_location[i]);
        }
    }
    if (_location == NULL)
        throw HTTP_NOT_FOUND;
}

bool Request::_parse_header(char *read_buf, size_t len, size_t &pos) {
    char c;
    for (; pos < len; pos++, _info_len++) {
        if (_info_len > MAX_INFO_LEN)
            throw HTTP_BAD_REQUEST;
        c = read_buf[pos];
        switch (_state_header) {
            case H_KEY_START:
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER;
                        break;
                    case '\n':
                        pos++;
                        return true;
                    default:
                        if (!IS_TOKEN_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _key += c;
                        _state_header = H_KEY;
                        break;
                }
                break;
            case H_KEY:
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        std::transform(_key.begin(), _key.end(), _key.begin(), ::toupper);
                        _m_header.insert(std::make_pair(_key, _value));
                        _key.clear();
                        _value.clear();
                        break;
                    case ':':
                        _state_header = H_VALUE_START;
                        break;
                    default:
                        if (!IS_TOKEN_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _key += c;
                        break;
                }
                break;
            case H_VALUE_START:
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        std::transform(_key.begin(), _key.end(), _key.begin(), ::toupper);
                        _m_header.insert(std::make_pair(_key, _value));
                        _key.clear();
                        _value.clear();
                        break;
                    case '\t':
                    case ' ':
                        break;
                    default:
                        if (!IS_TEXT_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _value += c;
                        _state_header = H_VALUE;
                        break;
                }
                break;
            case H_VALUE:
                switch (c) {
                    case '\r':
                        _state_header = H_ALMOST_DONE_HEADER_LINE;
                        break;
                    case '\n':
                        _state_header = H_KEY_START;
                        std::transform(_key.begin(), _key.end(), _key.begin(), ::toupper);
                        _m_header.insert(std::make_pair(_key, _value));
                        _key.clear();
                        _value.clear();
                        break;
                    default:
                        if (!IS_TEXT_CHAR(c))
                            throw HTTP_BAD_REQUEST;
                        _value += c;
                        break;
                }
                break;
            case H_ALMOST_DONE_HEADER_LINE:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                _state_header = H_KEY_START;
                std::transform(_key.begin(), _key.end(), _key.begin(), ::toupper);
                _m_header.insert(std::make_pair(_key, _value));
                _key.clear();
                _value.clear();
                break;
            case H_ALMOST_DONE_HEADER:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                pos++;
                return true;
        }
    }
    return false;
}

bool Request::_parse_body_chunked(char *read_buf, size_t len, size_t &pos) {
    char c;

    for (; pos < len; pos++) {
        c = read_buf[pos];
        switch (_state_body_chunked) {
            case BC_LENGTH_START:
                switch (c) {
                    case '0':
                        _state_body_chunked = BC_LENGTH_0;
                        break;
                    default:
                        if (!isxdigit(c))
                            throw HTTP_BAD_REQUEST;
                        _state_body_chunked = BC_LENGTH;
                        _chunk_len = HEX_CHAR_TO_INT(c);
                        break;
                }
                break;
            case BC_LENGTH:
                switch (c) {
                    case '\r':
                        _state_body_chunked = BC_LENGTH_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_body_chunked = BC_DATA;
                        break;
                    case ';':
                        _state_body_chunked = BC_LENGTH_EXTENSION;
                        break;
                    default:
                        if (!isxdigit(c))
                            throw HTTP_BAD_REQUEST;
                        _chunk_len = _chunk_len * 16 + HEX_CHAR_TO_INT(c);
                        if (_body.size() + _chunk_len > _location->client_max_body_size)
                            throw HTTP_CONTENT_TOO_LARGE;
                        break;
                }
                break;
            case BC_LENGTH_EXTENSION:
                switch (c) {
                    case '\r':
                        _state_body_chunked = BC_LENGTH_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_body_chunked = BC_DATA;
                        break;
                    default:
                        break;
                }
                break;
            case BC_LENGTH_ALMOST_DONE:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                _state_body_chunked = BC_DATA;
                break;
            case BC_DATA:
                if (_chunk_len > 0) {
                    _body += c;
                    _chunk_len--;
                    break;
                }
                switch (c) {
                    case '\r':
                        _state_body_chunked = BC_DATA_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_body_chunked = BC_LENGTH_START;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case BC_DATA_ALMOST_DONE:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                _state_body_chunked = BC_LENGTH_START;
                break;
            case BC_LENGTH_0:
                switch (c) {
                    case '\r':
                        _state_body_chunked = BC_LENGTH_0_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_body_chunked = BC_DATA_0;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case BC_LENGTH_0_ALMOST_DONE:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                _state_body_chunked = BC_DATA_0;
                break;
            case BC_DATA_0:
                switch (c) {
                    case '\r':
                        _state_body_chunked = BC_ALMOST_DONE;
                        break;
                    case '\n':
                        _state_body_chunked = BC_DONE;
                        break;
                    default:
                        throw HTTP_BAD_REQUEST;
                }
                break;
            case BC_ALMOST_DONE:
                if (c != '\n')
                    throw HTTP_BAD_REQUEST;
                _state_body_chunked = BC_DONE;
                break;
            case BC_DONE:
                break;
        }
        if (_state_body_chunked == BC_DONE) {
            pos++;
            return true;
        }
    }
    return false;
}

void Request::print() const {
    typedef std::map<std::string, std::string>::const_iterator const_header_it;
    std::cout << "REQUEST: \n";

    std::cout << "\tmethod:    " << _method_str << "\n";
    std::cout << "\tpath:      " << _path_decoded << "\n";
    std::cout << "\tquery:     " << _query_string << "\n";
    std::cout << "\thost:      " << _host_decoded << "\n";
    std::cout << "\tHEADER:    \n";
    for (const_header_it it = _m_header.begin(); it != _m_header.end(); it++)
        std::cout << "\t\t" << it->first << ": " << it->second << "\n";
    std::cout << "\tBODY_SIZE: " << _body.size() << "\n";
    std::cout << "\tBODY:      " << '\'' << _body << "\'\n";
}

}  // namespace http
