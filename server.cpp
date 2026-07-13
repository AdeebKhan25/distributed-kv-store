#include <iostream>    
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>   
#include <cstring>
#include <climits>
#include <cerrno>
#include <sstream>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unordered_map>

// define constants
constexpr size_t BUFFER_SIZE = 1024;
constexpr int MAX_EVENTS = 128;

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

// non-blocking helper
bool make_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if(flags == -1) {
        std::cerr << "ERROR: F_GETFL FAILED WITH FD " << fd << " : " << strerror(errno) << std::endl;
        return false;
    }
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "ERROR: F_SETFL FAILED WITH FD " << fd << " : " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

// register new client function
bool register_new_client(int serverSocket, int epfd) {
    int clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket == -1) {
        std::cerr << "ERROR: ACCEPT FAILED: " << strerror(errno) << std::endl;
        return false;
    }
    if(!make_non_blocking(clientSocket)) {
        close(clientSocket);
        return false;
    }
    clients.emplace(clientSocket, Client(clientSocket));
    std::cout << "SUCCESS: CLIENT CONNECTED WITH FD: " << clientSocket << std::endl;
    // register for epoll events
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = clientSocket;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
        std::cerr << "ERROR: EPOLL FD ADDITION FAILED FOR FD " << clientSocket << " : " << strerror(errno) << std::endl;
        close(clientSocket);
        clients.erase(clientSocket);
        return false;
    }
    return true;
}

// client clean up function
bool cleanup_client(int epfd, int client_fd) {
    if(epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, nullptr) == -1) {
        std::cerr << "ERROR: EPOLL FD DELETION FAILED FOR FD " << client_fd << " : " << strerror(errno) << std::endl;
        return false;
    }
    close(client_fd);
    clients.erase(client_fd);
    return true;
}

// command processing
std::string execute_command(
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
        // read till we encounter 'EAGIN' error
        while(true) {
            int bytes = recv(client.fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes > 0) {
                std::cout << "SUCCESS: RECEIVED DATA => " << std::string(buffer, bytes) << std::endl; 
                client.input_buffer.append(buffer, bytes);
            } else if(bytes == 0) {
                std::cout << "STATUS: CLIENT DISCONNECTED" << std::endl; 
                return false;
            } else if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // socket is drained
                break;
            } else {
                std::cerr << "ERROR: RECEIVE FAILED: " << strerror(errno) << std::endl;
                return false;
            }
            // process input
            size_t pos;
            while((pos = client.input_buffer.find('\n')) != std::string::npos) {
                std::string command = client.input_buffer.substr(0, pos);
                client.input_buffer.erase(0, pos + 1);
                std::string output = execute_command(command, store);
                // save output in output buffer
                client.output_buffer.append(output);
            } 
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
        return 1;
    }
    std::cout << "SUCCESS: SERVER STARTED LISTENING ON PORT 8080" << std::endl;
    
    // make listening socket non-blocking
    if(!make_non_blocking(serverSocket)) {
        close(serverSocket);
        return 1;
    }

    // create epoll instance and register server fd 
    int epfd = epoll_create1(0);
    if(epfd == -1) {
        std::cerr << "ERROR: EPOLL CREATION FAILED: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }

    epoll_event listen_event{};
    listen_event.events = EPOLLIN;
    listen_event.data.fd = serverSocket;

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, serverSocket, &listen_event) == -1) {
        std::cerr << "ERROR: EPOLL FD ADDITION FAILED FOR FD " << serverSocket << " : " << strerror(errno) << std::endl;
        close(serverSocket);
        close(epfd);
        return -1;
    }

    // event-driven loop
    epoll_event events[MAX_EVENTS]{};

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if(n == -1) {
            std::cerr << "ERROR: EPOLL WAIT FAILED: " << strerror(errno) << std::endl;
            close(serverSocket);
            close(epfd);
            return -1;
        }

        for(int i = 0; i < n; i++) {
            // process each event
            int fd = events[i].data.fd;
            if(fd == serverSocket) {
                // listening socket
                register_new_client(serverSocket, epfd);
            } else {
                // client socket
                Client& client = clients[fd];
                if(!handle_read(client, store)) {
                    cleanup_client(epfd, fd);
                } else if(!handle_write(client)){
                    cleanup_client(epfd, fd);
                }
            }
        }
    }

    return 0;
}