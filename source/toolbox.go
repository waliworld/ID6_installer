package main

import (
	"bytes"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"time"
)

// The stages handled by the bundled Python backend (tools/toolbox.py) are
// selfcheck, unlock, carplay and carplay-activate; the Go gauge engine owns
// discover/inspect/install/recover. Routing lives in main.go's request switch.

func pythonExe() string {
	candidates := []string{"python3", "python"}
	if runtime.GOOS == "windows" {
		candidates = []string{"py", "python", "python3"}
	}
	for _, c := range candidates {
		if p, err := exec.LookPath(c); err == nil {
			return p
		}
	}
	return "python3"
}

// toolboxScript finds tools/toolbox.py next to the executable (packaged layout:
// the app ships alongside a tools/ directory), falling back to the source tree.
func toolboxScript() string {
	if exe, err := os.Executable(); err == nil {
		if p := filepath.Join(filepath.Dir(exe), "tools", "toolbox.py"); exists(p) {
			return p
		}
	}
	for _, p := range []string{
		filepath.Join("tools", "toolbox.py"),
		filepath.Join("..", "tools", "toolbox.py"),
	} {
		if exists(p) {
			return p
		}
	}
	return filepath.Join("..", "tools", "toolbox.py")
}

func exists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

// runToolbox shells out to the Python backend for the SSH/CarPlay stages. The
// backend reads the JSON request body on stdin and writes a JSON result on stdout,
// so the Go side stays a thin, well-defined bridge over tested Python.
func runToolbox(op string, body []byte) json.RawMessage {
	cmd := exec.Command(pythonExe(), "-I", toolboxScript(), op)
	cmd.Stdin = bytes.NewReader(body)
	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	cmd.WaitDelay = 5 * time.Second
	if err := cmd.Run(); err != nil {
		msg, _ := json.Marshal(map[string]string{
			"Result": "Backend could not run (" + err.Error() + "). Is Python 3 installed and on PATH? " + tailStr(errb.String(), 300),
		})
		return msg
	}
	trimmed := bytes.TrimSpace(out.Bytes())
	if !json.Valid(trimmed) {
		msg, _ := json.Marshal(map[string]string{
			"Result": "Backend returned no usable result. " + tailStr(errb.String(), 300),
		})
		return msg
	}
	return json.RawMessage(trimmed)
}

func tailStr(s string, n int) string {
	if len(s) > n {
		return s[len(s)-n:]
	}
	return s
}
