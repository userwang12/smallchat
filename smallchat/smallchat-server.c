/* smallchat.c -- Read clients input, send to all the other connected clients.
 *
 * Copyright (c) 2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the project name of nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/select.h>
#include <unistd.h>

#include "chatlib.h"

/* ============================ Data structures =================================
 * The minimal stuff we can afford to have. This example must be simple
 * even for people that don't know a lot of C.
 * =========================================================================== */

#define MAX_CLIENTS 1000 // This is actually the higher file descriptor.
#define SERVER_PORT 7711

/* This structure represents a connected client. There is very little
 * info about it: the socket descriptor and the nick name, if set, otherwise
 * the first byte of the nickname is set to 0 if not set.
 * The client can set its nickname with /nick <nickname> command. */
struct client {//客户信息有什么属性，一个就是fd,一个就是名称
    int fd;     // Client socket.
    char *nick; // Nickname of the client.
};

/* This global structure encapsulates the global state of the chat. */
//这个全局结构封装了聊天的全局状态。
struct chatState {//这算是一个服务端全局结构体，里面封装了监听文件描述符
                //客户端当前连接进来的数量，客户端当前连接进来对应文件描述符
                //的最大值，客户信息数组
                //他用一个全局对象来管理所有的client对象
    int serversock;     // Listening server socket.
    int numclients;     // Number of connected clients right now.
    int maxclient;      // The greatest 'clients' slot populated.
    //这个客户对象数组也是提前创建的，但是他对象信息并没有分配内存都是没初始化的
    struct client *clients[MAX_CLIENTS]; // Clients are set in the corresponding
                                         // slot of their socket descriptor.
};

struct chatState *Chat; // Initialized at startup.

/* ====================== Small chat core implementation ========================
 * Here the idea is very simple: we accept new connections, read what clients
 * write us and fan-out (that is, send-to-all) the message to everybody
 * with the exception of the sender. And that is, of course, the most
 * simple chat system ever possible.
 * =========================================================================== */

/* Create a new client bound to 'fd'. This is called when a new client
 * connects. As a side effect updates the global Chat state. */
struct client *createClient(int fd) {
    char nick[32]; // Used to create an initial nick for the user.
    int nicklen = snprintf(nick,sizeof(nick),"user:%d",fd);//初始化客户名称
    //这里这个是另一种写法跟我的服务端写法不同，这里是每连接进来一个客户端，
    //就动态给连接进来的客户端根据文件描述符创建客户信息结构体(对象)，而我的
    //写法是服务端跑起来的时候就预创建所有客户端的对象空间，等客户端连接进来后
    //在给这个对象的属性赋值上对应客户的信息
    struct client *c = chatMalloc(sizeof(*c));//等价于sizeof(struct client)
    socketSetNonBlockNoDelay(fd); // Pretend this will not fail.
    c->fd = fd;
    c->nick = chatMalloc(nicklen+1);
    memcpy(c->nick,nick,nicklen);
    //因为客户的对应的文件描述符唯一，所以通过文件描述符进行定位客户信息
    assert(Chat->clients[c->fd] == NULL); // This should be available.
    Chat->clients[c->fd] = c;
    /* We need to update the max client set if needed. */
    if (c->fd > Chat->maxclient) Chat->maxclient = c->fd;
    Chat->numclients++;
    return c;
}
//这个服务端给我的感觉就是函数封装的非常好，按照一个功能分函数，
//并且需要调用操作系统api的时候单独封装设置一个文件封装操作系统api函数

/* Free a client, associated resources, and unbind it from the global
 * state in Chat. */
//因为在客户连接进来时创建了client对象，那么在客户断开连接的时候就要销毁创建的对象。
void freeClient(struct client *c) {
    free(c->nick);
    close(c->fd);
    Chat->clients[c->fd] = NULL;
    Chat->numclients--;
    if (Chat->maxclient == c->fd) {
        //如果当前服务端中最大的客户端fd是这个要销毁的fd，那么我们要重置
        //服务端当前客户最大的文件描述符符
        /* Ooops, this was the max client set. Let's find what is
         * the new highest slot used. */
        int j;
        for (j = Chat->maxclient-1; j >= 0; j--) {
            if (Chat->clients[j] != NULL) {
                Chat->maxclient = j;
                break;
            }
        }
        //如果已经当前服务端没有客户端连接，就值为-1
        if (j == -1) Chat->maxclient = -1; // We no longer have clients.
    }
    free(c);
}

/* Allocate and init the global stuff. */
void initChat(void) {
    Chat = chatMalloc(sizeof(*Chat));
    memset(Chat,0,sizeof(*Chat));
    /* No clients at startup, of course. */
    Chat->maxclient = -1;//当前最大的客户文件描述符，因为还有有客户端连接进来所以为0
    Chat->numclients = 0;

    /* Create our listening socket, bound to the given port. This
     * is where our clients will connect. */
    Chat->serversock = createTCPServer(SERVER_PORT);//创建服务端监听套接字
    if (Chat->serversock == -1) {
        perror("Creating listening socket");
        exit(1);
    }
}

/* Send the specified string to all connected clients but the one
 * having as socket descriptor 'excluded'. If you want to send something
 * to every client just set excluded to an impossible socket: -1. */
void sendMsgToAllClientsBut(int excluded, char *s, size_t len) {
    for (int j = 0; j <= Chat->maxclient; j++) {//遍历到最大的客户文件描述符
    //遍历过程中，只要客户文件描述符存在并且他不是发消息的文件描述符都发送
        if (Chat->clients[j] == NULL ||
            Chat->clients[j]->fd == excluded) continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
        write(Chat->clients[j]->fd,s,len);
    }
}

/* The main() function implements the main chat logic:
 * 1. Accept new clients connections if any.
 * 2. Check if any client sent us some new message.
 * 3. Send the message to all the other clients. */
int main(void) {
    initChat();//创建好服务端监听套接字

    while(1) {
        fd_set readfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&readfds);
        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */
        FD_SET(Chat->serversock, &readfds);//把监听套接字加入到监听事件集中

        for (int j = 0; j <= Chat->maxclient; j++) {
            if (Chat->clients[j]) FD_SET(j, &readfds);
        }

        /* Set a timeout for select(), see later why this may be useful
         * in the future (not now). */
        tv.tv_sec = 1; // 1 sec timeout
        tv.tv_usec = 0;

        /* Select wants as first argument the maximum file descriptor
         * in use plus one. It can be either one of our clients or the
         * server socket itself. */
        int maxfd = Chat->maxclient;//当前客户连接的最大的文件描述符
        if (maxfd < Chat->serversock) maxfd = Chat->serversock;//如果最大文件描述符小于监听文件描述符就替换
        retval = select(maxfd+1, &readfds, NULL, NULL, &tv);//监听文件描述符集合的读事件
        if (retval == -1) {
            perror("select() error");
            exit(1);
        } else if (retval) {//如果大于0.说明有读事件到来

            /* If the listening socket is "readable", it actually means
             * there are new clients connections pending to accept. */
            if (FD_ISSET(Chat->serversock, &readfds)) {//如果是监听文件描述符有读事件到达
                int fd = acceptClient(Chat->serversock);//接收客户端
                struct client *c = createClient(fd);//为接收进来的客户文件描述符创建客户对象
                /* Send a welcome message. */
                char *welcome_msg =
                    "Welcome to Simple Chat! "
                    "Use /nick <nick> to set your nick.\n";
                write(c->fd,welcome_msg,strlen(welcome_msg));
                printf("Connected client fd=%d\n", fd);
            }

            /* Here for each connected client, check if there are pending
             * data the client sent us. */
            char readbuf[256];
            //遍历一直到当前最大的客户文件描述符
            for (int j = 0; j <= Chat->maxclient; j++) {
                if (Chat->clients[j] == NULL) continue;
                if (FD_ISSET(j, &readfds)) {//如果当前客户文件描述符有读事件
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line is reached. */
                    int nread = read(j,readbuf,sizeof(readbuf)-1);
                    //其实这里感觉有点问题，就是读入如果一次性没有读完数据，那么就有问题
                    //接收客户端的数据，并检验是聊天消息还是命令
                    if (nread <= 0) {//读出nread
                        /* Error or short read means that the socket
                         * was closed. */
                        printf("Disconnected client fd=%d, nick=%s\n",
                            j, Chat->clients[j]->nick);
                        freeClient(Chat->clients[j]);
                    } else {
                        /* The client sent us a message. We need to
                         * relay this message to all the other clients
                         * in the chat. */
                        struct client *c = Chat->clients[j];
                        //这算是一个比较常见的编码习惯，我感觉这样的作用就是
                        //简化编码。
                        readbuf[nread] = 0;//这是一个好习惯

                        /* If the user message starts with "/", we
                         * process it as a client command. So far
                         * only the /nick <newnick> command is implemented. */
                        if (readbuf[0] == '/') {//带有斜杠则代表不发送消息，而是执行特殊的命令
                            /* Remove any trailing newline. */
                            char *p;//去掉读入的换行符
                            p = strchr(readbuf,'\r'); if (p) *p = 0;
                            p = strchr(readbuf,'\n'); if (p) *p = 0;
                            /* Check for an argument of the command, after
                             * the space. */
                            //检查命令后是否有参数
                            char *arg = strchr(readbuf,' ');//检查是否有空格
                            if (arg) {
                                *arg = 0; /* Terminate command name. */
                                arg++; /* Argument is 1 byte after the space. */
                            }//arg就是现在指向参数，空格后面是参数
                            //返回0代表匹配，匹配并且有参数
                            if (!strcmp(readbuf,"/nick") && arg) {//如果是 /nick则代表执行改名命令
                                free(c->nick);//将原来的名字的申请的空间都删除掉
                                int nicklen = strlen(arg);//重新申请一块空间用来填名字
                                c->nick = chatMalloc(nicklen+1);
                                memcpy(c->nick,arg,nicklen+1);
                            } else {
                                /* Unsupported command. Send an error. */
                                char *errmsg = "Unsupported command\n";
                                write(c->fd,errmsg,strlen(errmsg));
                            }
                        } else {
                            /* Create a message to send everybody (and show
                             * on the server console) in the form:
                             *   nick> some message. */
                            char msg[256];
                            int msglen = snprintf(msg, sizeof(msg),
                                "%s> %s", c->nick, readbuf);
                            //把读缓冲区中读到的数据全部放到msg里面，如果读不下就会截断
                            /* snprintf() return value may be larger than
                             * sizeof(msg) in case there is no room for the
                             * whole output. */
                            //读缓冲区数据超出256的情况
                            if (msglen >= (int)sizeof(msg))
                                msglen = sizeof(msg)-1;
                            printf("%s",msg);

                            /* Send it to all the other clients. */
                            sendMsgToAllClientsBut(j,msg,msglen);
                        }
                    }
                }
            }
        } else {
            /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */
        }
    }
    return 0;
}
