//go:build unix

package relay

import (
	"fmt"
	"os"
	"os/user"
	"strconv"
	"syscall"
)

// The same core binary may be setuid for TUN. VK and SSH management always run as the launching user.
func DropPrivileges() error {
	if os.Geteuid() != 0 {
		return nil
	}
	uid, gid := os.Getuid(), os.Getgid()
	if uid == 0 {
		return fmt.Errorf("Откройте VK-релей от обычного пользователя, не root")
	}
	owner, e := user.LookupId(strconv.Itoa(uid))
	if e != nil {
		return e
	}
	ids, e := owner.GroupIds()
	if e != nil {
		return e
	}
	groups := []int{gid}
	for _, id := range ids {
		n, e := strconv.Atoi(id)
		if e == nil && n != gid {
			groups = append(groups, n)
		}
	}
	if e = syscall.Setgroups(groups); e != nil {
		return e
	}
	if e = syscall.Setgid(gid); e != nil {
		return e
	}
	if e = syscall.Setuid(uid); e != nil {
		return e
	}
	if e = os.Setenv("HOME", owner.HomeDir); e != nil {
		return e
	}
	return nil
}
