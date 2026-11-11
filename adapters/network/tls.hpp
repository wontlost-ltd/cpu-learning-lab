#pragma once
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <cerrno>

namespace cpuai::net {
template<class T,auto Free>using Owned=std::unique_ptr<T,decltype(Free)>;
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline std::string hex(std::span<const unsigned char> bytes){constexpr char digits[]="0123456789abcdef";std::string result;for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;}
inline bool isHex(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::string digest(const std::string& text){unsigned char out[32];unsigned n=0;check(EVP_Digest(text.data(),text.size(),out,&n,EVP_sha256(),nullptr)==1&&n==32,"SHA256 failed");return hex(out);}
inline std::string secret(){unsigned char bytes[32];check(RAND_priv_bytes(bytes,32)==1,"secure random failed");return hex(bytes);}
inline bool equal(const std::string& a,const std::string& b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
inline std::string fingerprint(X509* cert){unsigned char out[32];unsigned n=0;check(cert&&X509_digest(cert,EVP_sha256(),out,&n)==1&&n==32,"certificate digest failed");return hex(out);}
using Clock=std::chrono::steady_clock;
class Socket {
    int fd_=-1;
public:
    explicit Socket(int fd):fd_(fd){check(fd>=0,"socket creation failed");const int flags=fcntl(fd_,F_GETFL,0);if(flags<0||fcntl(fd_,F_SETFL,flags|O_NONBLOCK)<0){::close(fd_);fd_=-1;throw std::runtime_error("nonblocking socket failed");}}
    ~Socket(){if(fd_>=0)::close(fd_);}Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete;
    int fd()const{return fd_;}
};
inline void wait(int fd,short events,Clock::time_point deadline){
    for(;;){const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();check(left>0,"network deadline exceeded");
        pollfd item{fd,events,0};const int r=poll(&item,1,static_cast<int>(left));if(r<0&&errno==EINTR)continue;check(r>0,"network timeout/poll failed");return;}
}
inline sockaddr_in address(const std::string& host,unsigned port){sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(static_cast<std::uint16_t>(port));check(port<=65535&&inet_pton(AF_INET,host.c_str(),&addr.sin_addr)==1,"endpoint requires IPv4 literal and port <=65535");return addr;}
inline Owned<SSL_CTX,SSL_CTX_free> context(bool server){Owned<SSL_CTX,SSL_CTX_free> ctx(SSL_CTX_new(server?TLS_server_method():TLS_client_method()),SSL_CTX_free);
    check(static_cast<bool>(ctx)&&SSL_CTX_set_min_proto_version(ctx.get(),TLS1_3_VERSION)==1,"TLS 1.3 context failed");SSL_CTX_set_max_early_data(ctx.get(),0);SSL_CTX_set_max_cert_list(ctx.get(),16384);return ctx;}
class Channel {
    Owned<SSL,SSL_free> ssl_;
    int fd_;Clock::time_point deadline_;
    void retry(int result){const int error=SSL_get_error(ssl_.get(),result);
        if(error==SSL_ERROR_WANT_READ)wait(fd_,POLLIN,deadline_);else if(error==SSL_ERROR_WANT_WRITE)wait(fd_,POLLOUT,deadline_);else throw std::runtime_error("TLS connection failed or closed");}
public:
    Channel(SSL_CTX* ctx,int fd,bool server,unsigned timeoutMs):ssl_(SSL_new(ctx),SSL_free),fd_(fd),deadline_(Clock::now()+std::chrono::milliseconds(timeoutMs)){
        check(static_cast<bool>(ssl_)&&SSL_set_fd(ssl_.get(),fd)==1,"TLS setup failed");
        if(server)SSL_set_accept_state(ssl_.get());else SSL_set_connect_state(ssl_.get());
        for(;;){ERR_clear_error();const int r=SSL_do_handshake(ssl_.get());if(r==1)break;retry(r);}
    }
    void pin(const std::string& expected){Owned<X509,X509_free> cert(SSL_get1_peer_certificate(ssl_.get()),X509_free);
        check(cert&&isHex(expected)&&equal(fingerprint(cert.get()),expected),"server certificate pin mismatch");
        check(X509_cmp_current_time(X509_get0_notBefore(cert.get()))<0&&X509_cmp_current_time(X509_get0_notAfter(cert.get()))>0,"server certificate outside validity");
    }
    void read(std::span<unsigned char> bytes){std::size_t done=0;while(done<bytes.size()){
        check(Clock::now()<deadline_,"network deadline exceeded");std::size_t n=0;ERR_clear_error();const int r=SSL_read_ex(ssl_.get(),bytes.data()+done,bytes.size()-done,&n);if(r==1){check(n>0,"empty TLS read");done+=n;}else retry(r);}}
    void write(std::span<const unsigned char> bytes){std::size_t done=0;while(done<bytes.size()){
        check(Clock::now()<deadline_,"network deadline exceeded");std::size_t n=0;ERR_clear_error();const int r=SSL_write_ex(ssl_.get(),bytes.data()+done,bytes.size()-done,&n);if(r==1){check(n>0,"empty TLS write");done+=n;}else retry(r);}}
    std::string receive(std::size_t maximum=131072){unsigned char header[4];read(header);std::uint32_t size=0;for(auto b:header)size=(size<<8)|b;
        check(size>0&&size<=maximum,"network frame size outside limit");std::vector<unsigned char> bytes(size);read(bytes);return {bytes.begin(),bytes.end()};}
    void send(const std::string& payload){check(!payload.empty()&&payload.size()<=131072,"network frame size outside limit");const auto size=static_cast<std::uint32_t>(payload.size());unsigned char header[4];for(int i=0;i<4;++i)header[i]=static_cast<unsigned char>((size>>(24-8*i))&255);write(header);write(std::span(reinterpret_cast<const unsigned char*>(payload.data()),payload.size()));}
};
}
