#ifndef CHATSERVER_H
#define CHATSERVER_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>
#include<fcntl.h>
#include<netinet/tcp.h>
#include<sys/select.h>
#include<stdio.h>
#include<iostream>
#include<array>
#include<memory>
#include<string>
#include<cstring> 
#include<vector>

#define MAX_CLIENT 1024
#define BIND_PORT 7711

class Client final
{
    public:
        Client() = delete;
        Client(int sockfd);
        ~Client() = default;
        bool read();
        bool write(const Client& c);

    private:
        int         m_fd;
        char        m_writeBuf[1024];
        std::string m_nick;
};

class ClientManager final
{//单例模式
    public:
        ClientManager(const ClientManager &rhs) = delete;
        ~ClientManager();
        static ClientManager& getInstance();
        void freeClient(int rmfd);
    
    public:
        int m_clientNumber; //当前连接的用户数量
        int m_maxClientFd; //当前最大的客户文件描述符

        //非要动态创建的内存尽量使用智能指针替代指针，但是这里确实是更需要使用普通指针，
        //因为ClientManage是单例静态的，所以会存在到程序终止，但是指针指向的为客户分配的内存，在程序运行的
        //过程中可能需要动态删除，比如客户端断开连接.
        //std::array<std::shared_ptr<Client>, MAX_CLIENT> user;
        std::array<Client *, MAX_CLIENT> user; 
    
    private:
        ClientManager();
};

class ChatServer final
{//单例模式
    public: 
        ChatServer(const ChatServer &rhs) = delete;
        ~ChatServer();
        static ChatServer& getInstance();//这里需要声明为静态，否则在类外无法实例化对象
        bool initServer();//初始化部分,返回监听文件描述符
        int initSelect();
        bool acceptClient();
        void forwardMessage();
        bool chatRoom();//while循环逻辑部分
    
    private:
        ChatServer() = default;

    private:
        int    m_listenfd;
        fd_set m_readfds;//读文件描述符集合。
};

#endif //CHATSERVER_H