#include "cpuai/network_protocol.hpp"
#include "pairing.hpp"
#include <iostream>
#include <csignal>

using namespace cpuai;
namespace {
unsigned number(const char* arg,unsigned max){const std::string s=arg;net::check(!s.empty()&&s.find_first_not_of("0123456789")==std::string::npos,"expected unsigned integer");const auto n=std::stoull(s);net::check(n<=max,"argument outside limit");return static_cast<unsigned>(n);}
void noExtra(std::istringstream& in){std::string extra;net::check(!(in>>extra),"trailing protocol fields");}
std::string hello(net::Channel& channel,const std::filesystem::path& dir,const std::string& node){
    std::istringstream in(channel.receive(2048));std::string type,version,server,device,token;
    net::check(static_cast<bool>(in>>type>>version>>server>>device>>token)&&type=="HELLO"&&version=="CPUAI_NET1"&&server==node,"invalid protocol handshake");noExtra(in);validateDeviceId(device);
    {net::Registry registry(dir);net::check(registry.node()==node&&registry.authorized(device,token),"authentication failed");}
    channel.send("READY CPUAI_NET1 "+node);return device+" "+token;
}
void serve(const std::filesystem::path& store,const std::filesystem::path& dir,unsigned port,unsigned requests,const std::string& bind,unsigned timeout,bool drop){
    net::check(requests>0&&timeout>=100,"positive request count and timeout >=100ms required");DeviceJournal journal(store);
    {net::Registry registry(dir);net::check(registry.node()==journal.node(),"server identity/journal mismatch");}
    auto context=net::context(true);net::check(SSL_CTX_use_certificate_file(context.get(),(dir/"certificate.pem").c_str(),SSL_FILETYPE_PEM)==1&&SSL_CTX_use_PrivateKey_file(context.get(),(dir/"private.pem").c_str(),SSL_FILETYPE_PEM)==1&&SSL_CTX_check_private_key(context.get())==1,"cannot load server identity");
    net::Socket listener(::socket(AF_INET,SOCK_STREAM,0));int reuse=1;setsockopt(listener.fd(),SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));const auto addr=net::address(bind,port);
    net::check(::bind(listener.fd(),reinterpret_cast<const sockaddr*>(&addr),sizeof(addr))==0&&::listen(listener.fd(),8)==0,"listen failed");
    sockaddr_in actual{};socklen_t size=sizeof(actual);net::check(getsockname(listener.fd(),reinterpret_cast<sockaddr*>(&actual),&size)==0,"cannot get listening port");
    std::cout<<"ready port="<<ntohs(actual.sin_port)<<std::endl;
    for(unsigned request=0;request<requests;){
        pollfd item{listener.fd(),POLLIN,0};const int ready=poll(&item,1,1000);if(ready<0&&errno==EINTR)continue;net::check(ready>=0,"accept poll failed");if(!ready)continue;
        const int fd=::accept(listener.fd(),nullptr,nullptr);if(fd<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;net::Socket socket(fd);++request;
        try{net::Channel channel(context.get(),socket.fd(),true,timeout);std::istringstream identity(hello(channel,dir,journal.node()));std::string device,token;identity>>device>>token;
            std::size_t batches=0;for(;;){const auto payload=channel.receive();if(payload=="END"){channel.send("DONE");break;}
                net::check(++batches<=32,"session batch limit 32");const auto events=decodeBatch(payload,device);std::size_t inserted=0;
                {net::Registry registry(dir);net::check(registry.authorized(device,token),"device revoked or key rotated");inserted=journal.receive(events);}
                if(drop)break;
                channel.send("ACK "+std::to_string(events.size())+" "+std::to_string(inserted)+" "+net::digest(payload)+" "+journal.node());
            }
        }catch(const std::exception&){std::cerr<<"request rejected or disconnected\n";}
    }
}
void push(const std::filesystem::path& store,const std::filesystem::path& pairFile,const std::string& host,unsigned port,unsigned timeout){
    net::check(port>0&&timeout>=100,"port and timeout required");const auto pair=net::loadPair(pairFile);DeviceJournal source(store);net::check(source.node()==pair.device,"credential/device journal mismatch");
    std::vector<DeviceEvent> local;for(const auto& [id,e]:source.events())if(id.device==source.node())local.push_back(e); // Cannot forward another device's identity.
    auto context=net::context(false);SSL_CTX_set_verify(context.get(),SSL_VERIFY_NONE,nullptr);net::Socket socket(::socket(AF_INET,SOCK_STREAM,0));const auto addr=net::address(host,port);const auto deadline=net::Clock::now()+std::chrono::milliseconds(timeout);
    const int result=::connect(socket.fd(),reinterpret_cast<const sockaddr*>(&addr),sizeof(addr));
    if(result<0){net::check(errno==EINPROGRESS,"connect failed");net::wait(socket.fd(),POLLOUT,deadline);int error=0;socklen_t n=sizeof(error);net::check(getsockopt(socket.fd(),SOL_SOCKET,SO_ERROR,&error,&n)==0&&error==0,"connect failed");}
    net::Channel channel(context.get(),socket.fd(),false,timeout);channel.pin(pair.pin);channel.send("HELLO CPUAI_NET1 "+pair.server+" "+pair.device+" "+pair.token);
    net::check(channel.receive(2048)=="READY CPUAI_NET1 "+pair.server,"receiver identity/protocol mismatch");std::size_t delivered=0,duplicates=0;
    for(std::size_t offset=0;offset<local.size();offset+=128){std::vector<DeviceEvent> batch(local.begin()+static_cast<std::ptrdiff_t>(offset),local.begin()+static_cast<std::ptrdiff_t>(std::min(offset+128,local.size())));const auto payload=encodeBatch(batch);channel.send(payload);
        std::istringstream in(channel.receive(2048));std::string type,hash,node;std::size_t count,inserted;net::check(static_cast<bool>(in>>type>>count>>inserted>>hash>>node)&&type=="ACK"&&count==batch.size()&&inserted<=count&&hash==net::digest(payload)&&node==pair.server,"invalid commit acknowledgement");noExtra(in);
        source.acknowledge(pair.server,batch);delivered+=inserted;duplicates+=count-inserted;
    }
    channel.send("END");net::check(channel.receive(2048)=="DONE","incomplete sync session");std::cout<<"delivered="<<delivered<<" duplicates="<<duplicates<<" complete=1\n";
}
}
int main(int argc,char** argv)try{
    std::signal(SIGPIPE,SIG_IGN);
    if(argc<2||std::string(argv[1])=="--help"){
        std::cout<<"cpu-link init NEW_DIR NODE | pair DIR DEVICE NEW_BUNDLE | revoke DIR DEVICE | serve STORE DIR PORT CONNECTIONS [--bind IPv4] [--timeout-ms MS] [--drop-after-commit] | push STORE BUNDLE IPv4 PORT [TIMEOUT_MS]\n";return 0;}
    const std::string command=argv[1];
    if(command=="init"&&argc==4){net::initialize(argv[2],argv[3]);std::cout<<"server identity initialized\n";}
    else if(command=="pair"&&argc==5){net::pairDevice(argv[2],argv[3],argv[4]);std::cout<<"pairing bundle created; transfer privately\n";}
    else if(command=="revoke"&&argc==4){net::Registry registry(argv[2]);registry.revoke(argv[3]);std::cout<<"device revoked\n";}
    else if(command=="serve"&&argc>=6){std::string bind="127.0.0.1";unsigned timeout=5000;bool drop=false;for(int i=6;i<argc;++i){const std::string option=argv[i];if(option=="--bind"&&i+1<argc)bind=argv[++i];else if(option=="--timeout-ms"&&i+1<argc)timeout=number(argv[++i],60000);else if(option=="--drop-after-commit"&&!drop)drop=true;else throw std::runtime_error("invalid server option");}
        serve(argv[2],argv[3],number(argv[4],65535),number(argv[5],1000000),bind,timeout,drop);}
    else if(command=="push"&&(argc==6||argc==7))push(argv[2],argv[3],argv[4],number(argv[5],65535),argc==7?number(argv[6],60000):5000);
    else throw std::runtime_error("invalid command; see cpu-link --help");
    return 0;
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
