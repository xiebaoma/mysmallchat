#ifndef CHATLIB_H
#define CHATLIB_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Networking. */
    int createTCPServer(int port);
    int socketSetNonBlockNoDelay(int fd);
    int acceptClient(int server_socket);
    int TCPConnect(char *addr, int port, int nonblock);

    /* Allocation. */
    void *chatMalloc(size_t size);
    void *chatRealloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif // CHATLIB_H
