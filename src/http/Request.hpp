#pragma once

#include <cctype>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../core/ByteBuffer.hpp"

#define IMPLEMENTED true
#define NOT_IMPLEMENTED false

// static const std::pair<std::string, bool> valid_methods[] = {
//     std::make_pair("OPTIONS", NOT_IMPLEMENTED), std::make_pair("GET", IMPLEMENTED),
//     std::make_pair("HEAD", IMPLEMENTED),        std::make_pair("POST", IMPLEMENTED),
//     std::make_pair("PUT", NOT_IMPLEMENTED),     std::make_pair("DELETE", IMPLEMENTED),
//     std::make_pair("TRACE", NOT_IMPLEMENTED),   std::make_pair("CONNECT", NOT_IMPLEMENTED)};

namespace http {

enum method { NONE, GET, HEAD, POST, DELETE };
enum state { REQUEST_LINE, REQUEST_HEADER, REQUEST_BODY, REQUEST_DONE, REQUEST_ERROR };

enum state_request_line {
    START,
    METHOD,
    AFTER_METHOD,
    URI_HT,
    URI_HTT,
    URI_HTTP,
    URI_HTTP_COLON,
    URI_HTTP_COLON_SLASH,
    URI_HTTP_COLON_SLASH_SLASH,
    URI_HTTP_COLON_SLASH_SLASH_HOST,
    URI_HOST_ENCODE_1,
    URI_HOST_ENCODE_2,
    URI_HOST_PORT,
    URI_SLASH,
    URI_ENCODE_1,
    URI_ENCODE_2,
    URI_QUERY,
    URI_FRAGMENT,
    AFTER_URI,
    VERSION_HT,
    VERSION_HTT,
    VERSION_HTTP,
    VERSION_HTTP_SLASH,
    VERSION_HTTP_SLASH_MAJOR,
    VERSION_HTTP_SLASH_MAJOR_DOT,
    VERSION_HTTP_SLASH_MAJOR_DOT_MINOR,
    AFTER_VERSION,
    ALMOST_DONE,
    DONE
};

enum state_header {
    H_KEY_START,
    H_KEY,
    H_VALUE_START,
    H_VALUE,
    H_ALMOST_DONE_HEADER_LINE,
    H_ALMOST_DONE_HEADER
};

enum CONNECTION_STATE { CONNECTION_CLOSE, CONNECTION_KEEP_ALIVE };

class Request {
   public:
    core::ByteBuffer& _buf;

    std::size_t _request_end;
    std::size_t _method_start;
    std::size_t _method_end;
    method      _method;
    std::size_t _version_start;
    std::size_t _version_end;

    std::size_t _uri_start;
    std::size_t _uri_end;
    std::size_t _uri_host_start;
    std::size_t _uri_host_end;
    std::size_t _uri_port_start;
    std::size_t _uri_port_end;
    std::size_t _uri_path_start;
    std::size_t _uri_path_end;
    std::size_t _uri_query_start;
    std::size_t _uri_query_end;
    std::size_t _uri_fragment_start;
    std::size_t _uri_fragment_end;

    std::size_t _header_start;
    std::size_t _header_end;

    std::size_t _header_key_start;
    std::size_t _header_key_end;
    std::size_t _header_value_start;
    std::size_t _header_value_end;

    std::size_t _body_expected_size;

    CONNECTION_STATE connection_state;

    state              _state;
    state_request_line _state_request_line;
    state_header       _state_header;

    std::map<std::string, std::string> _m_header;

    int _add_header();
    int _parse_method();
    int _analyze_request();

   public:
    int status_code;

    Request(core::ByteBuffer& buf);
    ~Request();

    int parse_input();
    int parse_request_line();
    int parse_header();

    void print();
    bool done();
};

}  // namespace http

// REQUEST LINE Parsing
// -

// HEADER Parsing
// - durch den buffer Line by line
// - check if line is too long
// - check if line starts with space, append value to last header
// - check if line contains ':'
// - extract key / value and store it in map

// BODY Parsing
// - check if body size is correct
// - append read calls to buffer

// key: value\r\n
