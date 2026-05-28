#ifdef NATIVE_BUILD

#include "device/drivers/native/native-http-client-driver.hpp"
#include "cli/cli-http-server.hpp"

void NativeHttpClientDriver::processPendingRequests() {
    // Process all pending requests
    while (!pendingRequests_.empty()) {
        HttpRequest request = pendingRequests_.front();
        pendingRequests_.pop();
        
        if (!mockServerEnabled_) {
            // Mock server disabled - fail immediately (original behavior)
            if (request.onError) {
                WirelessErrorInfo error;
                error.code = WirelessError::WIFI_NOT_CONNECTED;
                error.message = "Mock server disabled";
                error.willRetry = false;
                request.onError(error);
            }
            continue;
        }
        
        // Check if mock server is simulating offline
        cli::MockHttpServer& server = cli::MockHttpServer::getInstance();
        if (server.isOffline()) {
            if (request.onError) {
                WirelessErrorInfo error;
                error.code = WirelessError::WIFI_NOT_CONNECTED;
                error.message = "Server offline (simulated)";
                error.willRetry = true;
                request.onError(error);
            }
            
            // Track in history
            HttpRequestHistoryEntry entry;
            entry.method = request.method;
            entry.path = request.path;
            entry.requestBody = request.payload;
            entry.responseBody = "";
            entry.statusCode = 0;
            entry.success = false;
            addToHistory(entry);
            continue;
        }
        
        // Process request through mock server
        std::string responseBody;
        int statusCode = server.handleRequest(
            request.method, 
            request.path, 
            request.payload, 
            responseBody);
        
        // Track in history
        HttpRequestHistoryEntry entry;
        entry.method = request.method;
        entry.path = request.path;
        entry.requestBody = request.payload;
        entry.responseBody = responseBody;
        entry.statusCode = statusCode;
        entry.success = (statusCode >= 200 && statusCode < 300);
        addToHistory(entry);
        
        // Call appropriate callback
        if (statusCode >= 200 && statusCode < 300) {
            if (request.onSuccess) {
                request.onSuccess(responseBody);
            }
        } else {
            if (request.onError) {
                WirelessErrorInfo error;
                // Use SERVER_ERROR for 5xx, INVALID_RESPONSE for 4xx
                error.code = (statusCode >= 500) ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
                error.message = "HTTP " + std::to_string(statusCode);
                error.willRetry = false;
                request.onError(error);
            }
        }
    }
}

#endif // NATIVE_BUILD
