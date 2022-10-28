#pragma once

#include <map>
#include <string>

#include "../config/Location.hpp"
#include "../core/ByteBuffer.hpp"

namespace http {

class Request {
   public:
    enum Method { GET, POST, DELETE, HEAD };

   private:
    enum State { REQUEST_LINE, HEADER, BODY, BODY_CHUNKED, DONE };

    enum StateRequestLine {
        RL_START,
        RL_METHOD,
        RL_AFTER_METHOD,
        RL_URI_HT,
        RL_URI_HTT,
        RL_URI_HTTP,
        RL_URI_HTTP_COLON,
        RL_URI_HTTP_COLON_SLASH,
        RL_URI_HTTP_COLON_SLASH_SLASH,
        RL_URI_HOST_START,
        RL_URI_HOST,
        RL_URI_HOST_ENCODE_1,
        RL_URI_HOST_ENCODE_2,
        RL_URI_HOST_PORT,
        RL_URI_PATH,
        RL_URI_PATH_ENCODE_1,
        RL_URI_PATH_ENCODE_2,
        RL_URI_QUERY,
        RL_URI_FRAGMENT,
        RL_AFTER_URI,
        RL_VERSION_HT,
        RL_VERSION_HTT,
        RL_VERSION_HTTP,
        RL_VERSION_HTTP_SLASH,
        RL_VERSION_HTTP_SLASH_MAJOR,
        RL_VERSION_HTTP_SLASH_MAJOR_DOT,
        RL_VERSION_HTTP_SLASH_MAJOR_DOT_MINOR,
        RL_AFTER_VERSION,
        RL_ALMOST_DONE,
        RL_DONE
    };

    enum StateHeader {
        H_KEY_START,
        H_KEY,
        H_VALUE_START,
        H_VALUE,
        H_ALMOST_DONE_HEADER_LINE,
        H_ALMOST_DONE_HEADER
    };

    enum Content { CONT_NONE, CONT_LENGTH, CONT_CHUNKED };

    enum Connection { CONN_CLOSE, CONN_KEEP_ALIVE };

   private:
    Method      _method;
    std::string _method_str;
    std::string _path_encoded;
    std::string _path_decoded;
    std::string _query_string;
    std::string _host_encoded;
    std::string _host_decoded;
    std::string _key;
    std::string _value;

    std::map<std::string, std::string> _m_header;
    core::ByteBuffer                   _body;

    const config::Location *_location;
    const config::Server   *_server;

    State            _state;
    StateRequestLine _state_request_line;
    StateHeader      _state_header;

    Content _content;
    size_t  _content_length;

    Connection _connection;  // naming ??! same as connection from webserver

    bool _parse_request_line(char *read_buf, size_t len, size_t &pos);
    void _parse_method();
    void _analyze_request_line();
    void _analyze_header();
    bool _parse_header(char *read_buf, size_t len, size_t &pos);
    bool _parse_body(char *read_buf, size_t len, size_t &pos);
    bool _parse_body_chunked(char *read_buf, size_t len, size_t &pos);
    bool _finalize();

   public:
    bool parse(char *read_buf, size_t len);
};

}  // namespace http