#include<httplib.h>
using namespace httplib;
void getHandler(const Request & req , Response &res){
    std::cout<<"Received Client Req\n";
    res.set_content("Hello there" , "text/plain");
}
int main(){

    Server server;

    server.Get("/" , getHandler);

    server.listen("127.0.0.1" , 8080);
    return 0;
}