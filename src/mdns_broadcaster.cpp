#include "mdns_broadcaster.hpp"
#include <iostream>
#include <thread>

#ifdef __linux__

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>

class MdnsBroadcaster::Impl {
public:
    Impl(const std::string &instanceId, const std::string &hostname,
         uint16_t signalingPort)
        : instanceId_(instanceId), hostname_(hostname),
          signalingPort_(signalingPort), state_(NodeState::IDLE) {}

    ~Impl() { stop(); }

    bool start() {
        simplePoll_ = avahi_simple_poll_new();
        if (!simplePoll_) {
            std::cerr << "mDNS: failed to create simple poll\n";
            return false;
        }

        int error;
        client_ = avahi_client_new(avahi_simple_poll_get(simplePoll_),
                                   AVAHI_CLIENT_NO_FAIL, clientCallback, this,
                                   &error);
        if (!client_) {
            std::cerr << "mDNS: failed to create client: "
                      << avahi_strerror(error) << "\n";
            avahi_simple_poll_free(simplePoll_);
            simplePoll_ = nullptr;
            return false;
        }

        pollThread_ = std::thread([this]() {
            avahi_simple_poll_loop(simplePoll_);
        });

        std::cout << "mDNS: broadcaster started (service: _remotedesktop._udp, "
                     "port: "
                  << signalingPort_ << ")\n";
        return true;
    }

    void stop() {
        if (simplePoll_) {
            avahi_simple_poll_quit(simplePoll_);
            if (pollThread_.joinable()) {
                pollThread_.join();
            }
        }
        if (entryGroup_) {
            avahi_entry_group_free(entryGroup_);
            entryGroup_ = nullptr;
        }
        if (browser_) {
            avahi_service_browser_free(browser_);
            browser_ = nullptr;
        }
        if (client_) {
            avahi_client_free(client_);
            client_ = nullptr;
        }
        if (simplePoll_) {
            avahi_simple_poll_free(simplePoll_);
            simplePoll_ = nullptr;
        }
    }

    void updateState(NodeState newState) {
        state_ = newState;
        if (entryGroup_ && client_ &&
            avahi_client_get_state(client_) == AVAHI_CLIENT_S_RUNNING) {
            registerService();
        }
    }

    void onPeerDiscovered(std::function<void(const PeerInfo &)> cb) {
        onPeerDiscovered_ = std::move(cb);
    }

    void onPeerRemoved(std::function<void(const std::string &)> cb) {
        onPeerRemoved_ = std::move(cb);
    }

private:
    void registerService() {
        if (!entryGroup_) {
            entryGroup_ = avahi_entry_group_new(client_, entryGroupCallback,
                                                this);
            if (!entryGroup_) {
                std::cerr << "mDNS: failed to create entry group: "
                          << avahi_strerror(avahi_client_errno(client_))
                          << "\n";
                return;
            }
        }

        avahi_entry_group_reset(entryGroup_);

        AvahiStringList *txt = nullptr;
        txt = avahi_string_list_add_pair(txt, "instance_id",
                                         instanceId_.c_str());
        txt = avahi_string_list_add_pair(txt, "hostname",
                                         hostname_.c_str());
        txt = avahi_string_list_add_pair(
            txt, "state",
            state_ == NodeState::BUSY ? "BUSY" : "IDLE");

        std::string portStr = std::to_string(signalingPort_);
        txt = avahi_string_list_add_pair(txt, "port", portStr.c_str());

        std::string serviceName = "RemoteDesktop_" + instanceId_.substr(0, 8);

        int ret = avahi_entry_group_add_service_strlst(
            entryGroup_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            static_cast<AvahiPublishFlags>(0), serviceName.c_str(),
            "_remotedesktop._udp", nullptr, nullptr, signalingPort_, txt);

        avahi_string_list_free(txt);

        if (ret < 0) {
            std::cerr << "mDNS: failed to add service: "
                      << avahi_strerror(ret) << "\n";
            return;
        }

        ret = avahi_entry_group_commit(entryGroup_);
        if (ret < 0) {
            std::cerr << "mDNS: failed to commit entry group: "
                      << avahi_strerror(ret) << "\n";
        }
    }

    void createBrowser() {
        browser_ = avahi_service_browser_new(
            client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            "_remotedesktop._udp", nullptr, static_cast<AvahiLookupFlags>(0),
            browseCallback, this);
        if (!browser_) {
            std::cerr << "mDNS: failed to create service browser: "
                      << avahi_strerror(avahi_client_errno(client_)) << "\n";
        }
    }

    static void clientCallback(AvahiClient *client, AvahiClientState state,
                               void *userdata) {
        auto *self = static_cast<Impl *>(userdata);
        self->client_ = client;
        switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            self->registerService();
            self->createBrowser();
            break;
        case AVAHI_CLIENT_FAILURE:
            std::cerr << "mDNS: client failure: "
                      << avahi_strerror(avahi_client_errno(client)) << "\n";
            avahi_simple_poll_quit(self->simplePoll_);
            break;
        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:
            if (self->entryGroup_) {
                avahi_entry_group_reset(self->entryGroup_);
            }
            break;
        case AVAHI_CLIENT_CONNECTING:
            break;
        }
    }

    static void entryGroupCallback(AvahiEntryGroup *group,
                                    AvahiEntryGroupState state,
                                    void *userdata) {
        (void)group;
        (void)userdata;
        switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            std::cout << "mDNS: service registered\n";
            break;
        case AVAHI_ENTRY_GROUP_COLLISION:
            std::cerr << "mDNS: service name collision\n";
            break;
        case AVAHI_ENTRY_GROUP_FAILURE:
            std::cerr << "mDNS: entry group failure\n";
            break;
        default:
            break;
        }
    }

    static void browseCallback(AvahiServiceBrowser *browser,
                                AvahiIfIndex interface, AvahiProtocol protocol,
                                AvahiBrowserEvent event, const char *name,
                                const char *type, const char *domain,
                                AvahiLookupResultFlags flags, void *userdata) {
        (void)browser;
        (void)flags;
        auto *self = static_cast<Impl *>(userdata);

        switch (event) {
        case AVAHI_BROWSER_NEW:
            avahi_service_resolver_new(
                self->client_, interface, protocol, name, type, domain,
                AVAHI_PROTO_UNSPEC, static_cast<AvahiLookupFlags>(0),
                resolveCallback, self);
            break;
        case AVAHI_BROWSER_REMOVE: {
            std::string svcName(name);
            std::string instanceId;
            if (svcName.starts_with("RemoteDesktop_")) {
                instanceId = svcName.substr(14);
            }
            std::cout << "mDNS: peer removed: " << name << "\n";
            if (self->onPeerRemoved_ && !instanceId.empty()) {
                self->onPeerRemoved_(instanceId);
            }
            break;
        }
        case AVAHI_BROWSER_FAILURE:
            std::cerr << "mDNS: browser failure\n";
            break;
        default:
            break;
        }
    }

    static void resolveCallback(AvahiServiceResolver *resolver,
                                 AvahiIfIndex interface, AvahiProtocol protocol,
                                 AvahiResolverEvent event, const char *name,
                                 const char *type, const char *domain,
                                 const char *host_name, const AvahiAddress *address,
                                 uint16_t port, AvahiStringList *txt,
                                 AvahiLookupResultFlags flags, void *userdata) {
        (void)interface;
        (void)protocol;
        (void)type;
        (void)domain;
        (void)flags;
        (void)host_name;

        auto *self = static_cast<Impl *>(userdata);

        if (event == AVAHI_RESOLVER_FOUND) {
            PeerInfo peer{};
            peer.instanceId = "unknown";
            peer.hostname = host_name ? host_name : "";
            {
                char addrBuf[AVAHI_ADDRESS_STR_MAX];
                peer.address = address
                    ? std::string(avahi_address_snprint(addrBuf, sizeof(addrBuf), address))
                    : "";
            }
            peer.signalingPort = port;

            std::string svcName(name);
            if (svcName.starts_with("RemoteDesktop_")) {
                peer.instanceId = svcName.substr(14);
            }

            for (AvahiStringList *l = txt; l; l = l->next) {
                char *key = nullptr, *val = nullptr;
                if (avahi_string_list_get_pair(l, &key, &val, nullptr) == 0) {
                    std::string k(key);
                    if (k == "instance_id" && val) {
                        peer.instanceId = val;
                    } else if (k == "hostname" && val) {
                        peer.hostname = val;
                    } else if (k == "state" && val) {
                        peer.state = (std::string(val) == "BUSY")
                                         ? NodeState::BUSY
                                         : NodeState::IDLE;
                    } else if (k == "port" && val) {
                        peer.signalingPort = static_cast<uint16_t>(std::stoi(val));
                    }
                    avahi_free(key);
                    avahi_free(val);
                }
            }

            if (peer.instanceId == self->instanceId_) {
                avahi_service_resolver_free(resolver);
                return;
            }

            std::cout << "mDNS: discovered peer: " << peer.instanceId
                      << " @ " << peer.address << ":" << peer.signalingPort
                      << " state=" << nodeStateToString(peer.state) << "\n";

            if (self->onPeerDiscovered_) {
                self->onPeerDiscovered_(peer);
            }
        } else {
            std::cerr << "mDNS: resolver failure: " << name << "\n";
        }

        avahi_service_resolver_free(resolver);
    }

    std::string instanceId_;
    std::string hostname_;
    uint16_t signalingPort_;
    NodeState state_;

    AvahiSimplePoll *simplePoll_ = nullptr;
    AvahiClient *client_ = nullptr;
    AvahiEntryGroup *entryGroup_ = nullptr;
    AvahiServiceBrowser *browser_ = nullptr;
    std::thread pollThread_;

    std::function<void(const PeerInfo &)> onPeerDiscovered_;
    std::function<void(const std::string &)> onPeerRemoved_;
};

#else // Windows stub

class MdnsBroadcaster::Impl {
public:
    Impl(const std::string &instanceId, const std::string &hostname,
         uint16_t signalingPort)
        : instanceId_(instanceId), hostname_(hostname),
          signalingPort_(signalingPort) {}

    bool start() {
        std::cout << "mDNS: stub implementation (not available on this OS)\n";
        return true;
    }
    void stop() {}
    void updateState(NodeState) {}

    void onPeerDiscovered(std::function<void(const PeerInfo &)> cb) {
        onPeerDiscovered_ = std::move(cb);
    }
    void onPeerRemoved(std::function<void(const std::string &)> cb) {
        onPeerRemoved_ = std::move(cb);
    }

private:
    std::string instanceId_;
    std::string hostname_;
    uint16_t signalingPort_;
    std::function<void(const PeerInfo &)> onPeerDiscovered_;
    std::function<void(const std::string &)> onPeerRemoved_;
};

#endif

MdnsBroadcaster::MdnsBroadcaster(const std::string &instanceId,
                                 const std::string &hostname,
                                 uint16_t signalingPort)
    : impl_(std::make_unique<Impl>(instanceId, hostname, signalingPort)) {}

MdnsBroadcaster::~MdnsBroadcaster() = default;

bool MdnsBroadcaster::start() { return impl_->start(); }
void MdnsBroadcaster::stop() { impl_->stop(); }
void MdnsBroadcaster::updateState(NodeState state) {
    impl_->updateState(state);
}

void MdnsBroadcaster::onPeerDiscovered(
    std::function<void(const PeerInfo &)> callback) {
    impl_->onPeerDiscovered(std::move(callback));
}

void MdnsBroadcaster::onPeerRemoved(
    std::function<void(const std::string &)> callback) {
    impl_->onPeerRemoved(std::move(callback));
}