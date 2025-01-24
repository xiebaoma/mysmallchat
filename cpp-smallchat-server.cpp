#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <cstring>
#include <sys/select.h>
#include <unistd.h>
#include "chatlib.h"

#define MAX_CLIENTS 1000
#define SERVER_PORT 7711

class Client
{
public:
    Client(int fd) : fd(fd), nick("user:" + std::to_string(fd))
    {
        socketSetNonBlockNoDelay(fd);
    }

    int getFd() const { return fd; }
    const std::string &getNick() const { return nick; }
    void setNick(const std::string &newNick) { nick = newNick; }

private:
    int fd;
    std::string nick;
};

class ChatServer
{
public:
    ChatServer() : serversock(-1), numclients(0), maxclient(-1)
    {
        clients.resize(MAX_CLIENTS, nullptr);
        serversock = createTCPServer(SERVER_PORT);
        if (serversock == -1)
        {
            perror("Creating listening socket");
            exit(1);
        }
    }

    void run()
    {
        while (true)
        {
            fd_set readfds;
            struct timeval tv;

            FD_ZERO(&readfds);
            FD_SET(serversock, &readfds);

            int maxfd = serversock;
            for (const auto &client : clients)
            {
                if (client)
                {
                    FD_SET(client->getFd(), &readfds);
                    maxfd = std::max(maxfd, client->getFd());
                }
            }

            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int retval = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
            if (retval == -1)
            {
                perror("select() error");
                continue;
            }
            else if (retval > 0)
            {
                handleEvents(readfds);
            }
        }
    }

private:
    int serversock;
    int numclients;
    int maxclient;
    std::vector<std::shared_ptr<Client>> clients;

    void handleEvents(fd_set &readfds)
    {
        // 检查服务器 socket 是否有新连接
        if (FD_ISSET(serversock, &readfds))
        {
            acceptClientConnection();
        }

        char readbuf[256];
        std::vector<int> disconnectedClients; // 记录需要断开的客户端

        for (auto &client : clients)
        {
            if (client && FD_ISSET(client->getFd(), &readfds))
            {
                int nread = read(client->getFd(), readbuf, sizeof(readbuf) - 1);

                if (nread <= 0)
                {
                    if (nread == 0)
                    {
                        std::cout << "[INFO] Client disconnected: fd=" << client->getFd()
                                  << ", nick=" << client->getNick() << std::endl;
                    }
                    else if (nread == -1)
                    {
                        std::cerr << "[ERROR] Read error on fd=" << client->getFd()
                                  << ": " << strerror(errno) << std::endl;
                    }
                    disconnectedClients.push_back(client->getFd());
                }
                else
                {
                    readbuf[nread] = '\0';       // 确保字符串以 '\0' 结尾
                    if (isValidMessage(readbuf)) // 添加输入校验
                    {
                        processClientMessage(client, readbuf);
                    }
                    else
                    {
                        std::cerr << "[WARNING] Invalid message from client fd="
                                  << client->getFd() << ": " << readbuf << std::endl;
                    }
                }
            }
        }

        // 统一处理断开连接的客户端
        for (int fd : disconnectedClients)
        {
            disconnectClient(fd);
        }
    }

    bool isValidMessage(const char *msg)
    {
        // 检查消息是否为空或全为空白字符
        if (!msg || strlen(msg) == 0)
        {
            return false;
        }

        // 检查消息长度是否在合理范围内（防止超长输入）
        if (strlen(msg) > 255)
        {
            return false;
        }

        // 遍历消息，确保没有控制字符（ASCII 范围 0x00 - 0x1F，除了换行符）
        for (size_t i = 0; i < strlen(msg); ++i)
        {
            if (msg[i] < 32 && msg[i] != '\n')
            {
                return false;
            }
        }

        return true; // 通过所有检查认为消息有效
    }

    void acceptClientConnection()
    {
        int fd = acceptClient(serversock);
        auto client = std::make_shared<Client>(fd);
        clients[fd] = client;

        if (fd > maxclient)
            maxclient = fd;
        numclients++;

        const char *welcome_msg = "Welcome to Simple Chat! Use /nick <nick> to set your nick.\n";
        ssize_t bytes_written = write(fd, welcome_msg, strlen(welcome_msg));
        if (bytes_written == -1)
        {
            perror("Failed to send welcome message");
        }

        std::cout << "Connected client fd=" << fd << std::endl;
    }

    void disconnectClient(int fd)
    {
        // 边界检查，确保 fd 在合法范围内
        if (fd < 0 || fd >= static_cast<int>(clients.size()))
        {
            std::cerr << "[ERROR] Invalid client fd=" << fd << " for disconnection." << std::endl;
            return;
        }

        // 检查客户端是否已经断开
        if (!clients[fd])
        {
            std::cerr << "[WARNING] Attempted to disconnect an already disconnected client fd=" << fd << std::endl;
            return;
        }

        // 释放客户端资源
        clients[fd].reset();
        numclients--;

        std::cout << "[INFO] Disconnected client fd=" << fd << ". Remaining clients: " << numclients << std::endl;

        // 更新 maxclient
        if (fd == maxclient)
        {
            maxclient = -1; // 重置为无效值
            for (int i = static_cast<int>(clients.size()) - 1; i >= 0; --i)
            {
                if (clients[i]) // 找到新的最大文件描述符
                {
                    maxclient = i;
                    break;
                }
            }

            if (maxclient == -1)
            {
                std::cout << "[INFO] No active clients remaining." << std::endl;
            }
        }
    }

    void processClientMessage(const std::shared_ptr<Client> &client, const char *message)
    {
        if (message[0] == '/')
        {
            processClientCommand(client, message);
        }
        else
        {
            broadcastMessage(client, message);
        }
    }

    void processClientCommand(const std::shared_ptr<Client> &client, const char *command)
    {
        std::string cmd(command);
        size_t spacePos = cmd.find(' ');

        if (spacePos != std::string::npos)
        {
            std::string cmdName = cmd.substr(0, spacePos);
            std::string arg = cmd.substr(spacePos + 1);

            if (cmdName == "/nick")
            {
                client->setNick(arg);
            }
            else
            {
                const char *errmsg = "Unsupported command\n";
                write(client->getFd(), errmsg, strlen(errmsg));
            }
        }
    }

    void broadcastMessage(const std::shared_ptr<Client> &sender, const char *message)
    {
        // 校验消息有效性
        if (!message || strlen(message) == 0)
        {
            std::cerr << "[WARNING] Attempted to broadcast an empty message from "
                      << sender->getNick() << std::endl;
            return;
        }

        // 构造广播消息
        std::string msg = sender->getNick() + "> " + message;
        std::cout << "[BROADCAST] " << msg << std::endl; // 打印到服务器日志

        std::vector<int> failedClients; // 记录发送失败的客户端

        // 遍历所有客户端并发送消息
        for (const auto &client : clients)
        {
            if (client && client->getFd() != sender->getFd())
            {
                ssize_t bytesWritten = write(client->getFd(), msg.c_str(), msg.size());
                if (bytesWritten == -1) // 检查发送结果
                {
                    std::cerr << "[ERROR] Failed to send message to client fd="
                              << client->getFd() << ": " << strerror(errno) << std::endl;
                    failedClients.push_back(client->getFd()); // 标记为失败
                }
            }
        }

        // 处理发送失败的客户端
        for (int fd : failedClients)
        {
            disconnectClient(fd); // 清理断开的客户端
        }
    }
};

int main()
{
    ChatServer server;
    server.run();
    return 0;
}
