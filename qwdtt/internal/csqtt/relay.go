package csqtt

import (
	"context"
	"errors"
	"io"
	"net"
)

// RelayIP connects a userspace IP device to the original client's UDP
// dispatcher. The connected UDP socket accepts packets only from that client.
// RelayIP owns both resources so cancellation also interrupts blocked reads.
func RelayIP(ctx context.Context, device io.ReadWriteCloser, socket *net.UDPConn) error {
	results := make(chan error, 2)
	pump := func(dst io.Writer, src io.Reader) {
		packet := make([]byte, 65535)
		for {
			n, err := src.Read(packet)
			if n > 0 {
				written, writeErr := dst.Write(packet[:n])
				if writeErr != nil {
					results <- writeErr
					return
				}
				if written != n {
					results <- io.ErrShortWrite
					return
				}
			}
			if err != nil {
				results <- err
				return
			}
		}
	}
	go pump(socket, device)
	go pump(device, socket)
	var err error
	completed := 0
	select {
	case <-ctx.Done():
		err = ctx.Err()
	case err = <-results:
		completed = 1
	}
	device.Close()
	socket.Close()
	for completed < 2 {
		<-results
		completed++
	}
	if errors.Is(err, net.ErrClosed) && ctx.Err() != nil {
		return ctx.Err()
	}
	return err
}
