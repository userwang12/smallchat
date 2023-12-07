---
runme:
  id: 01HH2CCD2JEZWN00S0AKF2HTYF
  version: v2.0
---

# SmallChat

说明：本项目基于https://github.com/antirez/smallchat，redis之父的SmallChat的server项目改成的C++项目。

该server项目在linux端编译命令: make          运行命令: ./server

运行客户端需要进入smallchat文件夹中(smallchat文件夹中的代码为redis之父的c语言版本的源代码，仅用于测试服务端代码) 

编译命令：make(生成smallchat-client即客户端程序)

运行命令：./smallchat-client 服务端ip地址(本机则是127.0.0.1) 端口号(7711)
