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

Response framing (important — see test_ui_product_e2e / COM4 field bug):
  nr_micro_shell prints the prompt ("nr@bos:" by default, Kconfig
  NR_SHELL_USER_NAME) after every command.  The prompt is the natural
  end-of-response marker.  It often arrives split across UART chunks
  ("nr@bos" | ":"), and a slow b_log loop can pause >50ms mid-response,
  so an idle-gap-only reader truncates the reply and parses garbage while
  the serial journal (independent path) shows the device replied fine.

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

    #: Kconfig NR_SHELL_USER_NAME default (bos/thirdparty/Kconfig)
    DEFAULT_PROMPT = 'nr@bos:'

    def __init__(self, uart: Any, line_ending: str = '\r',
                 default_timeout: float = 1.0,
                 prompt: Optional[str] = None) -> None:
        if uart is None:
            raise ValueError('uart is required')
        self._uart = uart
        self._line_ending = line_ending
        self._default_timeout = float(default_timeout)
        # None → framework default prompt; '' disables prompt termination
        self._prompt = self.DEFAULT_PROMPT if prompt is None else str(prompt)
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
                      idle_gap: float = 0.15,
                      poll_interval: float = 0.01,
                      end_marker: Optional[str] = None) -> str:
        """
        Collect shell output until `end_marker` (shell prompt) is seen,
        `timeout` expires, or — when no `end_marker` is given — an idle gap
        of `idle_gap` seconds passes after the last received byte.

        nr_micro_shell ends EVERY command with its prompt, so when the
        prompt is known it is the only reliable end of response: an
        idle-gap stop can cut the reply mid-token ("param_t" | "est_1")
        whenever the firmware pauses between b_log lines.  The prompt may
        itself arrive split ("nr@bos" | ":"), which is why only the full
        marker ends the read.

        Returns decoded text (utf-8, errors=replace). Empty string on
        timeout with no data.
        """
        if timeout is None:
            timeout = self._default_timeout
        deadline = time.time() + max(0.0, float(timeout))
        chunks: List[bytes] = []
        last_rx = 0.0
        got_any = False
        text = ''

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
                if end_marker:
                    text = b''.join(chunks).decode('utf-8', errors='replace')
                    if end_marker in text:
                        break
                # keep draining while data flows
                continue
            if not end_marker and got_any and (time.time() - last_rx) >= idle_gap:
                break
            time.sleep(poll_interval)

        if not text and chunks:
            text = b''.join(chunks).decode('utf-8', errors='replace')
        self._rx_text = text
        return text

    def send_command(self, cmd: str, timeout: Optional[float] = None,
                     idle_gap: float = 0.15) -> str:
        """
        Write cmd + line_ending, then collect the response text.

        Pending RX bytes are discarded first: the previous exchange's
        prompt straggler (e.g. the trailing ':' of a split "nr@bos:") would
        otherwise be consumed as this command's response and cut it short —
        the serial journal still shows the device's real reply, so the UI
        looks like it "failed to parse a correct response".
        """
        self.clear_rx()
        self.write(cmd)
        return self.read_response(timeout=timeout, idle_gap=idle_gap,
                                  end_marker=self._prompt or None)

    @staticmethod
    def _strip_prompt(text: str, prompt: str) -> str:
        """Remove prompt occurrences so a glued 'name:123nr@bos:' parses."""
        if prompt and prompt in text:
            return text.replace(prompt, '')
        return text

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
        """Parse ': name' lines produced by `param` with no args.

        Firmware format (b_mod_param.c): b_log(": %s\\r\\n", name) — a
        colon, a space, then the name.  Requiring the space rejects
        straggler-glued garbage such as ':param' (a leftover ':' byte
        glued onto the echoed command line).
        """
        names: List[str] = []
        for raw_line in text.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
            line = raw_line.strip()
            if not line.startswith(':'):
                continue
            body = line[1:]
            if not body[:1].isspace():
                continue
            name = body.strip()
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
        return self._parse_list(self._strip_prompt(text, self._prompt))

    def param_get(self, name: str, timeout: Optional[float] = None) -> Optional[int]:
        """`param <name>` — read one parameter. None if unavailable."""
        if not name:
            return None
        text = self.send_command('param %s' % name, timeout=timeout)
        return self._parse_get(self._strip_prompt(text, self._prompt), name)

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
        self.clear_rx()
        n = self.write('param %s %s' % (name, text_value))
        if n is None or n < 0:
            return False
        # drain echo + trailing prompt (firmware prints no result line)
        self.read_response(timeout=0.2 if timeout is None else min(0.2, timeout),
                           idle_gap=0.03,
                           end_marker=self._prompt or None)
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
    def prompt(self) -> str:
        return self._prompt

    @property
    def raw_log(self) -> List[bytes]:
        return list(self._raw_log)

    def status(self) -> dict:
        return {
            'line_ending': self._line_ending,
            'default_timeout': self._default_timeout,
            'commands_sent': len(self._raw_log),
            'prompt': self._prompt,
        }
