//
// Created by Elli Furedy on 10/4/2024.
//

#include <WString.h>

class IDeviceSerial {
public:

    virtual void writeString(String *msg) = 0;

    virtual void writeString(const String *msg) = 0;

    virtual String readString() = 0;

    virtual String *peekComms() = 0;

    virtual bool commsAvailable() = 0;

    virtual int getTrxBufferedMessagesSize() = 0;

protected:
    String head = "";
};