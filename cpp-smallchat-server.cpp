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
                exit(1);
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
        if (FD_ISSET(serversock, &readfds))
        {
            acceptClientConnection();
        }

        char readbuf[256];
        for (auto &client : clients)
        {
            if (client && FD_ISSET(client->getFd(), &readfds))
            {
                int nread = read(client->getFd(), readbuf, sizeof(readbuf) - 1);

                if (nread <= 0)
                {
                    std::cout << "Disconnected client fd=" << client->getFd()
                              << ", nick=" << client->getNick() << std::endl;
                    disconnectClient(client->getFd());
                }
                else
                {
                    readbuf[nread] = '\0';
                    processClientMessage(client, readbuf);
                }
            }
        }
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
        clients[fd].reset();
        numclients--;

        if (fd == maxclient)
        {
            for (int i = maxclient - 1; i >= 0; --i)
            {
                if (clients[i])
                {
                    maxclient = i;
                    break;
                }
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
        std::string msg = sender->getNick() + "> " + message;
        std::cout << msg;

        for (const auto &client : clients)
        {
            if (client && client->getFd() != sender->getFd())
            {
                write(client->getFd(), msg.c_str(), msg.size());
            }
        }
    }
};

int main()
{
    ChatServer server;
    server.run();
    return 0;
}
