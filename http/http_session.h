/**
 * @file http_session.h
 * @brief HTTPSession封装
 * 继承自SocketStream，实现了在套接字流上读取HTTP请求与发送HTTP响应的功能，在读取HTTP请求时需要借助HTTP解析器，
 * 以便于将套接字流上的内容解析成HTTP请求。 从每个请求的通信套接字读到请求数据，利用parser将流数据转换成自己定义的结构体由该模块完成。
 * 解析完后，会由servlet执行相关操作，把信息封装在HttpResponse，由该模块通过通信套接字传给客户端。
 */

#ifndef __SYLAR_HTTP_SESSION_H__
#define __SYLAR_HTTP_SESSION_H__

#include "socket_stream.h"
#include "http.h"

namespace sylar {
namespace http {

/**
 * @brief HTTPSession封装
 */
class HttpSession : public SocketStream {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<HttpSession> ptr;

    /**
     * @brief 构造函数
     * @param[in] sock Socket类型
     * @param[in] owner 是否托管
     */
    HttpSession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 接收HTTP请求
     */
    HttpRequest::ptr recvRequest();

    /**
     * @brief 发送HTTP响应
     * @param[in] rsp HTTP响应
     * @return >0 发送成功
     *         =0 对方关闭
     *         <0 Socket异常
     */
    int sendResponse(HttpResponse::ptr rsp);
};

}
}

#endif
