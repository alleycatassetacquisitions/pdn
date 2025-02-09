#pragma once

#include <vector>
#include <queue>
#include <unordered_map>
#include <esp_now.h>

//Change to 1 to enable tracking rssi for peers
//This works, but requires wifi to be in promiscuous mode
//which likely prevents connecting to access points and
//requires an unknown but likely high amount of processing
//power
#define PDN_ENABLE_RSSI_TRACKING 0

//Use this mac address in order to reach all nearby devices
const uint8_t ESP_NOW_BROADCAST_ADDR[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//PktType determines which callback will handle the packet on the receiving end
enum class PktType : uint8_t
{
    kPlayerInfoBroadcast = 0,
    kNumPacketTypes //Not a real packet type, DO NOT USE
};


//Singleton class that handles communication over ESP-NOW protocol.
class EspNowManager
{
public:
    static EspNowManager* GetInstance();

    void Update();

    //Queues up data for sending, may not send right away
    int SendData(const uint8_t* dstMac, const PktType packetType, const uint8_t* data, size_t len);

    //Type for packet handler callbacks (as function pointers)
    typedef void(*PktHandler)(const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg);

    //Set the packet handler for a particular packet type
    //Only one handler can be registered per packet type at a time, so if a new
    //packet handler is registered for a packet type that has an existing handler,
    //the existing handler is automatically unregistered
    //userArg will be saved per packet type and will be passed in unmodified to
    //packet handler for that packet type (when a packet of that type is receieved)
    void SetPacketHandler(const PktType packetType, PktHandler callback, void* userArg);

    //Unregister packet handler for specified packet type
    void ClearPacketHandler(const PktType packetType);

#if PDN_ENABLE_RSSI_TRACKING
    int GetRssiForPeer(const uint8_t* macAddr);
#endif

private:
    EspNowManager();

#if PDN_ENABLE_RSSI_TRACKING
    //Callback for receiving raw Wifi packets, used for rssi tracking
    static void WifiPromiscuousRecvCallback(void *buf, wifi_promiscuous_pkt_type_t type);
#endif

    //ESP-NOW callbacks
    static void EspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *data, int data_len);
    static void EspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status);

    //Attempt to send the next packet in send queue
    int SendFrontPkt();

    //Free front packet in send queue and pop it from queue
    void MoveToNextSendPkt();

    //ESP-NOW requires peers to be registered but there's a limit,
    //so we'll need to unregister least recently used peer if we
    //hit the limit. This function will ensure a peer is registered.
    int EnsurePeerIsRegistered(const uint8_t* mac_addr);

    //Storage for packet handler callbacks and their user args
    std::vector<std::pair<PktHandler, void*> > m_pktHandlerCallbacks;

    //Handle received packet of a certain type
    void HandlePktCallback(const PktType packetType, const uint8_t* srcMacAddr, const uint8_t* pktData, const size_t pktLen);

    //Storage for retry handling
    uint8_t m_maxRetries;
    uint8_t m_curRetries;

    //Send queue stores packets to send using DataSendBuffer
    struct DataSendBuffer
    {
        uint8_t dstMac[6];
        uint8_t* ptr;
        size_t len;
    };

    //Packet send queue
    std::queue<DataSendBuffer> m_sendQueue;

    //When receiving a buffer that's split across multiple packets,
    //this struct will track the data while it's being received
    struct DataRecvBuffer
    {
        uint8_t* data;
        unsigned long mostRecentRecvPktTime;
        uint8_t expectedNextIdx;
    };
    std::unordered_map<uint64_t, DataRecvBuffer> m_recvBuffers;

#if PDN_ENABLE_RSSI_TRACKING
    //Storage for rssi, which is captured by wifi promiscuous callback
    std::unordered_map<uint64_t, int> m_rssiTracker;
#endif
};

static uint64_t MacToUInt64(const uint8_t* macAddr)
{
    uint64_t tmp = 0;
    for(int i = 0; i < ESP_NOW_ETH_ALEN; ++i)
        tmp = (tmp << 8) + macAddr[i];

    return tmp;
}

static const char* MacToString(const uint8_t* macAddr)
{
    static char macStr[18];
    snprintf(macStr, 18, "%X:%X:%X:%X:%X:%X",
        macAddr[0], macAddr[1], macAddr[2],
        macAddr[3], macAddr[4], macAddr[5]);
        macStr[17] = '\0';
    return macStr;
}