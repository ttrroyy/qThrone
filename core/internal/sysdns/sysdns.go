package sysdns

const (
	// Address is a lo0 alias: a query the system resolver scopes to the physical interface still loops back to it.
	Address = "127.0.0.53"
	// InboundTag names generate.cpp's DNS inbound on Address:53; the core drops it when the address cannot be taken.
	InboundTag = "dns-system"
)
