#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"

#include <sys/wait.h>

namespace {
const unsigned kTick = 0x4C53484Fu;
const unsigned kImageBase = 57600;
const char kRoot[] = "/var/opt/hmi/data/ID5/Plugins/BEAMer/";
const char kWorker[] = "/var/opt/hmi/data/ID5/Plugins/BEAMer/beamer-qnx";
}

class LightShow : public bmw::PluginApp {
    unsigned generation_;
    bool visible_;
    pid_t child_;
    int channel_;

    void setLights(unsigned mask) {
        unsigned diagram = (mask & 3u ? 3u : 0u) | (mask & 12u ? 12u : 0u);
        updateModel(50u, bmw::ImageID(kImageBase + diagram));
    }
    void status(const char* text) {
        updateModel(66u, bmw::ImageID(57625));
        updateModel(51u, bmw::BasicString<char>(text));
    }

    void reap() {
        if (child_ <= 0) return;
        pid_t ended = waitpid(child_, 0, WNOHANG);
        if (ended == child_ || (ended < 0 && errno == ECHILD)) child_ = -1;
    }
    void closeChannel() {
        if (channel_ >= 0) close(channel_);
        channel_ = -1;
    }
    void stopWorker() {
        closeChannel();
        reap();
        if (child_ > 0) kill(child_, SIGTERM);
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(kTick);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u, data), 50u);
    }

    template <unsigned Mode>
    bool start(const bmw::Parcel&) {
        if (!visible_) return true;
        reap();
        if (child_ > 0 || channel_ >= 0) return true;
        int ends[2];
        if (pipe(ends) != 0) {
            status("Could not open lighting worker");
            return true;
        }
        char gatewayPath[128];
        snprintf(gatewayPath, sizeof gatewayPath, "%sgateway-ip.txt", kRoot);
        const DiagnosticGateway gateway(gatewayPath);
        const char pattern[] = {static_cast<char>('0' + Mode), 0};
        const char* args[] = {kWorker, "--host", gateway.host(), pattern, 0};
        if (fcntl(ends[0], F_SETFL, O_NONBLOCK) >= 0 && fcntl(ends[0], F_SETFD, FD_CLOEXEC) >= 0 &&
            fcntl(ends[1], F_SETFD, FD_CLOEXEC) >= 0)
            child_ = launch(args, ends[1]);
        close(ends[1]);
        if (child_ <= 0) {
            close(ends[0]);
            status("Could not start lighting worker");
            return true;
        }
        channel_ = ends[0];
        status("Checking lighting module and vehicle status");
        ++generation_;
        postPoll();
        return true;
    }

    bool stop(const bmw::Parcel&) {
        ++generation_;
        stopWorker();
        if (visible_) {
            setLights(0);
            status("Stopped");
        }
        if (child_ > 0) postPoll();
        return true;
    }

    bool visibility(const bmw::Parcel& event) {
        bool visible;
        if (event.readBool(visible) && visible != visible_) {
            visible_ = visible;
            if (!visible) return stop(event);
            setLights(0);
            updateModel(51u, bmw::BasicString<char>(""));
            updateModel(66u, bmw::ImageID(57626));
        }
        return true;
    }

    void process(unsigned char signal) {
        if (signal <= 15) {
            setLights(signal);
            return;
        }
        setLights(0);
        switch (signal) {
        case 'S': status("Playing exterior light pattern"); return;
        case 'D': status("Pattern finished"); break;
        case 'P': status("Select Park to start a light pattern"); break;
        case 'M': status("Stop the vehicle to start a light pattern"); break;
        case 'T': status("Could not confirm that the transmission is in Park"); break;
        case 'V': status("Could not confirm that vehicle speed is zero"); break;
        case 'K': status("Pattern finished  |  An unsupported lamp was skipped"); break;
        case 'L': status("None of the selected lamps are supported"); break;
        case 'E': status("Lighting diagnostic session unavailable"); break;
        case 'U': status("Lighting module profile unavailable"); break;
        case 'B': status("Diagnostics busy  |  Try again"); break;
        case 'G': status("Lighting gateway unavailable"); break;
        default: status("Lighting module did not confirm the pulse"); break;
        }
        closeChannel();
    }

public:
    LightShow(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned major, unsigned minor,
              bmw::RHMIInterfaceVersion version, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : PluginApp(d, name, major, minor, version, cfg, res),
          generation_(0), visible_(false), child_(-1), channel_(-1) {}

    virtual ~LightShow() { stopWorker(); }

    bool onInit() {
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        action.m_exec = &Action::execImpl<LightShow, &LightShow::visibility>;
        addHandler(100u, 11u, action);
        action.m_exec = &Action::execImpl<LightShow, &LightShow::start<0> >;
        addHandler(20u, action);
        action.m_exec = &Action::execImpl<LightShow, &LightShow::start<1> >;
        addHandler(21u, action);
        action.m_exec = &Action::execImpl<LightShow, &LightShow::start<2> >;
        addHandler(22u, action);
        action.m_exec = &Action::execImpl<LightShow, &LightShow::start<3> >;
        addHandler(23u, action);
        action.m_exec = &Action::execImpl<LightShow, &LightShow::stop>;
        addHandler(24u, action);
        return true;
    }

    bool onFinalize() {
        visible_ = false;
        ++generation_;
        stopWorker();
        return true;
    }

    bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key != kTick) return PluginApp::invokeDataUpdate(key, data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation != generation_) return true;
        reap();
        for (unsigned i = 0; channel_ >= 0 && i < 16; ++i) {
            unsigned char signal;
            ssize_t length = read(channel_, &signal, 1);
            if (length == 1) process(signal);
            else {
                if (!length || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    setLights(0);
                    status("Lighting connection ended");
                    closeChannel();
                }
                break;
            }
        }
        if (channel_ >= 0 || child_ > 0) postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo = {1, "BEAMer", "owner-development", 1,
                               create_app<LightShow>, destroy_app};
}
