Device Pairing
==============



Flow Diagram
------------


```
      .------.               .-------.             .------.
      | old  |               |       |             |  new |
      |device|               |verifyd|             |device|
      '------'               '-------'             '------'
         |                       |                     |     [ User start a ]
         |                       |   POST /create      |     [ new pairing  ]
         |                       |<--------------------|
         |                       |   200 OK (id=42)    |
         |                       |-------------------->|
devpair  |                       |                     |     [ Show Pairing ID ]
publish()|                       |                     |
         |                       |                     |
         |  PUT /publish?id=42   |                     |
         |  (SDP Offer)          |                     |
         |---------------------->|                     |
         |  200 OK               |                     |  poll
         |<----------------------|                     +--.
         |                       |  GET /publish?id=42 | /|\
         |                       |<--------------------|  |
         |                       |   200 OK (SDP Offer)|  |
         |                       |-------------------->|  |
         |                       |                     +--'
         |                       |                     | ~~~ devpair_create()
         |                       |                     | ~~~ devpair_accept()
         |                       |                     |
         |                       |  PUT /accept?id=42  |
         |                       |   (SDP Answer)      |
         |                       |<--------------------|
   poll  |                       |      200 OK         |
      .--+                       |-------------------->|
     /|\ |  GET /accept?id=42    |                     |
      |  |---------------------->|                     |
      |  |  200 OK (SDP Answer)  |                     |
      |  |<----------------------|                     |
      '--+                       |                     |
devpair  |                       |                     |
ack()    |                       |                     |
         |                       |                     |
         |<================= DataChannel =============>|     [  Started]


TODO: transfer data (ledger)

```
