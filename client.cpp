#include<httplib.h>
using namespace httplib;
int main(){
    Client client(
        "127.0.0.1:8080"
    );

    auto result = client.Get("/");
    std::cout<<"Result received "<<result<<"\n";
    std::cout<<"Result Status "<<result->status<<"\n";
    std::cout<<"Result Body "<<result->body<<"\n";

}