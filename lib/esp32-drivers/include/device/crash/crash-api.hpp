#pragma once

#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include <functional>
#include <string>


namespace CrashApi {

    inline void broadcastCrashes(
        WirelessManager* wirelessManager,
        const std::function<void(const std::string&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        std::string path = "/device-logs";
        
        HttpRequest request(
            path,
            "POST",
            "application/protobuf",
            "",
            onSuccess,
            onError
        );
        
        wirelessManager->queueHttpRequest(request);
    }
}