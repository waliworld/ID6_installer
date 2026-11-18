#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "dme_descriptions.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kCodesPollKey = 0x4543504cu;
const unsigned kCodePageSize = 4;
const char kEngineWorker[] = "/var/opt/hmi/data/ID5/Plugins/EngineCodes/engine-codes-qnx";

void format_code_line(char* text, size_t size, const char* line, int length, char* detail, size_t detail_size) {
    detail[0]=0;
    unsigned code, status;
    int used=0;
    if (sscanf(line,"%6x  status %2x%n",&code,&status,&used)!=2 || used!=length) {
        snprintf(text,size,"%.*s",length,line);
        return;
    }
    const char* description="Description unavailable";
    for (unsigned i=0;i<sizeof kDmeDescriptions/sizeof kDmeDescriptions[0];++i) {
        if (kDmeDescriptions[i].code==code) {
            description=kDmeDescriptions[i].text;
            break;
        }
    }
    snprintf(text,size,"%06X   %s",code,(status & 1u) ? "Active" : (status & 4u) ? "Pending" : "Stored");
    snprintf(detail,detail_size,"%s",description);
}
}

#if defined(__arm__)
typedef char codes_message_size[(sizeof(bmw::Message)==80 && __alignof__(bmw::Message)==8) ? 1 : -1];
#endif

class EngineCodes : public bmw::PluginApp {
    const char* worker_;
    pid_t codes_pid_;
    int codes_fd_;
    char code_result_[4096];
    size_t code_size_;
    unsigned code_page_;
    unsigned poll_generation_;

    void startCodes(bool clear);
    void pollCodes();
    void renderCodes();
    void reapWorker() {
        if (codes_pid_<=0) return;
        pid_t ended=waitpid(codes_pid_,0,WNOHANG);
        if (ended==codes_pid_ || (ended<0 && errno==ECHILD)) codes_pid_=-1;
    }
    void stopWorker() {
        ++poll_generation_;
        if (codes_fd_>=0) { close(codes_fd_); codes_fd_=-1; }
        reapWorker();
        if (codes_pid_>0) kill(codes_pid_,SIGTERM);
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(kCodesPollKey);
        data.writeUInt32(poll_generation_);
        post(bmw::Message(0x40000003u,data),100u);
    }

public:
    EngineCodes(bmw::IDispatcher& dispatcher, bmw::BasicString<char> name,
                unsigned major, unsigned minor, bmw::RHMIInterfaceVersion version,
                const bmw::Bundle& config, const bmw::Bundle& resources,
                const char* worker=kEngineWorker)
        : PluginApp(dispatcher,name,major,minor,version,config,resources),
          worker_(worker), codes_pid_(-1), codes_fd_(-1), code_size_(0),
          code_page_(0), poll_generation_(0) {}

    virtual ~EngineCodes() { stopWorker(); }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        action.m_exec=&Action::execImpl<EngineCodes,&EngineCodes::readEngine>;
        addHandler(23u,action);
        action.m_exec=&Action::execImpl<EngineCodes,&EngineCodes::clearEngine>;
        addHandler(27u,action);
        action.m_exec=&Action::execImpl<EngineCodes,&EngineCodes::nextCodes>;
        addHandler(29u,action);
        code_size_=0;
        code_page_=0;
        clearRows();
        updateModel(80u,bmw::BasicString<char>("Select Read engine codes"));
        updateModel(89u,bmw::BasicString<char>("Engine fault memory"));
        updateModel(90u,bmw::BasicString<char>("Clears stored codes and freeze-frame data"));
        if (codes_pid_>0) postPoll();
        return true;
    }
    virtual bool onFinalize() { stopWorker(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key!=kCodesPollKey) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation!=poll_generation_) return true;
        pollCodes();
        if (codes_fd_>=0 || codes_pid_>0) postPoll();
        return true;
    }

    bool readEngine(const bmw::Parcel&) { startCodes(false); return true; }
    bool clearEngine(const bmw::Parcel&) { startCodes(true); return true; }
    bool nextCodes(const bmw::Parcel&) {
        if (codes_fd_<0 && code_size_) { ++code_page_; renderCodes(); }
        return true;
    }
    void clearRows() {
        for (unsigned i=0;i<kCodePageSize;++i) {
            updateModel(81u+i,bmw::BasicString<char>(""));
            updateModel(91u+i,bmw::BasicString<char>(""));
        }
    }
};

void EngineCodes::startCodes(bool clear) {
    if (codes_fd_>=0 || codes_pid_>0) return;
    int channel[2];
    if (pipe(channel)<0) {
        updateModel(80u,bmw::BasicString<char>("Could not start diagnostics"));
        return;
    }
    const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/" "EngineCodes" "/gateway-ip.txt");
    const char *args[]={worker_,"--host",gateway.host(),clear ? "--clear" : "--codes",0};
    if (fcntl(channel[0],F_SETFL,O_NONBLOCK)>=0 &&
        fcntl(channel[0],F_SETFD,FD_CLOEXEC)>=0 && fcntl(channel[1],F_SETFD,FD_CLOEXEC)>=0)
        codes_pid_=launch(args,channel[1]);
    close(channel[1]);
    if (codes_pid_<0) {
        close(channel[0]);
        updateModel(80u,bmw::BasicString<char>("Could not start diagnostics"));
        return;
    }
    codes_fd_=channel[0];
    code_size_=0;
    code_page_=0;
    code_result_[0]=0;
    clearRows();
    updateModel(80u,bmw::BasicString<char>(clear ? "Clearing engine codes..." : "Reading codes..."));
    postPoll();
}

void EngineCodes::pollCodes() {
    if (codes_fd_>=0) {
        ssize_t n=read(codes_fd_,code_result_+code_size_,sizeof code_result_-1-code_size_);
        if (n>0) {
            code_size_+=static_cast<size_t>(n);
            code_result_[code_size_]=0;
            return;
        }
        if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) return;
        close(codes_fd_);
        codes_fd_=-1;
        if (code_size_) renderCodes();
        else updateModel(80u,bmw::BasicString<char>("No diagnostic result returned"));
    }
    reapWorker();
}

void EngineCodes::renderCodes() {
    if (!code_size_) return;
    const char *first=strchr(code_result_,'\n');
    unsigned lines=0;
    for (const char *line=first;line && line[1];line=strchr(line+1,'\n')) ++lines;
    unsigned pages=lines ? (lines+kCodePageSize-1)/kCodePageSize : 1;
    code_page_%=pages;
    char text[160];
    int header=first ? static_cast<int>(first-code_result_) : static_cast<int>(code_size_);
    if (header>110) header=110;
    if (pages>1) snprintf(text,sizeof text,"%.*s  (%u/%u)",header,code_result_,code_page_+1,pages);
    else snprintf(text,sizeof text,"%.*s",header,code_result_);
    updateModel(80u,bmw::BasicString<char>(text));
    const char *line=first ? first+1 : "";
    for (unsigned i=0;i<code_page_*kCodePageSize && *line;++i) {
        const char *end=strchr(line,'\n');
        line=end ? end+1 : "";
    }
    for (unsigned i=0;i<kCodePageSize;++i) {
        const char *end=strchr(line,'\n');
        int length=end ? static_cast<int>(end-line) : static_cast<int>(strlen(line));
        char detail[160];
        format_code_line(text,sizeof text,line,length,detail,sizeof detail);
        updateModel(81u+i,bmw::BasicString<char>(text));
        updateModel(91u+i,bmw::BasicString<char>(detail));
        line=end ? end+1 : "";
    }
}

extern "C" {
PluginAppInfo pluginAppInfo = {1, "EngineCodes", "owner-development", 1,
                              create_app<EngineCodes>, destroy_app};
}
