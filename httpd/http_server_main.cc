#include<iostream>
#include"http_server.h"

int main(int argc,char* argv[])
{
    if(argc != 3)
    {
        std::cout<<"Usage ./server [ip] [port]"<<std::endl;
    }
    HttpSever server;
    server.Start(argv[1],atoi(argv[2]));
    return 0;
}
