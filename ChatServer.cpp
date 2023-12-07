#include"ChatServer.h"

//设置文件描述符非阻塞非延迟
int setNonblockNondelay(int fd)
{
    int old_property = fcntl(fd, F_GETFL);
    int new_property = fcntl(fd, F_SETFL, old_property | O_NONBLOCK);
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

Client::Client(int sockfd) 
{
    m_fd = sockfd;
    m_nick = "client " + std::to_string(sockfd);
    memset(m_writeBuf, 0, sizeof(m_writeBuf));
}

bool Client::read()
{
    //这个read可能是设置命令(改名字)也可能是发送消息
    char msg[1024]={0};
    memset(msg, 0, sizeof(msg));
    int nread = recv(m_fd, msg, sizeof(msg), 0);
    if(nread <= 0)
    {
        printf("client %d close connect...\n", m_fd);
        return false;
    }
    else if(nread)
    {
        //在这里进行判断是进行改名字还是发消息
        if (msg[0] == '/')
        {
            //去除命令中的换行符
            char *target;
            target = strchr(msg,'\r'); if(target) *target = '\0';
            target = strchr(msg,'\n'); if(target) *target = '\0';

            char* args = strchr(msg,' ');
            if(args) 
                *args++ = '\0';

            if(!strcasecmp(msg, "/nick") && args)
            {//改名
                m_nick = args;
            }
            else 
            {
                std::string errstr = "unsupported cmd\n";
                send(m_fd, errstr.c_str(), errstr.size(), 0);
            }
            return true;
        } 
        //发送消息
        memset(m_writeBuf, 0, sizeof(m_writeBuf));
        int len = m_nick.size();
        memcpy(m_writeBuf, m_nick.c_str(), len);
        m_writeBuf[len++] = '>';
        memcpy(m_writeBuf + len, msg, sizeof(m_writeBuf)-len);
        if(nread > sizeof(m_writeBuf))
        {
            nread = sizeof(m_writeBuf)-1;
            m_writeBuf[nread] = '\0';
        }
        //把聊天用户发的消息打印在聊天服务端控制台 
        std::cout << m_writeBuf << std::endl;

        return true;
    }
}

bool Client::write(const Client& c)
{   
    int temp = send(m_fd, c.m_writeBuf, sizeof(c.m_writeBuf), 0);
    if(temp <= 0)
    {
        printf("send to %d failure\n", m_fd);
        return false;
    }
    return true;            
}

ClientManager::ClientManager()
{
    m_clientNumber = 0;
    m_maxClientFd = -1;
    for(int i = 0 ; i < MAX_CLIENT; i++)
    {
        user[i] = nullptr;
    }
}

ClientManager::~ClientManager()
{
    for(int i = 0; i < m_maxClientFd; i++)
    {
        if(user[i] != nullptr)
        {
            delete user[i];
            close(i);
        }
    }
}

ClientManager& ClientManager::getInstance()
{
    static ClientManager manage;
    return manage;
}

void ClientManager::freeClient(int rmfd)
{//这个函数必须需要，因为需要把writefds置空，还需要更新最大值
    if(rmfd == m_maxClientFd)
    {
        for(int i = m_maxClientFd-1; i >= 0; i--)
        {
            if(user[i] != nullptr)
            {
                m_maxClientFd = i;
                break;
            }
        }
    }
    if(user[rmfd] != nullptr)
    {
        delete user[rmfd];
        user[rmfd] = nullptr;
        m_clientNumber--;
    }

    close(rmfd);
}

ChatServer::~ChatServer()
{
    //chatServer对象维护listenfd生命周期
    if(m_listenfd != -1)
    {
        close(m_listenfd);
    }
}

ChatServer& ChatServer::getInstance()
{
    static ChatServer server;
    return server;
}

//创建监听套接字
bool ChatServer::initServer()
{
    m_listenfd=socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1)
    {
        printf("socket failure\n");
        return false;
    }

    int yes = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in saddr;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(BIND_PORT);
    saddr.sin_family = AF_INET;
    int ret = bind(m_listenfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if(ret == -1)
    {
        printf("bind failure\n");
        return false;
    }

    ret=listen(m_listenfd, 5);
    if(ret == -1)
    {
        printf("listen failure\n");
        return false;
    }

    return true;
}

int ChatServer::initSelect()
{
    FD_ZERO(&m_readfds);
    //监听文件描述符和有客户端连接进来对应的文件描述符都要监听读事件
    FD_SET(m_listenfd, &m_readfds);

    int maxfd = ClientManager::getInstance().m_maxClientFd;
    if(maxfd < m_listenfd) maxfd = m_listenfd;
    
    for(int i = 0; i <= maxfd; i++)
    {
        if(ClientManager::getInstance().user[i] == nullptr) continue;
        FD_SET(i, &m_readfds);
    }

    timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    //开始监听,不仅要注册读事件还需要注册写事件
    int num = select(maxfd+1, &m_readfds, nullptr, nullptr, nullptr);
    return num;
}

bool ChatServer::acceptClient()
{
    sockaddr_in cliaddr;
    socklen_t cli_len = sizeof(cliaddr);
    memset(&cliaddr, 0, cli_len);
    while(1)
    {
        int sockfd = accept(m_listenfd, (struct sockaddr *)&cliaddr, &cli_len);
        if(sockfd < 0)
        {
            if(errno == EINTR) 
                continue;//如果程序被中断则继续执行accept
            std::cout << "accept failure!" << std::endl;
            return false;
        }
        
        if(ClientManager::getInstance().m_clientNumber >= MAX_CLIENT)
        {
            std::cout << "Client Number limit!" << std::endl;
            close(sockfd);
            return false;
        }

        setNonblockNondelay(sockfd);

        Client *c = new Client(sockfd);
        ClientManager::getInstance().user[sockfd] = c;
        if(sockfd > ClientManager::getInstance().m_maxClientFd) 
            ClientManager::getInstance().m_maxClientFd = sockfd;
        ClientManager::getInstance().m_clientNumber++;

        std::cout << "sockfd " << sockfd << " join chatRoom" << std::endl;

        std::string msg("welcome to chatroom,/nick is change yourname\n");
        send(sockfd, msg.c_str(), msg.size(), 0);
        return true;
    }
}

void ChatServer::forwardMessage()
{
    for(int j = 0; j <= ClientManager::getInstance().m_maxClientFd; j++)
    {
        if(ClientManager::getInstance().user[j] == nullptr) 
            continue;

        Client& c = *ClientManager::getInstance().user[j];
        //读事件，客户自己发送的数据自己也收不到，所以是先判断读写没区别
        if(FD_ISSET(j, &m_readfds))
        {
            //根据读返回值判断是否要清除客户信息
            if(!c.read())
            {
                ClientManager::getInstance().freeClient(j);//写文件描述符集和读文件描述符集有点不一样
                continue;
            }

            //即时发送
            for(int i = 0; i <= ClientManager::getInstance().m_maxClientFd; i++)
            {
                if(ClientManager::getInstance().user[i] == nullptr || i == j) 
                    continue;
                bool isWrite = ClientManager::getInstance().user[i]->write(c);
                if(!isWrite)
                    ClientManager::getInstance().freeClient(i);
            }//end-inner-for-loop
        }
    }//end-outer-for-loop
}

bool ChatServer::chatRoom()
{
    while(1)
    {
        int num = initSelect();
        if(num < 0)
        {
            if(errno == EINTR) 
                continue;
            printf("select failure\n");
            return false;
        }
        else if(num)
        {
            if(FD_ISSET(m_listenfd, &m_readfds))
            {
                bool isAccept = acceptClient();
                //根据业务需求处理false 或者是 true,这里是不管是true还是false都直接忽略并往下执行
            }
            
            //客户文件描述符处理读事件
            forwardMessage();
        } 
    }
}

int main(int argc,char * argv[])
{
    ChatServer& server = ChatServer::getInstance();
    bool judge = server.initServer();
    if(judge == false)
    {
        delete &server;
        return 0;
    }
    server.chatRoom();
    return 0;
}


