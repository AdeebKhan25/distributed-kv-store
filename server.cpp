#include <iostream>    
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>   
#include <cstring>
#include <cerrno>
#include <sstream>
#include <unordered_map>

// define constants
#define BUFFER_SIZE 1024

// define storage
std::unordered_map<std::string, std::string> store;

// command processing
std::string process_command(const std::string& command) {
    // create a stream
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if(cmd == "PING") {
        return "PONG\n";
    } 

    else if(cmd == "SET") {
        std::string key, value;
        iss >> key >> value;
        if(key.empty() || value.empty()) {
            return "ERROR\n";
        }
        store[key] = value;
        return "OK\n";
    } 
    
    else if(cmd == "GET") {
        std::string key;
        iss >> key;
        if(store.find(key) != store.end()) {
            return store[key] + "\n";
        }
        return "NULL\n";
    }

    else if(cmd  == "DEL") {
        std::string key;
        iss >> key;
        if(store.erase(key)) {
            return "DELETED\n";
        }
        return "NOT_FOUND\n";
    }

    return "UNKNOWN\n";
}

int main() {
    // create the socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket == -1) {
        std::cerr << "ERROR: SOCKET CREATION FAILED" << std::endl;
        return 1;
    }
    std::cout << "SUCCESS: SOCKET CREATED WITH FILE DESCRIPTOR => " << serverSocket << std::endl;

    // create binding
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    if(bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        std::cerr << "ERROR: BINDING FAILED" << std::endl;
        close(serverSocket);
        return 1;
    }
    std::cout << "SUCCESS: BINDING COMPLETED" << std::endl;

    // start listening
    if(listen(serverSocket, SOMAXCONN) == -1) {
        std::cerr << "ERROR: LISTENING FAILED" << std::endl;
        close(serverSocket);
    }
    std::cout << "SUCCESS: SERVER STARTED LISTENING ON PORT 8080" << std::endl;

    // accept connection
    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            std::cerr << "ERROR: ACCEPT FAILED" << std::endl;
            continue;
        }
        std::cout << "SUCCESS: CLIENT CONNECTED WITH FD: " << clientSocket << std::endl;

        // parsing logic
        std::string accumulator;
        char buffer[BUFFER_SIZE];
        while(true) {
            int bytes = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytes > 0) {
                std::cout << "SUCCESS: RECEIVED DATA => " << std::string(buffer, bytes) << std::endl; 
                accumulator.append(buffer, bytes);
            } else if(bytes == 0) {
                std::cout << "STATUS: CLIENT DISCONNECTED" << std::endl; 
                break;
            } else {
                std::cerr << "ERROR: RECEIVE FAILED" << std::endl;
                break;
            }
            // processing of data
            size_t pos;
            while((pos = accumulator.find('\n')) != std::string::npos) {
                std::string command = accumulator.substr(0, pos);
                accumulator.erase(0, pos + 1);
                std::string output = process_command(command);
                if(send(clientSocket, output.c_str(), output.length(), 0) == -1) {
                    std::cerr << "ERROR: SEND FAILED" << std::endl;
                    break;
                }
            }
        }

        close(clientSocket);
    }

    // close the socket
    if (close(serverSocket) == -1) {
        std::cerr << "ERROR: CLOSE FAILED - " << strerror(errno) << std::endl;
    } else {
        std::cout << "SUCCESS: SOCKET CLOSED" << std::endl;
    }

    return 0;
}