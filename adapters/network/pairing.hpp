#pragma once
#include "cpuai/device_sync.hpp"
#include "tls.hpp"
#include <fstream>
#include <sys/stat.h>

namespace cpuai::net {
inline std::string smallFile(const std::filesystem::path& path,std::size_t max=65536){std::ifstream in(path,std::ios::binary|std::ios::ate);check(static_cast<bool>(in)&&in.tellg()>=0&&static_cast<std::uint64_t>(in.tellg())<=max,"cannot read credential/config file");const auto size=static_cast<std::size_t>(in.tellg());std::string data(size,'\0');in.seekg(0);check(static_cast<bool>(in.read(data.data(),static_cast<std::streamsize>(size))),"credential read failed");return data;}
inline void privateFile(const std::filesystem::path& path,const std::string& data){check(!std::filesystem::exists(path),"refusing credential overwrite");DurableFile file(path,true,true);file.append(std::span(reinterpret_cast<const unsigned char*>(data.data()),data.size()));file.sync();DurableFile::syncParent(path);}
inline std::string publicPin(const std::filesystem::path& dir){Owned<BIO,BIO_free> file(BIO_new_file((dir/"certificate.pem").c_str(),"r"),BIO_free);check(static_cast<bool>(file),"cannot read certificate");Owned<X509,X509_free> cert(PEM_read_bio_X509(file.get(),nullptr,nullptr,nullptr),X509_free);return fingerprint(cert.get());}
struct Pair {std::string server,device,pin,token;};
inline Pair loadPair(const std::filesystem::path& path){struct stat st{};check(stat(path.c_str(),&st)==0&&(st.st_mode&077)==0,"pair credentials require owner-only permissions");
    std::istringstream in(smallFile(path,1024));std::string magic,extra;Pair p;check(static_cast<bool>(in>>magic>>p.server>>p.device>>p.pin>>p.token)&&magic=="CPUAI_PAIR1"&&!(in>>extra),"invalid pairing bundle");validateDeviceId(p.server);validateDeviceId(p.device);check(isHex(p.pin)&&isHex(p.token),"invalid pin/key");return p;}
class Registry {
    std::filesystem::path dir_;DurableFile lock_;
    std::string node_;std::map<std::string,std::pair<std::string,bool>> grants_;
public:
    explicit Registry(const std::filesystem::path& dir):dir_(dir),lock_(dir/"registry.lock",true){
        std::istringstream in(smallFile(dir/"registry.cfg"));std::string magic;check(static_cast<bool>(in>>magic>>node_)&&magic=="CPUAI_AUTH1","invalid registry");validateDeviceId(node_);
        std::string device,hash;int active;
        while(in>>device){check(static_cast<bool>(in>>hash>>active)&&isHex(hash)&&(active==0||active==1),"invalid device grant");validateDeviceId(device);check(grants_.emplace(device,std::pair{hash,active==1}).second&&grants_.size()<=32,"duplicate/excess grants");}
        check(in.eof(),"invalid registry tail");
    }
    const auto& node()const{return node_;}
    bool authorized(const std::string& device,const std::string& token)const{const auto it=grants_.find(device);return isHex(token)&&it!=grants_.end()&&it->second.second&&equal(it->second.first,digest(token));}
    void save(){std::ostringstream out;out<<"CPUAI_AUTH1 "<<node_<<'\n';for(const auto& [device,g]:grants_)out<<device<<' '<<g.first<<' '<<g.second<<'\n';
        const auto temp=dir_/("registry-"+newSessionId()+".tmp");privateFile(temp,out.str());std::filesystem::rename(temp,dir_/"registry.cfg");DurableFile::syncParent(dir_/"registry.cfg");}
    void grant(const std::string& device,const std::string& token){validateDeviceId(device);check(device!=node_&&isHex(token),"invalid grant");check(grants_.contains(device)||grants_.size()<32,"grant limit 32 (revocations retain identity slots)");grants_[device]={digest(token),true};save();}
    void revoke(const std::string& device){const auto it=grants_.find(device);check(it!=grants_.end(),"device not paired");it->second.second=false;save();}
};
inline void initialize(const std::filesystem::path& dir,const std::string& node){validateDeviceId(node);check(std::filesystem::create_directory(dir),"identity directory must be new");check(chmod(dir.c_str(),0700)==0,"cannot restrict identity directory");
    Owned<EVP_PKEY,EVP_PKEY_free> key(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime256v1"),EVP_PKEY_free);Owned<X509,X509_free> cert(X509_new(),X509_free);
    check(key&&cert,"identity generation failed");check(X509_set_version(cert.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1)==1,"certificate setup failed");
    check(X509_gmtime_adj(X509_getm_notBefore(cert.get()),-60)&&X509_gmtime_adj(X509_getm_notAfter(cert.get()),31536000),"certificate dates failed");
    check(X509_set_pubkey(cert.get(),key.get())==1,"certificate key failed");auto name=X509_get_subject_name(cert.get());check(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>(node.c_str()),-1,-1,0)==1&&X509_set_issuer_name(cert.get(),name)==1&&X509_sign(cert.get(),key.get(),EVP_sha256())>0,"certificate sign failed");
    Owned<BIO,BIO_free> keyPem(BIO_new(BIO_s_mem()),BIO_free),certPem(BIO_new(BIO_s_mem()),BIO_free);
    check(keyPem&&certPem&&PEM_write_bio_PrivateKey(keyPem.get(),key.get(),nullptr,nullptr,0,nullptr,nullptr)==1&&PEM_write_bio_X509(certPem.get(),cert.get())==1,"identity encoding failed");
    char* data=nullptr;auto length=BIO_get_mem_data(keyPem.get(),&data);privateFile(dir/"private.pem",std::string(data,static_cast<std::size_t>(length)));
    length=BIO_get_mem_data(certPem.get(),&data);privateFile(dir/"certificate.pem",std::string(data,static_cast<std::size_t>(length)));privateFile(dir/"registry.cfg","CPUAI_AUTH1 "+node+"\n");
}
inline void pairDevice(const std::filesystem::path& dir,const std::string& device,const std::filesystem::path& output){Registry registry(dir);validateDeviceId(device);check(device!=registry.node(),"device must differ from server");const auto token=secret();
    privateFile(output,"CPUAI_PAIR1 "+registry.node()+" "+device+" "+publicPin(dir)+" "+token+"\n");registry.grant(device,token);
}
}
