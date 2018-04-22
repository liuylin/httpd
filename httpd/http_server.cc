#include"http_server.h"
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<pthread.h>
#include<sstream>
#include<stdlib.h>
#include<arpa/inet.h>
#include<sys/wait.h>

#include"util.hpp"
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

//浏览器显示静态文件：图片是二次请求的方式获得
//首次请求是一个/根目录
//第二次请求是/image/1.jpg
namespace http_server.h
{
    //服务器启动
    //创建一个TCP版本的服务器
    //每来一个请求就开辟一个线程为它服务
    int HttpServer::Start(const std::string& ip,short port)
    {
        int listen_sock = socket(AF_INET,SOCK__STREAM,,0);
        if (listen_sock < 0)
        {
            perror("socket");
            return -1;
        }
        //要给socket加上一个选项，能够重用我们的连接
        int opt = 1
        setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
        sockaddr_in addr;
        addr.sin_family =AF_INET;
        addr.sin_addr.s_addr =  inet_addr(ip.c_str());
        addr.sin_port =  htons(port);
        int ret = bind(listen_sock,(sockaddr*)&addr,sizeof(addr));
        if(ret < 0)
        {
            perror("bind");
            return -1;
        }
        ret = listen(listen_sock,5);
        if (ret < 0)
        {
            perror("listen");
            return -1;
        }
        //打印日志
        LOG(INFO) << "ServerStart OK\n";
         while(1)
        {
        //基于多线程来实现一个TCP服务器
        sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int new_sock = accept(listen_sock,(sockaddr*)&peer,&len);
        if(new_sock)
        {
            perror("accept");
            continue;//不能因为一次失败服务器就异常终止
        }
        //创建新线程，使用新线程完成这次请求的计算
        Context* context = new Context();
        context->new_sock = new_sock;
        context->server = this;
        pthread_t tid;
        pthread_create(&tid,NULL,ThreadEntry,reinterpret_cast<Context*>(context));
        pthread_detach(tid);
        }
        close(listen_sock);
        return 0;
    } 

    //线程入口函数
    void* HttpServer::ThreadEntry(void* arg)
    {
        //准备工作
        Context* context = reinterpret_cast<Context*>(arg);
        HttpServer* server = context->server;
        //1,从文件描述符中读取数据，转换成Request对象
        int ret = 0;
        ret = server->ReadOneRequest(context);
        if(ret < 0)
        {
            LOG(ERROR)<<"ReadOneRequest error!"<<"\n";
            //用这个函数构造一个404的http Response对象
            server->Process404(context);
            goto END;
        }
        //Test 通过以下函数将一个解析出来的请求打印出来
        server->PrintRequest(context->req);
        //2，把Request对象计算成Response对象
        ret = server->HandlerRequest(context);
        if(ret < 0)
        {
        {
            LOG(ERROR)<<"HandlerRequest error!"<<"\n";
            //用这个函数构造一个404的http Response对象
            server->Process404(context);
        }
        //3，把Request对象进行序列化，写回到客户端
    END:
        //处理失败的情况
        server->WriteOneResponse(context);
        //收尾工作
        close(context->new_sock);
        delete context;
        return NULL;
    }

    //构造一个状态码为404的Response对象
    int HttpServer::Process404(Context* context)
    {
        Response* resp = &context->resp;
        resp->code = 404;
        resp->desc = "Not Found";
        resp->body = "<head><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\"></head><h1>404! 您的页面被喵星人吃掉了^-^</h1>";
        std::stringstream ss;
        ss<<resp->body.size();
        std::string size;
        ss >> size;
        resp->header["Content-Length"] = size;
    }

    //从socket中读取字符串，构造生成Request对象
    //一下内容为编程规范，不同的公司可能要求有所差异，不要盲目模仿
    //1，如果参数用于输入，const T&
    //2,如果参数用于输出，T*
    int HttpServer::ReadOneRequest(Context* context)
    {
        Request* req = &context->req;
        //1，从socket中读取一行数据作为Request的首行
        //按行读取的分隔符应该就是\n \r \r\n
        std::string first_line;
        FileUtil::ReadLine(context->new_sock,&first_line);
        //2，解析首行，获取到请求的方法，method 和url
        int ret = ParseFirstLine(first_line,&req->method,&reeq->url);
        if(ret < 0)
        {
            LOG(ERROR)<<"ParseFirstLine error! first_line="<<first_line<<"\n";
            return -1;
        }
        //3,解析url_path,获取到path和query_string
        ret = ParseUrl(req->url,&req->url->path,&req->query_string);
        if(ret < 0)
        {
            LOG(ERROR)<<"ParseUrl error! url="<<req->url<<"\n";
            return -1;
        }
        //4,循环的按行读取数据，每次读取到一行数据，就进行一次header的解析
        //读到空行说明header解析完毕
        std::string header_line;
        while(1)
        {
            FileUtil::ReadLine(context->new_sock,&header_line);
            //如果 header_line是空行，就退出循环
            //由于Readline返回的head_line不包含\n等分隔符
            //因此读到空行的时候，header_line就是空字符串
            if(header_line == "")
            {
                break;
            }
            /*
            if()
            {
                break;
            }*/
            ret = ParseHeader(header_line,&req->header);
            if(ret < 0)
            {
                LOG(ERROR) << "ParseHeader error! header_line="<<header_line<<"\n";
                return -1;
            }
        }
        //5，如果是POST请求，但是没有Content-Length字段，任务这次请求失败
        Header::iterator it = req->header.find("Content-Length");
        if(req->method == "POST" && it ==  req->header.end())
        {
            LOG(ERROR) << "POST Request has no Content-Length!\n";
            return -1;
        }
        //如果是GET请求，就不用读body了
        if(req->method == "GET")
        {
            return 0;
        }
        //如果是POST请求，并且header中包含了Content-Length字段
        //继续读取socket,获取到body的内容
        int content_length = atoi(it->second.c_str());
        ret =FileUtil:: ReadN(Context->new_sock,content_length,&req->body);
        if(ret < 0)
        {
            LOG(REEOR) << "ReadN error! content_length="<<content_length<<"\n";
            return -1;
        }
        return 0;
    }


    //解析首行，就是按照空格进行分割，分割成三个部分
    //这三个部分分别就是 方法，url,版本号
    int HttpServer::ParseFirstLine(const std::string& first_line,std::string* method,std::string* url)
    {
        std::vector<std::string> tokens;
        StringUtil::Split(first_line," ",&tokens); 
        if(tokens.size() != 3)
        {
            //首行的格式不对
            LOG(ERROR)<<"ParseFirstLine error! split error! first_line="<<first_line<<"\n";
            return -1;
        }
        //如果版本号中不包含HTTP关键字，也认为出错
        if(tokens[2].find("HTTP") == std::string::npos)
        {
            //首行的格式不对，版本信息中不包含HTTP关键字
            LOG(ERROR) << "ParseFirstLine error! version error! first_line="<<first_line<<'\n';
            return -1;
        }
        *method = tokens[0];
        *url = tokens[1];
        return 0;
    }


    //解析一个标准的url,其实比较复杂，核心思路是以?作为分割，从？左边来查找url_path
    //从？右边来查找query_string
    //我们此处只实现一个简化版本，只考虑不包含域名和协议，以及#的情况
    int HttpServer:: ParseUrl(const std::string& url,std::string* url_path,std::string* query_string)
    {
        size_t pos = url.find("?");
        if(pos == std::string::npos)
        {
            //没找到
            *url_path = url;
            return 0;
        }
        //找到了
        *url_path = url.substr(0,pos);
        *query_string = url.substr(pos+1);
        return 0;
    }


    //此函数用于解析一行header
    //此处的实现使用string::find来进行实现
    //如果使用split的话，可能存在如下问题：
    //HOST：http://www.baidu.com
    int HttpServer:: ParseHeader(const std::string& header_line,Header* header)
    {
        size_t pos = header_line.find(":");
        if(pos == std::string::npos)
        {
            //找不到：说明header的格式有问题
            LOG(ERROR)<<"ParseHeader error!has no : header_line="<<header_line<<"\n";
            return -1;
        }
        //这个header的格式还是有问题，没有value
        if(pos + 2 >= header_line.size())
        {
            LOG(ERROR)<<"ParseHeader error!has no : header_line="<<header_line<<"\n";
            return -1;
        }
        (*header)[header_line.substr(0,pos)] = header_line.substr(pos+2);
        return 0;
    }

   //wwwwroot相当于http服务器的根目录
   //在URL中带有的path,本质上就是在请求以wwwroot目录为基准的一个相对路径上的文件、
   //1，通过Request中的url_path字段，计算出文件在磁盘上的路径是什么
   //例如url_path/index.html，想要得到的磁盘上的文件就是 ./wwwroot/index.html
   //例如 url_path/image/1.jpg,想得到的磁盘的文件就是 ./wwwroot/image/1.jpg
   //注意，./wwwroot是我们次数约定的跟目录，可以根据自己的喜好约定根目录是什么
   //2,打开文件，将文件中的所有内容读取出来放到body中
    int HttpServer::ProcessStaticFile(Context* context)
    {
        const Request& req = context->req;
        Response* resp = &context->resp;
        //1,获取到静态文件的完整路径
        std::string file_path;
        GetFilePath(req.url_path,&file_path);
        //2,打开并读取完整的文件
        int ret =FileUtil:: ReadAll(file_path,&resp->body);
        if(ret < 0)
        {
            LOG(ERROR) << "ReadAll error!filepath = "<<file_path<<"\n";
            return -1;
        }
        return 0;
    }
   
   //通过url_path找到对应的文件路径
   //例如  请求url可能是http://192.168.80.132:9090/
   //这种情况下url_path是 /
   //此时等价于请求 /index.html   作为我们的默认请求文件
   //例如  请求url可能是http://192.168.80.132:9090/image
   //这种情况下url_path 是 /
   //如果url_path指向的是一个目录，就尝试在这个目录下访问一个叫做index.html的文件（这个策略也是我们的一种简单约定）
    void HttpServer::GetFilePath(const std::string& url_path,std::string* file_path)
    {
        *file_path = "./wwwroot" + url_path;
        //判断一个路径是普通文件还是目录文件
        //1，linux的stat函数
        //2,通过boost的filesystem模块来进行判定
        //如果当前文件是一个目录，就可以进行一个文件名拼接，拼接上一个index.html后缀
        if(FileUtil::IsDir(*file_path))
        {
            // 1,/image/
            // 2,/image
            if(file_path->back()!= '/')
            {
                file_path->push_back('/');
            }
            (*file_path) +=  "index.html";
        }
        return;
    }


    ////////////////////////////////////////////////////////
    //分割线  以下为测试函数
    ////////////////////////////////////////////////////////
    
    void PrintRequest(const Request& req)
    {
        LOG(DEBUG)<<"Request:"<<"\n"<<req.method<<" "<<req.url<<"\n"<<req.url_path<<" "<<req.query_string<<"\n";
        for(Header::const_iterator it = req.header.begin();it != req.header.end;++it)
        {
            LOG(DEBUG)<<it->first<<":"<<it->second<<"\n";
        }
        LOG(DEBUG)<<"\n";
        LOG(EBUGD)<<req.body<<"\n";
    }


    //该函数实现序列化，把response对象转换成一个string
    //写回到socket中
    //此函数完全按照Http协议的要求来构造响应数据
    //我们实现这个函数的细节可能有很大差异，但是只要遵守http协议，那么就是ok的·
    int HttpServer::WriteOneResponse(Context* context)
    {
        //iostream和stringstream之间的关系类似于
        //printf和sprintf之间的关系
        //1，进行序列化
        const  Response& resp = context->resp;
        std::stringstream ss;
        ss << "HTTP/1.1 "<<resp.code<<" "<<resp.desc<<"\n";//状态码和描述
        if(resp.cgi_resp == "")
        {
            //当前认为是在处理静态页面
            for(auto item:resp.header)
            {
                ss << item.first <<": "<<item.second << "\n";
            }
        //空行
        ss << "\n";
        //body
        ss << resp.body;
        }
        else
        {
            //当前是在处理CGI生成的页面
            //cgi_resp同时包含了响应数据的header空行和body
            ss << resp.cgi_resp;
        }
        //将序列化的结果写入到socket中
        const std::string& str = ss.str();
        write(context->new_sock,str.c_str(),str.size());
        return 0;
    }

    //通过输入的Request对象计算生成Response对象
    //1，静态文件
    //a)GET 并且没有query_string作为参数
    //2，动态生成页面(使用CGI的方式生成)
    //a)GET并且存在query_string作为参数
    //b)POST请求 
    int HttpServer::HandlerRequest(Context* context)
    {
        const Request& req = context->req;
        Response* resp = &context->resp;
        resp->code = 200;
        resp->desc = "OK";
        //判定当前的处理方式是按照静态处理还是动态生成
        if(req.method == "GET" &&  req.query_string == "")
        {
            return context->server->ProcessStaticFile(context);
        }
        else if((req.method == "GET" && req.query_string != "") || (req.method == "POST"))
        {
            return context->server->ProcessCGI(context);
        }
        else
        {
            LOG(ERROR) <<"UnSupport Method! method="<<req.method<<"\n";
            return -1;
        }
        return -1;
    }



    int HttpServer::ProcessCGI(Context* context)
    {
        const Request& req = context->req;
        Response* resp = &context->resp;
        //1,创建一对匿名管道（父子进程要双向通信）
        int fd1[2],fd2[2];
        pipe(fd1);
        pipe(fd2);
        int father_write = fd1[1];
        int child_read = fd1[0];
        int father_read = fd2[0];
        int child_write = fd2[1];
        //2，设置环境变量
        //a)METHOD请求方法
        std::string env = "REQUEST_METHOD="+req.method;
        putenv(const_cast<char*>(env.c_str()));
        if(req.method == "GET")
        {
        //b)QUERY_STRING请求的参数
        env = "QUERY_STRING="+req.query_string;
        putenv(const_cast<char*>(env.c_str()));
        }
        else if(req.method == "POST")
        {
        //c)POST方法，就设置CONTENT_LENGTH
        Header::const_iterator pos = req.header.find("Content-Length");
        env = "CONTENT_LENGTH="pos->second;
        putenv(const_cast<char*>(env.c_str()));
        }
        pid_t ret = fork();
        if(ret < 0)
        {
            perror("fork");
            goto END;
        }
        //3，fork ，父进程流程
        if(ret > 0)
        {
        close(child_read);
        close(child_write);
        //a)如果是POST请求，父进程就要把body写入到管道中
        if(req.method == "POST")
        {
        write(father_write,req.body.c_str(),req.body.size());
        }
        //b)阻塞式的读取管道，尝试把子进程的结果读取出来，并且放到Response对象中
        //ReadAll从文件描述符中读取数据
        FileUtil::ReadAll(father_read,&resp->cgi_resp);
        //C)对子进程进行进程等待（为了避免僵尸进程）
        wait(NULL);
        }
        else
        {
        close(father_read);
        close(father_write);
        //4，fork,子进程流程
        //a)把标准输入和标准输出进行重定向
        //如果不使用环境变量。只使用管道可以吗？
        //如果仅仅使用管道完成进程间通信的话，会需要额外的协议
        //如果要想仅仅使用管道完成进程间通信的话，需要额外约定协议
        //此处的协议指的是，父进程按照啥样的格式组织数据并写入管道，以及子进程按照啥样的格式来解析
        dup2(child_read,0);
        dup2(child_write,1);
        //b)先获取到要替换的可执行文件是哪个（通过url_path来获取)
        std::string file_path;
        GetFilePath(req.url_path,&file_path);
        //c)进行进程的程序替换
        execl(file_path.c_str(),file_path.c_str(),NULL);
        //d)由我们的CGI可执行程序完成动态页面的计算，并且写回数据到管道中
        //这部分逻辑我们需要放到另外的单独的文件中实现，并且根据该文件编译生成我们的CGI可执行程序
        }
        END:
        //同一处理收尾工作
        close(father_read);
        close(father_write);
        close(child_write);
        close(child_read);
        return 0;
    }

}//end http_server
