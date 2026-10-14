#include "AutoGG.hpp"


void AutoGG::onEnable()
{
    Listen(this, PacketEvent, &AutoGG::onPacketReceive)
    Module::onEnable();
}

void AutoGG::onDisable()
{
    Deafen(this, PacketEvent, &AutoGG::onPacketReceive)
    Module::onDisable();
}

void AutoGG::defaultConfig()
{
    setDef("enabled", true);
    Module::defaultConfig("core");
    setDef("text", (std::string)"GG");
}

void AutoGG::settingsRender(float settingsOffset)
{
    initSettingsPage();

    addTextBox("Text", "The text to send.", 0, "text");

    FlarialGUI::UnsetScrollView();

    resetPadding();
}

void AutoGG::onPacketReceive(PacketEvent& event)
{
    if (!this->isEnabled()) return;
    MinecraftPacketIds id = event.getPacket()->getId();

    // TODO: add support for other servers (look for "won the game" text)
    if (id == MinecraftPacketIds::SetTitle)
    {
        auto* pkt = reinterpret_cast<SetTitlePacket*>(event.getPacket());
        static const std::regex rgxChRu(R"(Is The §6§l(Chronos|Rush) (Champion|Champions)!)");
        if (
                // Zeqa
            pkt->text == "§f§aYou won the game!" || // pre s8
            pkt->text == "§f§cYou lost the game!" || // pre s8
            pkt->text == "   " || // s8
            pkt->text == "   " || // s8
            // Galaxite
            pkt->text.find("Team§r§a won the game!") != std::string::npos ||
            pkt->text.find("§bHiders§r§f Win") != std::string::npos ||
            pkt->text.find("§eSeekers§r§f Win") != std::string::npos ||
            pkt->text == "Finished" ||
            pkt->text == "Out of Time!" ||
            std::regex_search(pkt->text, rgxChRu) ||
            // Mineville
            pkt->text == "§aYou Win!" ||
            pkt->text == "§cGame Over!"
        )
            SendGG();
    }

    if (id == MinecraftPacketIds::Text)
    {
        auto pktOpt = getTextPacket(event.getPacket());
        if (!pktOpt) return;
        auto& pkt = *pktOpt;
        if (pkt.message == "§c§l» §r§c§lGame OVER!" || // The Hive
            pkt.message.find("§a won the game!") != std::string::npos || // CubeCraft
            pkt.message.find("§a has won the game!") != std::string::npos // Lifeboat
        )
            SendGG();
    }
}

void AutoGG::SendGG()
{
    if (!this->isEnabled()) return;
    std::string win_message = getOps<std::string>("text");
    if (!win_message.empty())
    {
        auto player = SDK::clientInstance->getLocalPlayer();
        std::shared_ptr<Packet> packet = SDK::createPacket(9);
        craftChatPacket(packet.get(), player->getPlayerName(), win_message);

        SDK::clientInstance->getPacketSender()->sendToServer(packet.get());
    }
};