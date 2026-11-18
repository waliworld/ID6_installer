#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "gauge_core.h"
#include "road_jet_frame.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
const unsigned kPoll = 0x524A4554u;
const char kWorker[] = "/var/opt/hmi/data/ID5/Plugins/RoadJet/road-jet-qnx";
const char kGateway[] = "/var/opt/hmi/data/ID5/Plugins/RoadJet/gateway-ip.txt";
const unsigned kObstacleBase = 57350;
const unsigned kObstacleFrames = 26;
const unsigned kObstacleSpacing = 600000; // thousandths of a pixel
const unsigned kObstacleStep = 16000;
const struct { unsigned lane; unsigned kind; } kObstacles[] = {
    {1, 0}, {3, 0}, {2, 1}, {0, 0}, {4, 1}, {3, 0}
};
const unsigned kObstacleCount = sizeof kObstacles / sizeof kObstacles[0];
}

class RoadJet : public bmw::PluginApp {
    int socket_;
    pid_t child_;
    bool visible_;
    unsigned generation_;
    uint64_t shown_at_, retry_at_, drawn_at_, steering_at_;
    road_jet_frame frame_;
    unsigned road_, sprite_;
    int mode_, lane_, sprite_lane_, altitude_, steering_;
    unsigned scroll_, distance_, obstacle_, obstacle_lane_;

    void reap() {
        if (child_ <= 0) return;
        pid_t ended = waitpid(child_, 0, WNOHANG);
        if (ended == child_ || (ended < 0 && errno == ECHILD)) child_ = -1;
    }

    void shutdown() {
        visible_ = false;
        ++generation_;
        reap();
        if (child_ > 0) kill(child_, SIGTERM);
        if (socket_ >= 0) close(socket_);
        socket_ = -1;
    }

    void postPoll(unsigned delay) {
        bmw::Parcel data;
        data.writeUInt32(kPoll);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u, data), delay);
    }

    void drawObstacle() {
        // Leave an empty stretch at the end of each section before the next obstacle.
        unsigned section = distance_ / kObstacleSpacing;
        unsigned step = distance_ % kObstacleSpacing / kObstacleStep;
        unsigned lane = kObstacles[section].lane;
        unsigned image = step >= kObstacleFrames ? static_cast<unsigned>(ROAD_JET_NO_SPRITE) :
            kObstacleBase + kObstacles[section].kind * kObstacleFrames + step;
        if (obstacle_lane_ != lane && obstacle_ != ROAD_JET_NO_SPRITE) {
            updateModel(70u + obstacle_lane_, bmw::ImageID(ROAD_JET_NO_SPRITE));
            obstacle_ = ROAD_JET_NO_SPRITE;
        }
        if (obstacle_ != image) {
            updateModel(70u + lane, bmw::ImageID(image));
            obstacle_ = image;
        }
        obstacle_lane_ = lane;
    }

    void draw(uint64_t now) {
        int mode = ROAD_JET_WAITING;
        unsigned stale_after = frame_.mode == ROAD_JET_MOST_PEDAL ?
            ROAD_JET_MOST_STALE_MS : ROAD_JET_STALE_MS;
        if (frame_.started_ms >= shown_at_ && frame_.started_ms <= now &&
            now - frame_.started_ms < stale_after) mode = frame_.mode;
        if (mode != ROAD_JET_WAITING && mode != ROAD_JET_MOST_PEDAL) {
            mode = steering_at_ && now - steering_at_ < ROAD_JET_STALE_MS ?
                ROAD_JET_ACTIVE : ROAD_JET_PEDAL_ONLY;
        }
        unsigned sprite = ROAD_JET_NO_SPRITE;
        if (mode != ROAD_JET_WAITING) {
            if (mode != ROAD_JET_ACTIVE) lane_ = 2;
            const int angle = mode == ROAD_JET_ACTIVE ? steering_ : 0;
            while (lane_ < ROAD_JET_LANES - 1 &&
                   angle >= (lane_ - 2) * 3000 + 1800) ++lane_;
            while (lane_ > 0 &&
                   angle <= (lane_ - 2) * 3000 - 1800) --lane_;
            altitude_ = (altitude_ * 2 + frame_.pedal + 1) / 3;
            if (!frame_.pedal && altitude_ <= 2) altitude_ = 0;
            unsigned step = (altitude_ * (ROAD_JET_HEIGHTS - 1) + 500) / 1000;
            sprite = ROAD_JET_SPRITE_BASE + step;
            unsigned ms = drawn_at_ && now - drawn_at_ < 150 ? now - drawn_at_ : 67;
            unsigned travel = ms * (72 + frame_.pedal * 220 / 1000);
            scroll_ = (scroll_ + travel) % 72000;
            distance_ = (distance_ + travel) % (kObstacleSpacing * kObstacleCount);
        } else {
            // Keep the last image in place until valid data returns.
            if (frame_.started_ms) sprite = sprite_;
        }
        drawn_at_ = now;
        unsigned road = ROAD_JET_ROAD_BASE + scroll_ * ROAD_JET_ROAD_FRAMES / 72000;
        if (road_ != road) {
            updateModel(50u, bmw::ImageID(road));
            road_ = road;
        }
        drawObstacle();
        if (sprite_lane_ != lane_ && sprite_ != ROAD_JET_NO_SPRITE) {
            updateModel(60u + sprite_lane_, bmw::ImageID(ROAD_JET_NO_SPRITE));
            sprite_ = ROAD_JET_NO_SPRITE;
        }
        if (sprite_ != sprite) {
            updateModel(60u + lane_, bmw::ImageID(sprite));
            sprite_ = sprite;
        }
        sprite_lane_ = lane_;
        if (mode_ != mode) {
            const char* label = "Waiting for vehicle data";
            if (mode == ROAD_JET_ACTIVE) label = "Live vehicle data";
            if (mode == ROAD_JET_PEDAL_ONLY) label = "Live vehicle data  |  Pedal only";
            if (mode == ROAD_JET_MOST_PEDAL) label = "MOST  |  Pedal only";
            if (mode == ROAD_JET_WAITING && frame_.started_ms) label = "Vehicle data paused";
            updateModel(52u, bmw::BasicString<char>(label));
            mode_ = mode;
        }
    }

    bool visibility(const bmw::Parcel& event) {
        bool show;
        if (!event.readBool(show) || show == visible_) return true;
        visible_ = show;
        ++generation_;
        if (!show) {
            reap();
            if (child_ > 0) kill(child_, SIGTERM);
            if (child_ > 0) postPoll(100);
            return true;
        }
        shown_at_ = bd_mono_ms();
        drawn_at_ = retry_at_ = steering_at_ = scroll_ = 0;
        distance_ = kObstacleFrames * kObstacleStep;
        lane_ = 2;
        altitude_ = steering_ = 0;
        memset(&frame_, 0, sizeof frame_);
        draw(shown_at_);
        if (socket_ >= 0) postPoll(1);
        return true;
    }

public:
    RoadJet(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned min,
            bmw::RHMIInterfaceVersion version, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : PluginApp(d, name, maj, min, version, cfg, res), socket_(-1), child_(-1),
          visible_(false), generation_(0), shown_at_(0), retry_at_(0), drawn_at_(0), steering_at_(0),
          road_(0), sprite_(ROAD_JET_NO_SPRITE), mode_(-1), lane_(2), sprite_lane_(2), altitude_(0), steering_(0),
          scroll_(0), distance_(kObstacleFrames * kObstacleStep), obstacle_(ROAD_JET_NO_SPRITE), obstacle_lane_(0) {
        memset(&frame_, 0, sizeof frame_);
    }

    virtual ~RoadJet() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }

    virtual bool onInit() {
        socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_ >= 0) {
            struct sockaddr_in address;
            memset(&address, 0, sizeof address);
            address.sin_family = AF_INET;
            address.sin_port = htons(ROAD_JET_PORT);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (fcntl(socket_, F_SETFL, O_NONBLOCK) < 0 ||
                fcntl(socket_, F_SETFD, FD_CLOEXEC) < 0 ||
                bind(socket_, reinterpret_cast<struct sockaddr*>(&address), sizeof address) < 0) {
                close(socket_);
                socket_ = -1;
            }
        }
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        action.m_exec = &Action::execImpl<RoadJet, &RoadJet::visibility>;
        addHandler(100u, 11u, action);
        return true;
    }

    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key != kPoll) return PluginApp::invokeDataUpdate(key, data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation != generation_ || socket_ < 0) return true;
        reap();
        if (!visible_) {
            if (child_ > 0) postPoll(100);
            return true;
        }
        uint64_t now = bd_mono_ms();
        if (child_ <= 0 && now >= retry_at_) {
            const DiagnosticGateway gateway(kGateway);
            const char* args[] = {kWorker, "--host", gateway.host(), 0};
            child_ = launch(args, -1);
            retry_at_ = now + 1500;
        }
        for (unsigned i = 0; i < 8; ++i) {
            road_jet_frame next;
            ssize_t length = recv(socket_, &next, sizeof next, 0);
            if (length < 0) break;
            if (length == sizeof next && next.started_ms >= shown_at_ && next.started_ms <= bd_mono_ms() &&
                next.started_ms >= frame_.started_ms && next.mode > ROAD_JET_WAITING &&
                next.mode <= ROAD_JET_MOST_PEDAL && next.pedal >= 0 && next.pedal <= 1000 &&
                next.steering >= -90000 && next.steering <= 90000) {
                frame_ = next;
                if (next.mode == ROAD_JET_ACTIVE) {
                    steering_at_ = next.started_ms;
                    steering_ = next.steering;
                }
                if (next.mode == ROAD_JET_MOST_PEDAL) steering_at_ = 0;
            }
        }
        draw(bd_mono_ms());
        postPoll(67);
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo = {1, "RoadJet", "owner-development", 1,
                               create_app<RoadJet>, destroy_app};
}
