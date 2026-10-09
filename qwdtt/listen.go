package main

import "net"

func listenUDP(address string) (net.PacketConn, error) { return net.ListenPacket("udp", address) }
