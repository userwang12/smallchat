#include"ChatServer.h"

//设置文件描述符非阻塞非延迟
int setNonblockNondelay(int fd)
{
    int old_property = fcntl(fd, F_GETFL);
    int new_property = fcntl(fd, F_SETFL, old_property | O_NONBLOCK);
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

//////////////////////Client类
Client::Client(int sockfd) 
{
    m_fd = sockfd;
    m_nick = "client " + std::to_string(sockfd);
    memset(m_writeBuf, 0, sizeof(m_writeBuf));
}

Client::~Client()
{
    close(m_fd);
}

template<typename Func>
void Client::setReadCallback(Func func) 
{                          
    m_readCallback = [=]() { //传入进来的外部变量必须是传拷贝，传入的函数对象是临时的
        func(this);
    };
}

void Client::handleEvent()
{
    //这里只用处理接收到来自客户端的数据一种情况(也就是读事件)
    if(m_readCallback) 
        m_readCallback();
}

int Client::fd()
{
    return m_fd;
}

std::string Client::nick()
{
    return m_nick;
}

std::string Client::Buffer()
{
    return m_writeBuf;
}

void Client::changeNick(const char* nick)
{
    m_nick = nick;
}

void Client::changeBuffer(const char* buffer)
{
    memset(m_writeBuf, 0, sizeof(m_writeBuf));
    memcpy(m_writeBuf, buffer, sizeof(m_writeBuf));
}

//////////////这里是Acceptor类
Acceptor::Acceptor(ChatServer* server) 
    : m_server(server),
      m_listenfd(-2),
      m_clientNum(0),
      m_isReadyAcc(false)
{
}

Acceptor::~Acceptor()
{   
    close(m_listenfd);
}

int Acceptor::fd()
{
    if(m_listenfd == -2) 
        std::cout << "listenfd not initialized" << std::endl;
    
    return m_listenfd;
}

bool Acceptor::isReady()
{
    return m_isReadyAcc;
}

bool Acceptor::listenClient()  //返回false代表创建listenfd失败
{
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_listenfd == -1)
    {
        std::cout << "socket failure" << std::endl;
        return false;
    }

    int yes = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in saddr;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(BIND_PORT);
    saddr.sin_family = AF_INET;
    int ret = bind(m_listenfd, (struct sockaddr*)&saddr, sizeof(saddr));
    if(ret == -1)
    {
        std::cout << "bind failure" << std::endl;
        return false;
    }

    ret = listen(m_listenfd, 5);
    if(ret == -1)
    {
        std::cout << "listen failure" << std::endl;
        return false;
    }

    m_server->initMaxFd(m_listenfd);

    return true;
}

void Acceptor::setReady(bool ready)
{
    m_isReadyAcc = ready;
}

bool Acceptor::acceptClient()
{
    sockaddr_in cliaddr;
    socklen_t cli_len = sizeof(cliaddr);
    memset(&cliaddr, 0, cli_len);

    int sockfd;
    while(1)
    {
        sockfd = accept(m_listenfd, (struct sockaddr*)&cliaddr, &cli_len);
        if(sockfd < 0)
        {
            if(errno == EINTR)  //如果accept是被信号意外中断则重新accept
                continue;
            std::cout << "accept failure!" << std::endl;
            return false;
        }

        break;
    }

    if(m_clientNum == MAX_CLIENT)
    {
        std::cout << "Client Number limit!" << std::endl;
        close(sockfd);
        return false;
    }

    setNonblockNondelay(sockfd);
    
    addClient(sockfd);
    m_clientNum++;

    welcomeClientJoin(sockfd);

    setReady(false); 
    
    return true;
}

void Acceptor::addClient(int fd) 
{
    m_server->addClient(fd);
}

void Acceptor::welcomeClientJoin(int sockfd)
{
    std::cout << "welcome sockfd: " << sockfd << " join chatRoom" << std::endl;

    std::string msg("welcome to chatroom, /nick is change yourname\n");
    send(sockfd, msg.c_str(), msg.size(), 0);
}

void Acceptor::reduceClientNum()
{
    m_clientNum--;
} 

//////////////这里是Poller类
Poller::Poller() : m_maxClientFd(-1){}

void Poller::poll(std::vector<std::shared_ptr<Client>>& activeClients, std::shared_ptr<Acceptor> acceptor) //不使用引用是防止被误删资源
{
    FD_ZERO(&m_readfds);
    
    FD_SET(acceptor->fd(), &m_readfds);
    for(int i = 0; i <= m_maxClientFd; i++) //listenfd不由m_users管
    {
        if(m_users[i] && i != acceptor->fd()) //如果这个客户端对象存在就需要监听他的事件
            FD_SET(i, &m_readfds);
    }

    //开始监听
    int num = select(m_maxClientFd + 1, &m_readfds, nullptr, nullptr, nullptr);
    if(num > 0)
    {
        if(FD_ISSET(acceptor->fd(), &m_readfds))
        {
            num--;
            acceptor->setReady(true);
        }   

        fillActiveClients(activeClients, num);
    }
    else 
    {
        std::cout << "select error" << std::endl;
        return ;
    }
}

//填充事件准备就绪的client对象
void Poller::fillActiveClients(std::vector<std::shared_ptr<Client>>& activeClients, int num) 
{
    //填充activeClients
    for(int i = 0; i <= m_maxClientFd && num > 0; i++)
    {
        if(m_users[i] && FD_ISSET(i, &m_readfds))
        {
            num--;
            activeClients.push_back(m_users[i]);
        }
    }
}

void Poller::initMaxFd(int listenfd)
{
    m_maxClientFd = listenfd;
}

void Poller::addClient(std::shared_ptr<Client>& ptr, int maxClientFd) 
{
    //添加监听客户端
    m_users[ptr->fd()] = ptr;
    m_maxClientFd = maxClientFd;
}

void Poller::rmClient(int fd, int maxClientFd)
{
    m_users[fd].reset();
    m_maxClientFd = maxClientFd;
}

////////////从这里开始ChatServer类
ChatServer::ChatServer() 
{   
    m_maxClientFd = -2;
    m_acceptor = std::make_shared<Acceptor>(this);
    m_poller = std::make_unique<Poller>();
}

ChatServer& ChatServer::getInstance()
{
    static ChatServer server;
    return server;
}

void ChatServer::initMaxFd(int listenfd)
{
    m_maxClientFd = listenfd;
    m_poller->initMaxFd(listenfd);
}

void ChatServer::addClient(int fd)
{
    //添加客户端addClient
    m_users[fd] = std::make_shared<Client>(fd);
    m_users[fd]->setReadCallback(std::bind(&ChatServer::forwardMessage, this, std::placeholders::_1));
    
    if(fd > m_maxClientFd)
        m_maxClientFd = fd;

    m_poller->addClient(m_users[fd], m_maxClientFd);
}

void ChatServer::forwardMessage(Client* client) //由client对象调用,调用该函数的client的读事件就绪
{
    //1.read()消息 2.将消息转发(通过m_users,还有m_maxClientFd)
    int Flag = readFromSocket(client);
    if(Flag == 1) 
    {
        //转发消息
        for(int i = 0; i <= m_maxClientFd; i++) 
        {
            if(m_users[i] == nullptr || i == client->fd()) 
                continue;
            if(!sendMsg(client, i)) //发送消息失败
            {
                //释放连接客户端资源
                freeClient(i);
            }
        }
    }    
    else if(Flag == -1)
    {
        //读取消息失败，释放连接客户端资源
        freeClient(client->fd());
    }
    
}

int ChatServer::readFromSocket(Client* client) //-1代表出错或断开连接  0代表设置指令(改名)或没读到数据  1代表读取到数据
{
    if(!client) //防止访问空指针
    {
        std::cout << "readFd Empty Client" << std::endl;
        return -1; 
    }

    //这个read可能是设置命令(改名字)也可能是发送消息
    char msg[1024]={0};
    memset(msg, 0, sizeof(msg));
    int nread = recv(client->fd(), msg, sizeof(msg), 0);
    if(nread == -1)
    {
        if(errno == EAGAIN || errno == EINTR)
            return 0;
        return -1;
    }
    else if(nread == 0)
    {
        printf("client %d close connect...\n", client->fd());
        return -1;
    }
    
    //在这里进行判断是进行改名字还是发消息
    if (msg[0] == '/')
    {
        processCmd(client, msg);
        return 0;
    } 

    //发送消息
    readMsg(client, msg, nread);

    return 1;
}

void ChatServer::processCmd(Client* client, char* msg)
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
        client->changeNick(args);
        std::string info = "change nick success!\n";
        send(client->fd(), info.c_str(), info.size(), 0);
    }
    else 
    {
        std::string errstr = "unsupported cmd\n";
        send(client->fd(), errstr.c_str(), errstr.size(), 0);
    }
}

void ChatServer::readMsg(Client* client, char* msg, int nread)
{
    char writeBuf[1024];
    memset(writeBuf, 0, sizeof(writeBuf));

    int len = client->nick().size();
    memcpy(writeBuf, client->nick().c_str(), len);
    writeBuf[len++] = '>';
    memcpy(writeBuf + len, msg, sizeof(writeBuf) - len);

    if(nread > sizeof(writeBuf)) //当用户发送数据太长时截断，防止操作未分配内存
    {
        nread = sizeof(writeBuf) - 1;
        writeBuf[nread] = '\0';
    }

    //把聊天用户发的消息打印在聊天服务端控制台 
    std::cout << writeBuf << std::endl;

    client->changeBuffer(writeBuf);
}

bool ChatServer::sendMsg(Client* client, int targetFd)
{
    int tmp = send(targetFd, client->Buffer().c_str(), client->Buffer().size(), 0);
    if(tmp <= 0)
    {
        std::cout << "send to " << targetFd << " failure!" << std::endl;
        return false;
    }
    return true;
}

void ChatServer::freeClient(int fd)
{
    if(fd == m_maxClientFd) 
    {
        //更新最大fd
        for(int i = m_maxClientFd - 1; i >= 0; i--)
        {
            if(m_users[i] != nullptr)
            {
                m_maxClientFd = i;
                break;
            }
        }
    }

    //释放fd相关资源
    m_users[fd].reset();
    
    m_poller->rmClient(fd, m_maxClientFd); 
    m_acceptor->reduceClientNum(); //减少客户端数量
}

void ChatServer::start()
{
    m_isStop = false;
    if(!m_acceptor->listenClient())
    {
        std::cout << "create listenfd false" << std::endl;
        return ;
    }

    while(!m_isStop)
    {
        std::vector<std::shared_ptr<Client>> activeClients;
        m_poller->poll(activeClients, m_acceptor);

        if(m_acceptor->isReady()) //listenfd就绪
        {
            if(!m_acceptor->acceptClient())
            {
                std::cout << "accept Client failure!" << std::endl;
            }
        }

        for(std::shared_ptr<Client>& client : activeClients)
        {
            client->handleEvent();  
        } 
    }

}

void ChatServer::stop()
{
    m_isStop = true;
}

int main(int argc,char * argv[])
{
    ChatServer::getInstance();
    ChatServer::getInstance().start();
    
    return 0;
}


