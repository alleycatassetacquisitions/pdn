/*
This class is responsible for sending and receiving
data from an external server. Instead of using ESP-NOW
for mesh communication, this class makes HTTP requests
to the remote server. 

The SSID and password for the network the server is on
are hardcoded in the class.

There are two primary requests:
1. Make a post request to the server to upload 
   match data stored in PSRAM on the PDN.
2. Make a get request to the server to fetch
   a user's id
*/

#ifndef SERVER_COMMS_HPP
#define SERVER_COMMS_HPP


#endif