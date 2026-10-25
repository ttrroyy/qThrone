package sysdns

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"net"
	"net/netip"
	"os"
	"regexp"
	"slices"
	"strings"
	"sync"

	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/common/shell"
	"github.com/sagernet/sing/common/x/list"
	"golang.org/x/sys/unix"
)

const (
	stateDir  = "/var/db/Throne"
	stateFile = stateDir + "/system-dns.json"
	lockFile  = stateDir + "/system-dns.lock"
)

// record is written before every change, so whatever a dead core left behind can be put back by the next one.
type record struct {
	PID      int                 `json:"pid"`
	Alias    bool                `json:"alias,omitempty"`
	Services map[string][]string `json:"services,omitempty"`
}

var (
	mu        sync.Mutex
	owned     record
	current   string
	monitor   tun.DefaultInterfaceMonitor
	callback  *list.Element[tun.DefaultInterfaceUpdateCallback]
	followGen int
)

var serviceOrderPattern = regexp.MustCompile(`(?m)^\((\d+|\*)\) (.+)\n\(Hardware Port: .*, Device: (.*)\)$`)

// Prepare adds the lo0 alias and checks that Address:53 can be bound; on error the system DNS must be left alone.
func Prepare() error {
	if os.Geteuid() != 0 {
		return errors.New("the core is not running as root")
	}
	mu.Lock()
	defer mu.Unlock()
	leftovers := restoreStaleLocked()
	owned.PID = os.Getpid()
	// Decided before the change is recorded: an alias that is already there belongs to someone else.
	owned.Alias = owned.Alias || !aliasPresent()
	for service, previous := range leftovers {
		if owned.Services == nil {
			owned.Services = map[string][]string{}
		}
		owned.Services[service] = previous
	}
	if err := saveLocked(); err != nil {
		return err
	}
	if owned.Alias && !aliasPresent() {
		if err := addAlias(); err != nil {
			return err
		}
	}
	return probe()
}

// Apply points the default interface's network service at Address and follows the default interface from then on.
func Apply(interfaceMonitor tun.DefaultInterfaceMonitor) error {
	mu.Lock()
	defer mu.Unlock()
	if owned.PID == 0 {
		return errors.New("not prepared")
	}
	monitor = interfaceMonitor
	followGen++
	gen := followGen
	callback = monitor.RegisterCallback(func(*control.Interface, int) {
		// Off the monitor's goroutine: networksetup is slow, and each run reconciles to the latest default anyway.
		go follow(gen)
	})
	return followLocked()
}

// Release puts back every service Apply changed and removes the alias; it is a no-op when nothing was changed.
func Release() {
	mu.Lock()
	defer mu.Unlock()
	followGen++
	if monitor != nil && callback != nil {
		monitor.UnregisterCallback(callback)
	}
	monitor, callback, current = nil, nil, ""
	if owned.PID == 0 {
		return
	}
	for service := range owned.Services {
		restoreLocked(service)
	}
	if owned.Alias {
		removeAlias()
		owned.Alias = false
	}
	if len(owned.Services) > 0 {
		// Kept under this PID: the next core retries once this process is gone.
		if err := saveLocked(); err != nil {
			log.Println("system DNS:", err)
		}
		return
	}
	removeRecordLocked()
	owned = record{}
}

// RestoreStale undoes what a core that died without Release left in the system DNS.
func RestoreStale() {
	if os.Geteuid() != 0 {
		return
	}
	mu.Lock()
	defer mu.Unlock()
	restoreStaleLocked()
}

func follow(gen int) {
	mu.Lock()
	defer mu.Unlock()
	if gen != followGen {
		return
	}
	if err := followLocked(); err != nil {
		log.Println("system DNS:", err)
	}
}

func followLocked() error {
	defaultInterface := monitor.DefaultInterface()
	if defaultInterface == nil {
		return nil
	}
	service, err := serviceForDevice(defaultInterface.Name)
	if err != nil {
		return err
	}
	if service == current {
		return nil
	}
	if err = pointLocked(service); err != nil {
		return err
	}
	if previous := current; previous != "" {
		restoreLocked(previous)
	}
	current = service
	return nil
}

func pointLocked(service string) error {
	if _, ok := owned.Services[service]; !ok {
		previous, err := dnsServers(service)
		if err != nil {
			return err
		}
		// Only a dead core leaves exactly this behind, and its record has already been replayed.
		if slices.Equal(previous, []string{Address}) {
			previous = nil
		}
		if owned.Services == nil {
			owned.Services = map[string][]string{}
		}
		owned.Services[service] = previous
		if err = saveLocked(); err != nil {
			delete(owned.Services, service)
			return err
		}
	}
	return setDNSServers(service, []string{Address})
}

func restoreLocked(service string) {
	if err := setDNSServers(service, owned.Services[service]); err != nil {
		log.Println("system DNS: restore", service+":", err)
		return
	}
	delete(owned.Services, service)
	if err := saveLocked(); err != nil {
		log.Println("system DNS:", err)
	}
}

// restoreStaleLocked replays the record of a dead core and returns the services it could not put back.
func restoreStaleLocked() map[string][]string {
	var leftovers map[string][]string
	err := withStateLock(func() error {
		stored, err := loadRecord()
		if err != nil || stored.PID == os.Getpid() || alive(stored.PID) {
			return nil
		}
		for service, previous := range stored.Services {
			if err = setDNSServers(service, previous); err != nil {
				log.Println("system DNS: restore", service+":", err)
				continue
			}
			delete(stored.Services, service)
		}
		if stored.Alias {
			removeAlias()
			stored.Alias = false
		}
		if len(stored.Services) == 0 {
			return os.Remove(stateFile)
		}
		leftovers = stored.Services
		return writeRecord(stored)
	})
	if err != nil {
		log.Println("system DNS:", err)
	}
	return leftovers
}

func saveLocked() error {
	return withStateLock(func() error {
		return writeRecord(owned)
	})
}

func removeRecordLocked() {
	err := withStateLock(func() error {
		// A later core may have taken the file over since.
		if stored, err := loadRecord(); err != nil || stored.PID != os.Getpid() {
			return nil
		}
		return os.Remove(stateFile)
	})
	if err != nil {
		log.Println("system DNS:", err)
	}
}

func withStateLock(fn func() error) error {
	if err := os.MkdirAll(stateDir, 0o700); err != nil {
		return err
	}
	file, err := os.OpenFile(lockFile, os.O_CREATE|os.O_RDWR, 0o600)
	if err != nil {
		return err
	}
	defer file.Close()
	if err = unix.Flock(int(file.Fd()), unix.LOCK_EX); err != nil {
		return err
	}
	defer unix.Flock(int(file.Fd()), unix.LOCK_UN)
	return fn()
}

func loadRecord() (record, error) {
	var stored record
	data, err := os.ReadFile(stateFile)
	if err != nil {
		return stored, err
	}
	return stored, json.Unmarshal(data, &stored)
}

func writeRecord(stored record) error {
	data, err := json.Marshal(stored)
	if err != nil {
		return err
	}
	temp := stateFile + ".tmp"
	if err = os.WriteFile(temp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(temp, stateFile)
}

// alive also matches the command name: the record survives reboots, where its PID often belongs to someone else.
func alive(pid int) bool {
	if pid <= 0 {
		return false
	}
	owner, err := unix.SysctlKinfoProc("kern.proc.pid", pid)
	if err != nil || int(owner.Proc.P_pid) != pid {
		return false
	}
	self, err := unix.SysctlKinfoProc("kern.proc.pid", os.Getpid())
	return err == nil && owner.Proc.P_comm == self.Proc.P_comm
}

func aliasPresent() bool {
	loopback, err := net.InterfaceByName("lo0")
	if err != nil {
		return false
	}
	addrs, err := loopback.Addrs()
	if err != nil {
		return false
	}
	for _, addr := range addrs {
		if prefix, ok := addr.(*net.IPNet); ok && prefix.IP.String() == Address {
			return true
		}
	}
	return false
}

func addAlias() error {
	output, err := shell.Exec("/sbin/ifconfig", "lo0", "alias", Address, "netmask", "255.255.255.255").Read()
	return withOutput(err, output)
}

func removeAlias() {
	if !aliasPresent() {
		return
	}
	output, err := shell.Exec("/sbin/ifconfig", "lo0", "-alias", Address).Read()
	if err = withOutput(err, output); err != nil {
		log.Println("system DNS: remove the loopback alias:", err)
	}
}

// probe binds with the reuse options sing-box's reuse_addr sets, so success means the inbound will start.
func probe() error {
	config := net.ListenConfig{Control: control.ReuseAddr()}
	address := net.JoinHostPort(Address, "53")
	packetConn, err := config.ListenPacket(context.Background(), "udp4", address)
	if err != nil {
		return E.Cause(err, "listen on ", address)
	}
	packetConn.Close()
	listener, err := config.Listen(context.Background(), "tcp4", address)
	if err != nil {
		return E.Cause(err, "listen on ", address)
	}
	return listener.Close()
}

func serviceForDevice(device string) (string, error) {
	output, err := shell.Exec("/usr/sbin/networksetup", "-listnetworkserviceorder").ReadOutput()
	if err != nil {
		return "", err
	}
	for _, match := range serviceOrderPattern.FindAllStringSubmatch(output, -1) {
		// "(*)" marks a disabled service.
		if match[1] != "*" && match[3] == device {
			return match[2], nil
		}
	}
	return "", E.New("no enabled network service uses ", device)
}

func dnsServers(service string) ([]string, error) {
	output, err := shell.Exec("/usr/sbin/networksetup", "-getdnsservers", service).ReadOutput()
	if err != nil {
		return nil, err
	}
	var servers []string
	for line := range strings.Lines(output) {
		line = strings.TrimSpace(line)
		if _, parseErr := netip.ParseAddr(line); parseErr == nil {
			servers = append(servers, line)
		} else if !strings.HasPrefix(line, "There aren't any DNS Servers") {
			return nil, E.New("read the DNS servers of ", service, ": ", output)
		}
	}
	return servers, nil
}

func setDNSServers(service string, servers []string) error {
	args := append([]string{"-setdnsservers", service}, servers...)
	if len(servers) == 0 {
		args = append(args, "Empty")
	}
	// networksetup commits through configd; only its plist backup needs root, so its "Permission denied" is noise.
	output, err := shell.Exec("/usr/sbin/networksetup", args...).Read()
	return withOutput(err, output)
}

func withOutput(err error, output string) error {
	if err == nil {
		return nil
	}
	if output = strings.TrimSpace(output); output != "" {
		return E.Cause(err, output)
	}
	return err
}
