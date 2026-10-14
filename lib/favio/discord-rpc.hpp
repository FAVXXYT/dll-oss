#pragma once
//
// FAVIO compatibility header: no-op Discord Rich Presence stub.
//
// The original `discord-presence` library (EclipseMenu/discord-presence) is no
// longer available, and FAVIO has no registered Discord application ID yet, so
// this header implements the exact discord:: API surface used by
// DiscordRPCListener.hpp as a silent no-op. Presence calls compile and run but
// never connect anywhere.
//
// TODO: register a FAVIO Discord application, replace CLIENT_ID in
// DiscordRPCListener.hpp, and swap this stub for a real IPC implementation.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace discord {

struct User {
    std::string username;
    std::string discriminator;
    std::string id;
    std::string avatar;
};

class Presence {
public:
    Presence& setDetails(const std::string& v) {
        m_details = v;
        return *this;
    }
    Presence& setState(const std::string& v) {
        m_state = v;
        return *this;
    }
    Presence& setLargeImageKey(const std::string& v) {
        m_largeImageKey = v;
        return *this;
    }
    Presence& setLargeImageText(const std::string& v) {
        m_largeImageText = v;
        return *this;
    }
    Presence& setSmallImageKey(const std::string& v) {
        m_smallImageKey = v;
        return *this;
    }
    Presence& setSmallImageText(const std::string& v) {
        m_smallImageText = v;
        return *this;
    }
    Presence& setStartTimestamp(std::int64_t v) {
        m_startTimestamp = v;
        return *this;
    }
    Presence& setButton1(const std::string& label, const std::string& url, bool enabled = true) {
        m_button1Label = label;
        m_button1Url = url;
        m_button1Enabled = enabled;
        return *this;
    }
    Presence& setButton2(const std::string& label, const std::string& url, bool enabled = true) {
        m_button2Label = label;
        m_button2Url = url;
        m_button2Enabled = enabled;
        return *this;
    }
    void refresh() {
        // No backend: presence is intentionally not published.
    }

private:
    std::string m_details;
    std::string m_state;
    std::string m_largeImageKey;
    std::string m_largeImageText;
    std::string m_smallImageKey;
    std::string m_smallImageText;
    std::int64_t m_startTimestamp = 0;
    std::string m_button1Label;
    std::string m_button1Url;
    bool m_button1Enabled = true;
    std::string m_button2Label;
    std::string m_button2Url;
    bool m_button2Enabled = true;
};

class RPCManager {
public:
    static RPCManager& get() {
        static RPCManager instance;
        return instance;
    }

    void setClientID(const std::string& id) { m_clientId = id; }
    void onReady(std::function<void(const User&)> cb) { m_onReady = std::move(cb); }
    void onDisconnected(std::function<void(int, std::string_view)> cb) { m_onDisconnected = std::move(cb); }
    void onErrored(std::function<void(int, std::string_view)> cb) { m_onErrored = std::move(cb); }
    void initialize() {
        // No backend: never connects, callbacks never fire.
    }
    Presence& getPresence() { return m_presence; }
    RPCManager& clearPresence() { return *this; }
    void shutdown() {}

private:
    std::string m_clientId;
    std::function<void(const User&)> m_onReady;
    std::function<void(int, std::string_view)> m_onDisconnected;
    std::function<void(int, std::string_view)> m_onErrored;
    Presence m_presence;
};

} // namespace discord
