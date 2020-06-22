//
// Small example of a blocking C++ wrapper around a one to one *nix TCP socket
//
// It uses a very simple packet format to send and receive arbitrary amounts of
// data, which could be easily optimised by making the packet length constant.
//
// It can also be fairly easily edited to allow one to many connections.
//

#ifndef _NIX_TCP_HPP
#define _NIX_TCP_HPP

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Error type used by the wrapper
struct TcpError {
    int code;
    std::string message;
};

// Wrapper around a *nix TCP socket
class TcpSocket {
    // Local socket file descriptor
    std::optional<int> sockfd;
    // Remote socket file descriptor
    std::optional<int> remote_sockfd;

    // Packet length
    uint8_t packet_len;

    static void* get_in_addr(struct sockaddr* sa) {
        return sa->sa_family == AF_INET
                   ? (void*)&(((struct sockaddr_in*)sa)->sin_addr)
                   : (void*)&(((struct sockaddr_in6*)sa)->sin6_addr);
    }

  public:
    TcpSocket(uint8_t packet_len) {
        this->sockfd = std::nullopt;
        this->remote_sockfd = std::nullopt;

        this->packet_len = packet_len;
    }
    TcpSocket() : TcpSocket(64) {}

    // Close the sockets on drop
    ~TcpSocket() {
        if (this->is_connected()) {
            close(*this->remote_sockfd);
        }
        if (this->is_bound()) {
            close(*this->sockfd);
        }
    }

    // Whether the socket is currently bound to a port
    bool is_bound() { return this->sockfd.has_value(); }
    // Whether the socket is currently connected to a remote socket
    bool is_connected() { return this->remote_sockfd.has_value(); }

    // Binds the socket to the specified port
    void bind(std::string const& port) {
        if (this->is_bound()) {
            struct TcpError error = {-1, "socket already bound"};
            throw error;
        }

        // Basic information about the socket needed to find a suitable address
        // to bind to
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof hints);
        // IPV4 or IPV6 (read "AF_UNSPEC" as "address family : unspecified")
        hints.ai_family = AF_UNSPEC;
        // TCP (for UDP sockets this would be "SOCK_DATAGRAM")
        hints.ai_socktype = SOCK_STREAM;
        // Choose an IP for us
        hints.ai_flags = AI_PASSIVE;

        // Obtain a linked-list of IP addresses that fit the criteria
        struct addrinfo* server_info;
        auto gai_ret = getaddrinfo(nullptr, port.c_str(), &hints, &server_info);
        if (gai_ret != 0) {
            struct TcpError error = {gai_ret, gai_strerror(gai_ret)};
            throw error;
        }

        // Loop through the list to find a valid IP address to bind to
        struct addrinfo* i;
        for (i = server_info; i != nullptr; i = i->ai_next) {
            // Create a new socket
            this->sockfd = socket(i->ai_family, i->ai_socktype, i->ai_protocol);
            if (this->sockfd == -1) {
                continue;
            }

            // Avoid errors when another socket recently stopped listening on
            // the same port
            int yes = 1;
            if (setsockopt(*this->sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                           sizeof yes) == -1) {
                struct TcpError error = {errno, "couldn't set socket options"};
                throw error;
            }

            // Bind the socket
            if (::bind(*this->sockfd, i->ai_addr, i->ai_addrlen) == -1) {
                close(*this->sockfd);
                continue;
            }

            break;
        }

        // Not calling this would leak the memory used by the list
        freeaddrinfo(server_info);

        // If "i" is still null here, that means no suitable IP addresses were
        // found
        if (i == nullptr) {
            this->sockfd = std::nullopt;

            struct TcpError error = {1, "couldn't bind to any address"};
            throw error;
        }
    }

    // Listen for connections and accept the first incoming one
    void accept() {
        if (!this->is_bound()) {
            struct TcpError error = {-2, "socket unbound"};
            throw error;
        }
        if (this->is_connected()) {
            struct TcpError error = {-1, "socket already connected"};
            throw error;
        }

        // Start listening
        if (listen(*this->sockfd, 1) == -1) {
            struct TcpError error = {errno, "couldn't listen for connections"};
            throw error;
        }

        // Loop until a connection is successfully accepted
        struct sockaddr_storage remote_addr;
        socklen_t sin_len = sizeof remote_addr;
        while (true) {
            // Accept a connection
            this->remote_sockfd = ::accept(
                *this->sockfd, (struct sockaddr*)&remote_addr, &sin_len);

            if (this->remote_sockfd != -1) {
                break;
            }
        }
    }

    void connect(std::string const& remote, std::string const& port) {
        if (!this->is_bound()) {
            struct TcpError error = {-2, "socket unbound"};
            throw error;
        }
        if (this->is_connected()) {
            struct TcpError error = {-1, "socket already connected"};
            throw error;
        }

        // Basic information about the remote
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        // Obtain a linked-list of IP addresses that fit the criteria
        struct addrinfo* server_info;
        auto gai_ret =
            getaddrinfo(remote.c_str(), port.c_str(), &hints, &server_info);
        if (gai_ret != 0) {
            struct TcpError error = {gai_ret, gai_strerror(gai_ret)};
            throw error;
        }

        // Loop through the list to find a valid IP address to connect to
        struct addrinfo* i;
        for (i = server_info; i != nullptr; i = i->ai_next) {
            // Create a new remote socket
            this->remote_sockfd =
                socket(i->ai_family, i->ai_socktype, i->ai_protocol);
            if (this->remote_sockfd == -1) {
                continue;
            }

            // Bind the socket
            if (::connect(*this->remote_sockfd, i->ai_addr, i->ai_addrlen) ==
                -1) {
                close(*this->remote_sockfd);
                continue;
            }

            break;
        }

        // If "i" is still null here, that means no suitable IP addresses were
        // found
        if (i == nullptr) {
            this->remote_sockfd = std::nullopt;

            struct TcpError error = {1, "couldn't connect to any address"};
            throw error;
        }

        // Not calling this would leak the memory used by the list
        freeaddrinfo(server_info);
    }

    // Send data
    void send(std::vector<uint8_t> const& data) {
        if (!this->is_bound()) {
            struct TcpError error = {-2, "socket unbound"};
            throw error;
        }
        if (!this->is_connected()) {
            struct TcpError error = {-2, "socket disconnected"};
            throw error;
        }

        std::vector<uint8_t> packet(this->packet_len, 0);

        // Loop through the data by chunks
        auto data_size = data.size();
        auto offset = 0;
        uint8_t count;
        auto i = 0;
        for (; offset < data_size; offset += count) {
            // Grab a chunk of data
            count = std::min((unsigned long)this->packet_len - 1,
                             data_size - offset);

            // Write the chunk length at the beginning of the packet
            packet[0] = count;
            // Fill the rest of the packet with the chunk
            for (i = 1; i <= count; i++) {
                packet[i] = data[i - 1 + offset];
            }

            // Send the packet
            if (::send(*this->remote_sockfd, packet.data(), this->packet_len,
                       0) == -1) {
                struct TcpError error = {errno, "couldn't send data"};
                throw error;
            }
        }
    }

    std::vector<uint8_t> recv() {
        if (!this->is_bound()) {
            struct TcpError error = {-2, "socket unbound"};
            throw error;
        }
        if (!this->is_connected()) {
            struct TcpError error = {-2, "socket disconnected"};
            throw error;
        }

        std::vector<uint8_t> data;
        std::vector<uint8_t> packet(this->packet_len, 0);

        auto received = 0;
        uint8_t count;
        auto i = 0;
        while (true) {
            // Receive a packet
            received = ::recv(*this->remote_sockfd, packet.data(),
                              this->packet_len, 0);
            if (received == -1) {
                struct TcpError error = {errno, "couldn't send data"};
                throw error;
            } else if (received != this->packet_len) {
                struct TcpError error = {1, "invalid received packet length"};
                throw error;
            }

            // Extract the chunk length
            count = packet[0];
            // Append the chunk to the data
            for (i = 1; i <= count; i++) {
                data.push_back(packet[i]);
            }

            // If the chunk length is smaller than the max length it was the
            // last packet
            if (count < this->packet_len - 1) {
                break;
            }
        }

        return data;
    }
};

#endif
