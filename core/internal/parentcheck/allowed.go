package parentcheck

import (
	"runtime"
	"strings"
)

func allowedParent(parentDir, selfDir, name string, relay bool) bool {
	if runtime.GOOS == "windows" {
		return strings.EqualFold(parentDir, selfDir) && (strings.EqualFold(name, "Rawline.exe") || relay && strings.EqualFold(name, "qwdtt.exe"))
	}
	return parentDir == selfDir && (name == "Rawline" || relay && name == "qwdtt")
}
