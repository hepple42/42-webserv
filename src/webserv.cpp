/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webserv.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: khirsig <khirsig@student.42heilbronn.de    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/09/20 09:56:29 by khirsig           #+#    #+#             */
/*   Updated: 2022/10/06 13:31:44 by tjensen          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <vector>

#include "config/Parser.hpp"
#include "core/Connections.hpp"
#include "core/EventNotificationInterface.hpp"
#include "core/Socket.hpp"
#include "log/Log.hpp"

// #define DEBUG_CONFIG_PARSER

int main(int argc, char* argv[]) {
    std::string file_path;
    if (argc == 1)
        file_path = "./webserv.conf";
    else if (argc == 2)
        file_path = argv[1];
    else
        return 1;

    config::Parser              parser;
    std::vector<config::Server> v_server;

    parser.parse(file_path, v_server);

#ifdef DEBUG_CONFIG_PARSER
    std::cout << "\n\n";

    for (std::vector<config::Server>::iterator it = v_server.begin(); it != v_server.end(); ++it) {
        it->print();
        std::cout << "\n\n";
    }
#endif

    core::EventNotificationInterface eni;
    std::vector<core::Socket>        v_socket;

    for (std::vector<config::Server>::iterator it_server = v_server.begin();
         it_server != v_server.end(); ++it_server) {
        for (std::vector<config::Listen>::iterator it_listen = it_server->v_listen.begin();
             it_listen != it_server->v_listen.end(); ++it_listen) {
            // v_socket.insert(
            //     std::make_pair(it_listen->port, core::Socket(it_listen->addr, it_listen->port)));
            v_socket.push_back(core::Socket(it_listen->addr, it_listen->port));
        }
    }

    for (std::vector<core::Socket>::iterator it = v_socket.begin(); it != v_socket.end(); ++it) {
        eni.add_event(it->fd, EVFILT_READ, 0);
    }

    core::Connections connections(1024);
    while (42) {
        int num_events = eni.poll_events();
        if (num_events == -1) {
            std::cerr << "poll_events: " << strerror(errno) << '\n';
            // log::error(log::SYSTEM, "poll_events: ", strerror(errno), "");
            continue;
        }
        for (int i = 0; i < num_events; i++) {
            if (eni.events[i].flags & EV_ERROR) {
                std::cerr << "kevent() error" << strerror(errno) << " on " << eni.events[i].ident
                          << '\n';
            } else if (std::find(v_socket.begin(), v_socket.end(), eni.events[i].ident) !=
                       v_socket.end()) {
                connections.accept_connection(eni.events[i].ident, eni);
            } else if (eni.events[i].filter == EVFILT_TIMER) {
                if (eni.events[i].filter == EVFILT_READ)
                    connections.receive(eni.events[i].ident, eni);
                connections.timeout_connection(eni.events[i].ident, eni);
            } else if (eni.events[i].flags & EV_EOF) {
                if (eni.events[i].filter == EVFILT_READ)
                    connections.receive(eni.events[i].ident, eni);
                connections.close_connection(eni.events[i].ident, eni);
            } else if (eni.events[i].filter == EVFILT_WRITE) {
                // write(eni.events[i].ident, "RESPONSE WRITE\n", 15);
                // eni.delete_event(eni.events[i].ident, EVFILT_WRITE);
                // eni.add_event(eni.events[i].ident, EVFILT_READ, 0);
            } else if (eni.events[i].filter == EVFILT_READ) {
                connections.receive(eni.events[i].ident, eni);
            }
        }
    }
    return EXIT_SUCCESS;
}
