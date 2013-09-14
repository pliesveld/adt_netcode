adt_netcode
===========

A rate adaptive sliding window protocol.  Probes the available bandwidth of a channel by LIMD and EIMD.  Implements Go-Back-N with cumulative acknowledgements and selective-acknowledgements.

Features Nagles algorithm, Jacobson variance estimation, and Clark window acknowledgement strategies.  Fast-retransmit (triple ack triggers timeout event).


contents
========

libnet/  simulates network drops, RTT and BW conditions.

server _port_

client _host_ _port_ _file_

ploticus.*  graph scripts used to visualize performance
