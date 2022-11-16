/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: khirsig <khirsig@student.42heilbronn.de    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/09/09 09:34:22 by khirsig           #+#    #+#             */
/*   Updated: 2022/11/16 09:51:48 by khirsig          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../core/Address.hpp"
#include "../http/error_page.hpp"
#include "../settings.hpp"
#include "Location.hpp"

namespace config {

class Server {
   public:
    Server() : client_max_body_size(SIZE_MAX) {}
    void print() const;

    std::vector<core::Address>        v_listen;
    std::vector<std::string>          v_server_name;
    std::uint64_t                     client_max_body_size;
    std::vector<Location>             v_location;
    std::map<int, http::error_page_t> m_error_codes;
};

}  // namespace config