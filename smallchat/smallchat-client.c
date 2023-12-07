/* smallchat-client.c -- Client program for smallchat-server.
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
#include <termios.h>
#include <errno.h>

#include "chatlib.h"

/* ============================================================================
 * Low level terminal handling.
 * 低级终端处理
 * ========================================================================== */

void disableRawModeAtExit(void);

/* Raw mode: 1960 magic shit. */
int setRawMode(int fd, int enable) {
    /* We have a bit of global state (but local in scope) here.
     * This is needed to correctly set/undo raw mode. */
    static struct termios orig_termios; // Save original terminal status here.
    static int atexit_registered = 0;   // Avoid registering atexit() many times.
    static int rawmode_is_set = 0;      // True if raw mode was enabled.

    struct termios raw;

    /* If enable is zero, we just have to disable raw mode if it is
     * currently set. */
    if (enable == 0) {
        /* Don't even check the return value as it's too late. */
        if (rawmode_is_set && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
            rawmode_is_set = 0;
        return 0;
    }

    /* Enable raw mode. */
    if (!isatty(fd)) goto fatal;
    if (!atexit_registered) {
        atexit(disableRawModeAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - do nothing. We want post processing enabled so that
     * \n will be automatically translated to \r\n. */
    // raw.c_oflag &= ...
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * but take signal chars (^Z,^C) enabled. */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode_is_set = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* At exit we'll try to fix the terminal to the initial conditions. */
void disableRawModeAtExit(void) {
    setRawMode(STDIN_FILENO,0);
}

/* ============================================================================
 * Mininal line editing.
 * ========================================================================== */

void terminalCleanCurrentLine(void) {
    //\e表示ascii的转义字符，其ascii码为27，表示转义序列的开始
    //[2K 这是ansi转义荀烈的一部分，表示清除当前行
    //[ 表示序列的开始
    //2 表示清除行的操作、
    //K 表示清除行的末尾(从光标位置到行末尾)
    write(fileno(stdout),"\e[2K",4);
    //这样的操作通常用于在终端上重新绘制或者更新一行文本内容，已实现某些交互式界面效果，比如
    //清除之前的输入以显示新的输入
}

void terminalCursorAtLineStart(void) {
    write(fileno(stdout),"\r",1);
}

#define IB_MAX 128
struct InputBuffer {
    char buf[IB_MAX];       // Buffer holding the data.
    int len;                // Current length.
};

/* inputBuffer*() return values: */
#define IB_ERR 0        // Sorry, unable to comply.
#define IB_OK 1         // Ok, got the new char, did the operation, ...
#define IB_GOTLINE 2    // Hey, now there is a well formed line to read.

/* Append the specified character to the buffer. */
int inputBufferAppend(struct InputBuffer *ib, int c) {
    if (ib->len >= IB_MAX) return IB_ERR; // No room.

    ib->buf[ib->len] = c;
    ib->len++;
    return IB_OK;
}

void inputBufferHide(struct InputBuffer *ib);
void inputBufferShow(struct InputBuffer *ib);

/* Process every new keystroke arriving from the keyboard. As a side effect
 * the input buffer state is modified in order to reflect the current line
 * the user is typing, so that reading the input buffer 'buf' for 'len'
 * bytes will contain it. */
int inputBufferFeedChar(struct InputBuffer *ib, int c) {
    switch(c) {//我猜是将读到buf中的数据逐个解析，
    case '\n':
        break;          // Ignored. We handle \r instead.
    case '\r':
        return IB_GOTLINE;//当
    case 127:           // Backspace.
        if (ib->len > 0) {//输入缓冲区缓存，
            ib->len--;
            //我懂这里的逻辑了，就是清除所有行之后，再进行写操作，只是少写一个字符罢了
            inputBufferHide(ib);//一定遇上一个退格就将stdout中的数据清除，并将光标定位到开头
            inputBufferShow(ib);//将ib->len长度的ib->buf打印到终端
        }
        break;
    default:
    
        if (inputBufferAppend(ib,c) == IB_OK)
            write(fileno(stdout),ib->buf+ib->len-1,1);//将ib->buf的最后一个字符串写入
        //上面这里stdout为什么总是最后一个字符最后一个字符的读入，我觉得有两个原因，因为添加的时候添加的
        //就是最后一个字符，还有就是为了函数复用，
        break;
    }
    return IB_OK;
}

/* Hide the line the user is typing. */
void inputBufferHide(struct InputBuffer *ib) {
    (void)ib; // Not used var, but is conceptually part of the API.
    terminalCleanCurrentLine();//清除终端当前行的内容
    terminalCursorAtLineStart();//将光标定位到当前行的开头
}

/* Show again the current line. Usually called after InputBufferHide(). */
void inputBufferShow(struct InputBuffer *ib) {
    write(fileno(stdout),ib->buf,ib->len);
}

/* Reset the buffer to be empty. */
void inputBufferClear(struct InputBuffer *ib) {
    ib->len = 0;
    inputBufferHide(ib);
}

/* =============================================================================
 * Main program logic, finally :)
 * ========================================================================== */

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        exit(1);
    }
 
    /* Create a TCP connection with the server. */
    int s = TCPConnect(argv[1],atoi(argv[2]),0);
    if (s == -1) {
        perror("Connecting to server");
        exit(1);
    }

    /* Put the terminal in raw mode: this way we will receive every
     * single key stroke as soon as the user types it. No buffering
     * nor translation of escape sequences of any kind. */
    setRawMode(fileno(stdin),1);

    /* Wait for the standard input or the server socket to
     * have some data. */
    fd_set readfds;
    int stdin_fd = fileno(stdin);
   
    struct InputBuffer ib;
    inputBufferClear(&ib);
 //我主要疑惑的点在于这个客户端是怎么一边能够接收服务端发送过来的消息，同时能够接收从终端输入的消息
    while(1) {
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);//s是客户端连接服务端对应的文件描述符
        FD_SET(stdin_fd, &readfds);
        int maxfd = s > stdin_fd ? s : stdin_fd;

        int num_events = select(maxfd+1, &readfds, NULL, NULL, NULL);
        //我主要疑惑的点在于这个客户端是怎么一边能够接收服务端发送过来的消息，同时能够接收从终端输入的消息
        //因为select是监听有读事件到来的文件描述符，如果标准输入事件没有到来，那么就会卡在原地，此时
        //如果服务端发来数据就会直接打印服务端数据，如果标准输入没有按下回车键就一直不会进入事件处理read中
        //所以何来的阻塞。因为read(stdin_fd,buf,sizeof(buf))不是要遍历到这个函数才能有光标，进行标准输入
        //而是只要有这个函数都能进行终端输入，终端输入并不取决于程序运行到哪里，只是运行到的时候才进行赋值操作
        if (num_events == -1) {
            perror("select() error");
            exit(1);
        } else if (num_events) {
            char buf[128]; /* Generic buffer for both code paths. */

            if (FD_ISSET(s, &readfds)) {
                /* Data from the server? */
                ssize_t count = read(s,buf,sizeof(buf));
                if (count <= 0) {
                    printf("Connection lost\n");
                    exit(1);
                }
                inputBufferHide(&ib);
                write(fileno(stdout),buf,count);//往标准输出写数据，把服务端文件描述符中数据读到终端
                inputBufferShow(&ib);//其作用是将存储在输入缓冲区结构体 InputBuffer 中的内容显示到标准输出（stdout）
            } else if (FD_ISSET(stdin_fd, &readfds)) {
                /* Data from the user typing on the terminal? */
                ssize_t count = read(stdin_fd,buf,sizeof(buf));//从标准输入
                //这里从标准输入输入数据，你是按下了回车键代码才来到了循环检验你的字符串中的字符
                for (int j = 0; j < count; j++) {
                    int res = inputBufferFeedChar(&ib,buf[j]);
                    switch(res) {
                    case IB_GOTLINE:
                        inputBufferAppend(&ib,'\n');
                        inputBufferHide(&ib);
                        //我懂了就是输入的时候实时显示要有一份，然后ib缓冲区的那一份是要发送给服务端的
                        write(fileno(stdout),"you> ", 5);
                        write(fileno(stdout),ib.buf,ib.len);
                        write(s,ib.buf,ib.len);//把他写到服务端文件描述符中ib.buf就是输出的内容
                        inputBufferClear(&ib);
                        break;
                    case IB_OK:
                        break;
                    }
                }
            }
        }
    }

    close(s);
    return 0;
}
