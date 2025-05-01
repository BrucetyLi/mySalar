#include <iostream>
#include <sstream>
#include <stdarg.h>
#include <string>
#include <unistd.h>
#include <sys/syscall.h>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <map>

#include "log.h"
#include "config.h"
#include "scheduler.h"
#include "http_server.h"
#include "address.h"

std::string format(const char* fmt,...){
	std::stringstream ss;
	va_list args;
	va_start(args,fmt);   // va_start 作用：初始化va_list对象，使其指向可变参数列表的起始位置。
	char* buf = nullptr;
	int len = vasprintf(&buf,fmt,args);
	if(len != -1){
		ss<<std::string(buf,len);
		free(buf);
	}
	va_end(args);
	return ss.str();
}



//测试用的全局累加计数字段
int num = 0;
//循环累加的次数
uint64_t loop_times = 100000000;

void sing(){
    for(size_t i = 0; i < loop_times; ++i){
        ++num;
    }
    std::cout << sylar::Thread::GetName() << " &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&  sing~ " << num << std::endl;
}

void dance(){
    for(size_t i = 0; i < loop_times; ++i){
        ++num;
    }
    std::cout <<sylar::Thread::GetName() << " &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&   dance~ " << num << std::endl;   
}
void rap(){
    for(size_t i = 0; i < loop_times; ++i){
        ++num;
    }
    std::cout <<sylar::Thread::GetName() << " &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& rap~ " << num << std::endl;   
}


int main(int argc, char** argv)
{
    /*
    Logger::ptr lg(new Logger("XYZ"));
    // LogEvent::ptr event(new LogEvent(
    //     lg,
    //     LogLevel::INFO,     //日志级别
    //     __FILE__,           //文件名称
    //     __LINE__,           //行号
    //     1234567,            //运行时间
    //     syscall(SYS_gettid),//线程ID
    //     0,                  //协程ID
    //     time(0),             //当前时间
    //     "jiejie"
    // ));
   
   LogFormatter::ptr formatter(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
   StdoutLogAppender::ptr stdApd(new StdoutLogAppender());
 
   stdApd->setFormatter(formatter);
   lg->addAppender(stdApd);

//    event->getSS()<<"HELLO MY SON";
//    LogEventWrap(event).getSS() <<"  追加";

    LOG_LEVEL(lg, LogLevel::INFO)<<"你干嘛？杰哥"; 
    */

    SYLAR_LOG_INFO(SYLAR_LOG_NAME("root")) << "你干嘛？杰哥" << " ok";

    std::string p= "32768";
    int i=0;
 	// 将字符串转化为整数
    i=boost::lexical_cast<int>(p.c_str()); 
    std::cout << i << std::endl;

    std::cout << "************************" << std::endl;
    YAML::Node node = YAML::LoadFile("./bin/test.yaml");
    std::cout << node["name"].as<std::string>() << std::endl;
    std::cout << node["sex"].as<std::string>() << std::endl;
    std::cout << node["age"].as<int>() << std::endl;//18
    std::cout << node["system"]["port"].as<std::string>() << std::endl;
    std::cout << node["system"]["value"].as<std::string>() << std::endl;
    for(auto it = node["system"]["int_vec"].begin(); it != node["system"]["int_vec"].end(); ++it){
		std::cout << *it <<" ";
	}
    std::cout << "************************" << std::endl;

    sylar::ConfigVar<int>::ptr tmp = sylar::Config::Lookup("person.index",10);
    std::cout << tmp->getValue() << std::endl;
    YAML::Node node1 = YAML::LoadFile("./bin/test1.yaml");
    sylar::Config::LoadFromYaml(node1);
    std::cout << tmp->getValue() << std::endl;

    std::cout << "************************" << std::endl;
    /*
    sylar::Scheduler sc(2);
    sc.schedule(std::bind(&sing));
    sc.schedule(std::bind(&dance));
    sc.schedule(std::bind(&rap));

    sc.start();
    sc.stop();
    */
    std::cout << "************************" << std::endl;
    std::stringstream ss; ss<< node1;
    std::cout << ss.str() << std::endl;
    YAML::Node node2 = YAML::Load("[1,2,3]");
    ss<<node2;
    std::cout << ss.str() << std::endl;
    std::vector<int> tmp1 = sylar::LexicalCast<std::string, std::vector<int> >()("[1,2,3]");
    std::cout << tmp1.size()<< std::endl;
    YAML::Node node3; node3.push_back(3); node3.push_back(2);
    ss<<node3;
    std::cout << ss.str() << std::endl;
    



    sylar::IOManager iom(2);
    sylar::TcpServer::ptr tcp_server(new sylar::TcpServer);
    auto addr = sylar::Address::LookupAny("0.0.0.0:8020");
    while(!tcp_server->bind(addr)) {
        sleep(2);
    }
    tcp_server->start();
    
    return 0;
}