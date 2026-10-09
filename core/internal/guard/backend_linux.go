package guard

import (
	"errors"
	"fmt"
	"log"
	"net/netip"
	"os"
	"slices"
	"strings"
	"sync"
	"time"

	"github.com/sagernet/nftables"
	"golang.org/x/sys/unix"
)

const tableFlagOwner = 0x2 // NFT_TABLE_F_OWNER

const (
	ownershipMessage = "the kill switch needs Linux 5.12 or newer (nftables table ownership)"
	labelsMessage    = "the kill switch needs nftables conntrack labels in the kernel (CONFIG_NFT_CT and CONFIG_NF_CONNTRACK_LABELS)"
)

type nftBackend struct {
	conn      *nftables.Conn
	table     *nftables.Table
	committed bool

	stop     chan struct{}
	stopOnce sync.Once
	workers  sync.WaitGroup
}

func newBackend() (backend, error) {
	if os.Geteuid() == 0 {
		return &nftBackend{}, nil
	}
	if hasNetAdmin() {
		return nil, newError(CodeUnsupported, "this installation gives the core capabilities instead of root; the kill switch needs the setuid-root core so the core can switch to the guard group")
	}
	return nil, newError(CodePrivilege, "the kill switch needs the core to have root privileges")
}

func (b *nftBackend) Arm(cfg *config, events chan<- Event) error {
	if err := b.arm(cfg); err != nil {
		b.teardown()
		return err
	}
	if resolvers := lanResolvers("/etc/resolv.conf"); len(resolvers) > 0 {
		select {
		case events <- Event{Message: fmt.Sprintf("the system DNS server %v is on the local network and the kill switch blocks DNS to it; in Tun mode use systemd-resolved (Throne points it at the tunnel) or a DNS server outside the LAN", resolvers)}:
		default:
		}
	}
	b.stop = make(chan struct{})
	b.workers.Add(1)
	goSafe(events, func() {
		defer b.workers.Done()
		b.watchdog(events)
	})
	return nil
}

func lanResolvers(path string) []netip.Addr {
	content, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	var resolvers []netip.Addr
	for line := range strings.Lines(string(content)) {
		fields := strings.Fields(line)
		if len(fields) < 2 || fields[0] != "nameserver" {
			continue
		}
		addr, err := netip.ParseAddr(fields[1])
		if err != nil {
			continue
		}
		addr = addr.Unmap().WithZone("")
		if slices.ContainsFunc(slices.Concat(lanPrefixes4, lanPrefixes6), func(prefix netip.Prefix) bool { return prefix.Contains(addr) }) {
			resolvers = append(resolvers, addr)
		}
	}
	return resolvers
}

func (b *nftBackend) arm(cfg *config) error {
	b.table = &nftables.Table{Name: fmt.Sprintf("qthrone-guard-%d", os.Getpid()), Family: nftables.TableFamilyINet, Flags: tableFlagOwner}
	rules, err := newRuleset(b.table, cfg)
	if err != nil {
		return newError(CodeInternal, "build the kill switch rules: %w", err)
	}
	// The creating socket owns the table: other sockets get EPERM, and the kernel deletes it when this one closes.
	b.conn, err = nftables.New(nftables.AsLasting())
	if err != nil {
		return nftablesError("open the nftables socket", err)
	}
	// A batch to a missing nf_tables draws a single error while Flush waits for an ack per message.
	if _, err = b.conn.ListTablesOfFamily(nftables.TableFamilyINet); err != nil {
		return nftablesError("list the nftables tables", err)
	}
	if err = rules.queue(b.conn); err != nil {
		return newError(CodeInternal, "queue the kill switch rules: %w", err)
	}
	// Set before Flush: the batch can commit even if reading its acks fails.
	b.committed = true
	if err = b.conn.Flush(); err != nil {
		return installError(err)
	}
	table, err := b.conn.ListTableOfFamily(b.table.Name, nftables.TableFamilyINet)
	if err != nil {
		return newError(CodeInternal, "read back the kill switch table: %w", err)
	}
	if table.Flags&tableFlagOwner == 0 {
		return newError(CodeUnsupported, ownershipMessage)
	}
	log.Printf("guard: nftables table inet %s armed", b.table.Name)
	return nil
}

func installError(err error) error {
	if errnoIn(err, unix.EPERM, unix.EACCES) {
		return newError(CodePrivilege, "install the kill switch rules: %w", err)
	}
	log.Printf("guard: install the kill switch rules: %v", err)
	switch ownerErr := probe(false); {
	case errnoIn(ownerErr, unix.EOPNOTSUPP, unix.EINVAL):
		return newError(CodeUnsupported, ownershipMessage)
	case ownerErr == nil && errnoIn(probe(true), unix.EOPNOTSUPP, unix.ENOENT):
		return newError(CodeUnsupported, labelsMessage)
	}
	return newError(CodeInternal, "install the kill switch rules: %w", err)
}

// probe uses a socket of its own: a failed batch can leave replies unread on the owner socket.
func probe(labels bool) error {
	conn, err := nftables.New()
	if err != nil {
		return err
	}
	table := conn.AddTable(&nftables.Table{Name: fmt.Sprintf("qthrone-guard-%d-probe", os.Getpid()), Family: nftables.TableFamilyINet, Flags: tableFlagOwner})
	if labels {
		chain := conn.AddChain(&nftables.Chain{Name: "probe", Table: table})
		conn.AddRule(&nftables.Rule{Table: table, Chain: chain, Exprs: matchLabel()})
		conn.AddRule(&nftables.Rule{Table: table, Chain: chain, Exprs: setLabel()})
	}
	conn.DelTable(table)
	return conn.Flush()
}

func (b *nftBackend) Disarm() {
	b.stopOnce.Do(func() {
		if b.stop != nil {
			close(b.stop)
		}
	})
	// Flush and a concurrent dump on one socket would read each other's replies.
	b.workers.Wait()
	b.teardown()
}

func (b *nftBackend) teardown() {
	if b.conn == nil {
		return
	}
	if b.committed {
		b.conn.DelTable(b.table)
		if err := b.conn.Flush(); err != nil && !errors.Is(err, unix.ENOENT) {
			log.Printf("guard: delete nftables table %s: %v", b.table.Name, err)
		}
		b.committed = false
	}
	_ = b.conn.CloseLasting()
	b.conn = nil
}

func (b *nftBackend) watchdog(events chan<- Event) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	failing := false
	for {
		select {
		case <-b.stop:
			return
		case <-ticker.C:
		}
		present, err := b.present()
		if err != nil {
			if !failing {
				log.Printf("guard: nftables check failed: %v", err)
			}
			failing = true
			continue
		}
		failing = false
		if present {
			continue
		}
		select {
		case events <- Event{Fatal: true, Code: CodeTampered, Message: "the kill switch nftables table was removed"}:
		case <-b.stop:
		}
		return
	}
}

func (b *nftBackend) present() (bool, error) {
	table, err := b.conn.ListTableOfFamily(b.table.Name, nftables.TableFamilyINet)
	if errors.Is(err, unix.ENOENT) {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	return table.Flags&tableFlagOwner != 0, nil
}

func nftablesError(op string, err error) error {
	switch {
	case errnoIn(err, unix.EPERM, unix.EACCES):
		return newError(CodePrivilege, "%s: %w", op, err)
	// nfnetlink answers EINVAL when it cannot load the nf_tables subsystem.
	case errnoIn(err, unix.EPROTONOSUPPORT, unix.ENOENT, unix.EINVAL, unix.EOPNOTSUPP, unix.EAFNOSUPPORT):
		log.Printf("guard: %s: %v", op, err)
		return newError(CodeUnsupported, "nftables is not available in this kernel")
	}
	return newError(CodeInternal, "%s: %w", op, err)
}

func errnoIn(err error, errnos ...unix.Errno) bool {
	return slices.ContainsFunc(errnos, func(errno unix.Errno) bool { return errors.Is(err, errno) })
}

func hasNetAdmin() bool {
	header := unix.CapUserHeader{Version: unix.LINUX_CAPABILITY_VERSION_3}
	var data [2]unix.CapUserData
	if err := unix.Capget(&header, &data[0]); err != nil {
		return false
	}
	return data[unix.CAP_NET_ADMIN/32].Effective&(1<<(unix.CAP_NET_ADMIN%32)) != 0
}
