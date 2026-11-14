#ifdef NATIVE_BUILD

#include "device/drivers/native/native-http-client-driver.hpp"
#include "cli/cli-http-server.hpp"

void NativeHttpClientDriver::processPendingRequests() {
    // Process all pending requests
    while (!pendingRequests.empty()) {
        HttpRequest request = std::move(pendingRequests.front());
        pendingRequests.pop();

        if (!isConnected()) {
            // No WiFi association, so the request never reaches the mock server.
            if (request.onError) {
                WirelessErrorInfo error;
                error.code = WirelessError::WIFI_NOT_CONNECTED;
                error.message = "HTTP client disconnected";
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

        // An oversized body is rejected as the device client rejects it: neither
        // kept in the history nor delivered.
        const bool tooLarge = responseBody.size() > MAX_HTTP_RESPONSE_BYTES;
        const bool success = !tooLarge && statusCode >= 200 && statusCode < 300;

        HttpRequestHistoryEntry entry;
        entry.method = request.method;
        entry.path = request.path;
        entry.requestBody = request.payload;
        entry.responseBody = tooLarge ? "" : responseBody;
        entry.statusCode = statusCode;
        entry.success = success;
        addToHistory(entry);

        if (success) {
            if (request.onSuccess) {
                request.onSuccess(responseBody);
            }
        } else if (request.onError) {
            WirelessErrorInfo error;
            if (tooLarge) {
                error.code = WirelessError::INVALID_RESPONSE;
                error.message = "Response too large";
            } else {
                // SERVER_ERROR for 5xx, INVALID_RESPONSE for 4xx.
                error.code =
                (statusCode >= 500) ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
                error.message = "HTTP " + std::to_string(statusCode);
                error.httpStatus = statusCode;
            }
            request.onError(error);
        }
    }
}

#endif // NATIVE_BUILD
