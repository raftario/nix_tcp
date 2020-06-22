#include "nix_tcp.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

void thread1() {
    try {
        TcpSocket sck(32);
        sck.bind("1234");

        // 2 - Accept the connection
        sck.accept();

        std::vector<uint8_t> data;
        for (auto i = 0; i < 48; i++) {
            data.push_back(i);
        }

        // 3 - Send data
        sck.send(data);

        // 6 - Receive data
        data = sck.recv();

        std::cout << "Thread 1 received " << data.size() << " bytes of data"
                  << std::endl;
        for (auto i = 0; i < data.size(); i++) {
            std::cout << std::setw(2) << (int)data[i] << " ";
            if ((i + 1) % 8 == 0) {
                std::cout << std::endl;
            }
        }
        std::cout << std::endl;
    } catch (TcpError err) {
        std::cout << "Thread 1 error [" << err.code << "] " << err.message
                  << std::endl;
        std::abort();
    }
}

void thread2() {
    try {
        TcpSocket sck(32);
        sck.bind("4321");

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 1 - Connect
        sck.connect("localhost", "1234");

        // 4 - Receive data
        auto data = sck.recv();

        std::cout << "Thread 2 received " << data.size() << " bytes of data"
                  << std::endl;
        for (auto i = 0; i < data.size(); i++) {
            std::cout << std::setw(2) << (int)data[i] << " ";
            if ((i + 1) % 8 == 0) {
                std::cout << std::endl;
            }

            data[i] *= 2;
        }
        std::cout << std::endl;

        // 5 - Send data
        sck.send(data);
    } catch (TcpError err) {
        std::cout << "Thread 2 error [" << err.code << "] " << err.message
                  << std::endl;
        std::abort();
    }
}

int main() {
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
}
