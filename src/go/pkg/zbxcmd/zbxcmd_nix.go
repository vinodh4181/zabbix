// +build !windows

/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package zbxcmd

import (
	"bytes"
	"fmt"
	"os/exec"
	"strings"
	"syscall"
	"time"

	"zabbix.com/pkg/log"
)

// Execute runs the 's' command without checking cmd.Wait error.
// This means that non zero exit status code will not return an error.
// Returns an error if there is an issue with executing the command or
// if the specified timeout has been reached or if maximum output length
// has been reached.
func Execute(s string, timeout time.Duration) (string, error) {
	return execute(s, timeout, false)
}

// ExecuteStrict runs the 's' command and checks cmd.Wait error.
// This means that non zero exit status code will return an error.
// Also returns an error if there is an issue with executing the command or
// if the specified timeout has been reached or if maximum output length
// has been reached.
func ExecuteStrict(s string, timeout time.Duration) (string, error) {
	return execute(s, timeout, true)
}

func execute(s string, timeout time.Duration, strict bool) (string, error) {
	cmd := exec.Command("sh", "-c", s)

	var b bytes.Buffer
	cmd.Stdout = &b
	cmd.Stderr = &b

	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}

	err := cmd.Start()

	if err != nil {
		return "", fmt.Errorf("Cannot execute command: %s", err)
	}

	t := time.AfterFunc(timeout, func() {
		errKill := syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
		if errKill != nil {
			log.Warningf("failed to kill [%s]: %s", s, errKill)
		}
	})

	if strict {
		if err = cmd.Wait(); err != nil {
			return "", fmt.Errorf("Command execution failed: %s", err)
		}
	} else {
		_ = cmd.Wait()
	}

	if !t.Stop() {
		return "", fmt.Errorf("Timeout while executing a shell script.")
	}

	if maxExecuteOutputLenB <= len(b.String()) {
		return "", fmt.Errorf("Command output exceeded limit of %d KB", maxExecuteOutputLenB/1024)
	}

	return strings.TrimRight(b.String(), " \t\r\n"), nil
}

func ExecuteBackground(s string) error {
	cmd := exec.Command("sh", "-c", s)
	err := cmd.Start()

	if err != nil {
		return fmt.Errorf("Cannot execute command: %s", err)
	}

	go cmd.Wait()

	return nil
}
