"""
shell_client — BabyOS device shell (nr_micro_shell) text client.

Parameter adjustment is NOT a b_protocol frame — it is a shell text
command sent over the already-open serial port (firmware b_mod_param.c):

  "param\\r"                 → list all params, each printed as ": name\\r\\n"
  "param <name>\\r"          → get, printed as "<name>:<value>\\r\\n"
  "param <name> <value>\\r"  → set via atoi(), no success line on firmware

Authority:
  - bos/modules/b_mod_param.c (_ShellParamHandle)
  - test/selftest/test_modules.c param shell cases
  - bos/thirdparty/nr_micro_shell (line-oriented, CR-terminated)

Python 3.8 compatible. Real UART I/O only.
"""

from __future__ import print_function

import re
import time
from typing import Any, List, Optional


class ShellClient:
    """
    Send shell text commands over a UART and collect the response.

    uart must provide write(data) -> int and read_available() -> bytes.
    """

    def __init__(self, uart: Any, line_ending: str = '\r',
                 default_timeout: float = 1.0) -> None:
        if uart is None:
            raise ValueError('uart is required')
        self._uart = uart
        self._line_ending = line_ending
        self._default_timeout = float(default_timeout)
        self._rx_text = ''          # decoded leftovers
        self._raw_log: List[bytes] = []  # raw frames written

    # ------------------------------------------------------------------
    # low-level I/O
    # ------------------------------------------------------------------

    @property
    def line_ending(self) -> str:
        return self._line_ending

    def write(self, text: str) -> int:
        """Write a shell line (auto-appends line_ending if missing)."""
        if text is None:
            return -1
        if not text.endswith(self._line_ending):
            text = text + self._line_ending
        data = text.encode('utf-8')
        self._raw_log.append(data)
        return self._uart.write(data)

    def read_response(self, timeout: Optional[float] = None,
                      idle_gap: float = 0.05,
                      poll_interval: float = 0.01) -> str:
        """
        Collect shell output until `timeout` expires or an idle gap of
        `idle_gap` seconds passes after the last received byte.

        Returns decoded text (utf-8, errors=replace). Empty string on
        timeout with no data.
        """
        if timeout is None:
            timeout = self._default_timeout
        deadline = time.time() + max(0.0, float(timeout))
        chunks: List[bytes] = []
        last_rx = 0.0
        got_any = False

        while True:
            now = time.time()
            if now >= deadline:
                break
            try:
                data = self._uart.read_available()
            except Exception:
                data = b''
            if data:
                chunks.append(data)
                got_any = True
                last_rx = time.time()
                # keep draining while data flows
                continue
            if got_any and (time.time() - last_rx) >= idle_gap:
                break
            time.sleep(poll_interval)

        if chunks:
            text = b''.join(chunks).decode('utf-8', errors='replace')
        else:
            text = ''
        self._rx_text = text
        return text

    def send_command(self, cmd: str, timeout: Optional[float] = None,
                     idle_gap: float = 0.05) -> str:
        """Write cmd + line_ending, then collect the response text."""
        self.write(cmd)
        return self.read_response(timeout=timeout, idle_gap=idle_gap)

    def clear_rx(self) -> None:
        """Discard any pending UART bytes and internal text buffer."""
        self._rx_text = ''
        try:
            self._uart.read_available()
        except Exception:
            pass

    # ------------------------------------------------------------------
    # param command wrappers (firmware b_mod_param.c)
    # ------------------------------------------------------------------

    @staticmethod
    def _parse_list(text: str) -> List[str]:
        """Parse ': name' lines produced by `param` with no args."""
        names: List[str] = []
        for raw_line in text.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
            line = raw_line.strip()
            if not line:
                continue
            # firmware: b_log(": %s\r\n", name)
            if line.startswith(':'):
                name = line[1:].strip()
                if name:
                    names.append(name)
        return names

    @staticmethod
    def _parse_get(text: str, name: str) -> Optional[int]:
        """
        Parse '<name>:<value>' lines produced by `param <name>`.
        Returns int value or None if not found.
        """
        if not name:
            return None
        # prefer exact "name:value" at start of a line (after optional ': ')
        pattern = re.compile(r'^\s*:?\s*' + re.escape(name) + r'\s*:\s*(-?\d+)\s*$')
        for raw_line in text.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
            m = pattern.match(raw_line)
            if m:
                try:
                    return int(m.group(1))
                except ValueError:
                    return None
        # fallback: any "name:value" substring
        m = re.search(re.escape(name) + r'\s*:\s*(-?\d+)', text)
        if m:
            try:
                return int(m.group(1))
            except ValueError:
                return None
        return None

    def param_list(self, timeout: Optional[float] = None) -> List[str]:
        """`param` — list registered parameter names."""
        text = self.send_command('param', timeout=timeout)
        return self._parse_list(text)

    def param_get(self, name: str, timeout: Optional[float] = None) -> Optional[int]:
        """`param <name>` — read one parameter. None if unavailable."""
        if not name:
            return None
        text = self.send_command('param %s' % name, timeout=timeout)
        return self._parse_get(text, name)

    def param_set(self, name: str, value: Any,
                  timeout: Optional[float] = None,
                  verify: bool = True) -> bool:
        """
        `param <name> <value>` — write one parameter.

        Firmware prints nothing on success; when verify=True the client
        re-reads the parameter and compares. Returns False if the UART
        write fails or verification fails.
        """
        if not name:
            return False
        # accept int/str; shell uses atoi()
        if isinstance(value, bool):
            value = 1 if value else 0
        text_value = str(value).strip()
        if text_value == '':
            return False
        n = self.write('param %s %s' % (name, text_value))
        if n is None or n < 0:
            return False
        # drain any immediate echo / log
        self.read_response(timeout=0.2 if timeout is None else min(0.2, timeout),
                           idle_gap=0.03)
        if not verify:
            return True
        try:
            expect = int(text_value)
        except ValueError:
            # non-numeric: cannot verify via atoi round-trip; treat write as ok
            return True
        got = self.param_get(name, timeout=timeout)
        return got == expect

    # ------------------------------------------------------------------
    # status
    # ------------------------------------------------------------------

    @property
    def last_response(self) -> str:
        return self._rx_text

    @property
    def raw_log(self) -> List[bytes]:
        return list(self._raw_log)

    def status(self) -> dict:
        return {
            'line_ending': self._line_ending,
            'default_timeout': self._default_timeout,
            'commands_sent': len(self._raw_log),
        }
