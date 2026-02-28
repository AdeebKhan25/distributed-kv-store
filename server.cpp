#include <iostream>    
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>   
#include <cstring>
#include <climits>
#include <cerrno>
#include <sstream>
#include <unordered_map>

// define constants
#define BUFFER_SIZE 1024

// client structure
struct Client {
    int fd;
    std::string input_buffer;
    std::string output_buffer;

    Client() {
        fd = INT_MIN;
        input_buffer = "";
        output_buffer = "";
    }

    Client(int client_fd) {
        fd = client_fd;
        input_buffer = "";
        output_buffer = "";
    }
};

// clients storage
std::unordered_map<int, Client> clients;

// command processing
std::string process_command(
    const std::string& command, 
    std::unordered_map<std::string, std::string>& store) {
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

// read handler
bool handle_read(
    Client& client,
    std::unordered_map<std::string, std::string>& store) {
        // reading logic
        char buffer[BUFFER_SIZE];
        int bytes = recv(client.fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes > 0) {
            std::cout << "SUCCESS: RECEIVED DATA => " << std::string(buffer, bytes) << std::endl; 
            client.input_buffer.append(buffer, bytes);
        } else if(bytes == 0) {
            std::cout << "STATUS: CLIENT DISCONNECTED" << std::endl; 
            return false;
        } else {
            std::cerr << "ERROR: RECEIVE FAILED" << std::endl;
            return false;
        }
        // process input
        size_t pos;
        while((pos = client.input_buffer.find('\n')) != std::string::npos) {
            std::string command = client.input_buffer.substr(0, pos);
            client.input_buffer.erase(0, pos + 1);
            std::string output = process_command(command, store);
            // save output in output buffer
            client.output_buffer.append(output);
        } 
        return true;       
}

// write handler
bool handle_write(Client& client) {
    if(send(client.fd, client.output_buffer.data(), client.output_buffer.size(), 0) == -1) {
        std::cerr << "ERROR: SEND FAILED" << std::endl;
        return false;
    } 
    client.output_buffer.clear();
    return true;
}

int main() {
    // define main storage
    std::unordered_map<std::string, std::string> store;

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
        clients.emplace(clientSocket, Client(clientSocket));
        Client& client = clients[clientSocket];
        std::cout << "SUCCESS: CLIENT CONNECTED WITH FD: " << clientSocket << std::endl;

        // networking logic
        while(true) {
            if(!handle_read(client, store))
                break;
            if(!handle_write(client)) 
                break;
        }

        close(clientSocket);
        clients.erase(clientSocket);
    }

    // close the socket
    if (close(serverSocket) == -1) {
        std::cerr << "ERROR: CLOSE FAILED - " << strerror(errno) << std::endl;
    } else {
        std::cout << "SUCCESS: SOCKET CLOSED" << std::endl;
    }

    return 0;
}