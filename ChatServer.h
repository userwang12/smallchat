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
#include<functional>

#define MAX_CLIENT 1024
#define BIND_PORT 7711

class ChatServer;

class Client final
{
    public:
        Client(int sockfd);
        ~Client();
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        template<typename Func>
        void setReadCallback(Func func); //读事件的回调处理函数

        void handleEvent(); //处理就绪事件

        int fd(); // 返回m_fd
        std::string nick(); //返回m_nick
        std::string Buffer(); //返回m_writebuf

        void changeNick(const char* nick); //修改名称
        void changeBuffer(const char* buffer); //重写缓冲区中的数据
    
    private:
        int                   m_fd; 
        std::string           m_nick; //用户名称
        char                  m_writeBuf[1024]; //暂时接收用户发送数据的缓冲区
        std::function<void()> m_readCallback;  //注意这里不能是引用
};

class Acceptor final
{
    public:
        Acceptor(ChatServer* server);
        ~Acceptor();
        Acceptor(const Acceptor&) = delete;
        Acceptor& operator=(const Acceptor&) = delete;

        int  fd(); //返回m_listenfd
        bool isReady(); //返回是否接收值

        bool listenClient(); //开始监听客户端连接
        void setReady(bool ready); //设置开始接收与否
        bool acceptClient(); 
        void addClient(int fd); //更新当前接收的文件描述符到Poller中
        void welcomeClientJoin(int sockfd);
        void reduceClientNum(); 
        
    private:
        ChatServer* m_server;
        int         m_listenfd;
        int         m_clientNum;
        bool        m_isReadyAcc;
};

class Poller final
{
    public:
        Poller();
        ~Poller() = default;
        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        
        void poll(std::vector<std::shared_ptr<Client>>& activeClients, std::shared_ptr<Acceptor> acceptor);
        
        void initMaxFd(int fd);
        void addClient(std::shared_ptr<Client>& ptr, int maxClientFd);
        void rmClient(int fd, int maxClientFd);

    private:
        void fillActiveClients(std::vector<std::shared_ptr<Client>>& activeClients, int num);

    private:
        int                                             m_maxClientFd;
        fd_set                                          m_readfds;
        std::array<std::shared_ptr<Client>, MAX_CLIENT> m_users;
};

class ChatServer final
{//单例模式
    public: 
        ChatServer(const ChatServer &rhs) = delete;
        ChatServer& operator=(const ChatServer& ) = delete;
        ~ChatServer() = default;

        static ChatServer& getInstance();//这里需要声明为静态，否则在类外无法实例化对象
        
        void start();
        void stop();

    private:
        ChatServer();

        void initMaxFd(int fd);
        void addClient(int fd);

        //定制事件响应方法(接收并转发信息函数)
        void forwardMessage(Client* client);
        int  readFromSocket(Client* client); 
        void processCmd(Client* client, char* msg);
        void readMsg(Client* client, char* msg, int nread);
        bool sendMsg(Client* client, int targetFd);
        void freeClient(int fd);
        
    private:
        bool                                            m_isStop; //是否停止运行
        int                                             m_maxClientFd; //当前连接用户对应的最大的fd
        std::unique_ptr<Poller>                         m_poller; //IO对象
        std::shared_ptr<Acceptor>                       m_acceptor; 
        std::array<std::shared_ptr<Client>, MAX_CLIENT> m_users;//跟Poller拿的是同一份Client对象(同一个Client对象两个shared_ptr指向)
        
    friend class Acceptor;
};

#endif //CHATSERVER_H