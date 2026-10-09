package main

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"
)

func archiveFor(t *testing.T, entry string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "release.zip")
	f, err := os.Create(p)
	if err != nil {
		t.Fatal(err)
	}
	w := zip.NewWriter(f)
	e, err := w.Create(entry)
	if err != nil {
		t.Fatal(err)
	}
	e.Write([]byte("release payload"))
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestExtractRejectsTraversal(t *testing.T) {
	for _, entry := range []string{"qThrone/../escape", "qThrone/../../escape", "../qThrone/file", "Throne/qThrone.exe", "qThrone/..\\escape"} {
		t.Run(entry, func(t *testing.T) {
			if err := extract(archiveFor(t, entry), t.TempDir()); err == nil {
				t.Fatal("accepted unsafe entry")
			}
		})
	}
}

func TestExtractRelease(t *testing.T) {
	stage := t.TempDir()
	if err := extract(archiveFor(t, "qThrone/usr/lib/test.dll"), stage); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(stage, "usr", "lib", "test.dll"))
	if err != nil || string(data) != "release payload" {
		t.Fatalf("payload: %q, %v", data, err)
	}
}
