#ifndef CHATLIB_H
#define CHATLIB_H
//这里我感觉都是封装的调用操作系统api函数
/* Networking. */
int createTCPServer(int port);
int socketSetNonBlockNoDelay(int fd);
int acceptClient(int server_socket);
int TCPConnect(char *addr, int port, int nonblock);

/* Allocation. */
void *chatMalloc(size_t size);
void *chatRealloc(void *ptr, size_t size);

#endif // CHATLIB_H
