"""Tests for xmodem_ydmodem.py — standard XMODEM-128 / YMODEM-1K protocol."""
import xmodem_ydmodem as xm
from xmodem_ydmodem import XmodemSender, YmodemSender, SOH, STX, EOT, ACK, NAK, CRCPKT

def make_data(n):
    return bytes([i % 256 for i in range(n)])

all_pass = True

# ============================================================
# Protocol-correct test structure:
#
# XMODEM: trigger(NAK/'C') sends block 1.
#   Each iteration: ACK drives to next frame or DONE. CRCPKT (if added)
#   is called AFTER the ACK in a separate on_uart_byte call, so each
#   triggers exactly one frame send.
#
# YMODEM: trigger('C') sends block 0. ACK transitions to data mode and
#   sends block 1 SYNCHRONOUSLY. Each subsequent ACK sends the next frame.
#   CRCPKT is NOT called between ACK calls (ACK drives all frame sends).
#
# Frame counts (standard: SOH padding only when file NOT an exact multiple):
#   XMODEM  128B → 2 frames (block1 + EOT, 1 block, exact mult)
#   XMODEM  256B → 4 frames (block1 + block2 + block2_CRC + EOT, 2 blocks)
#   XMODEM  300B → 5 frames (block1 + block2 + block3 + SOHpad + EOT, 3 blocks)
#   XMODEM 1024B → 10 frames (block1-8 + block8_CRC + EOT, 8 blocks)
#   YMODEM  512B → 4 frames (block0 + STX(512B+padded) + SOHpad + EOT)
#   YMODEM 1024B → 3 frames (block0 + STX(1024B) + EOT)
#   YMODEM 2048B → 4 frames (block0 + STX*2 + EOT)
#   YMODEM 3000B → 6 frames (block0 + STX*3 + SOHpad + EOT)
# ============================================================

def test_xmodem(data, mode, expect_frames, desc=''):
    """Standard XMODEM test: trigger + ACK-driven loop (no CRCPKT between ACKs)."""
    global all_pass
    tx_log = []
    def uart_send(d): tx_log.append(bytes(d))
    s = XmodemSender(uart_send, lambda *_: None)
    s.start(data)
    trigger = NAK if mode == 'checksum' else CRCPKT
    s.on_uart_byte(trigger)   # sends block 1

    while True:
        s.on_uart_byte(ACK)   # ACK sends next frame or sets DONE
        if s._state == xm.XferState.DONE:
            break
        if len(tx_log) > expect_frames + 3:
            print(f'FAIL {desc}: >{expect_frames} frames')
            for i, b in enumerate(tx_log):
                print(f'  [{i}] SOH={b[0]} num={b[1] if len(b)>1 else "?"} len={len(b)}')
            all_pass = False
            return

    frames = tx_log
    ok = True
    if not frames or frames[-1][0] != EOT:
        print(f'FAIL {desc}: last frame not EOT'); ok = False
    if len(frames) != expect_frames:
        print(f'FAIL {desc}: {len(frames)} frames, expected {expect_frames}'); ok = False
    if ok:
        print(f'PASS {desc}  ({len(frames)} frames)')
    else:
        all_pass = False

# XMODEM: ACK-only loop (CRCPKT not used between ACKs in standard transfer)
test_xmodem(make_data(128),  'checksum', 2,  'XMODEM 128B  checksum  (1 block, exact mult)')
test_xmodem(make_data(128),  'crc',      2,  'XMODEM 128B  CRC16     (1 block, exact mult)')
test_xmodem(make_data(256),  'checksum', 3,  'XMODEM 256B  checksum  (2 blocks, exact mult)')
test_xmodem(make_data(256),  'crc',      3,  'XMODEM 256B  CRC16     (2 blocks, exact mult)')
test_xmodem(make_data(300),  'checksum', 5,  'XMODEM 300B  checksum  (3 blocks, partial)')
test_xmodem(make_data(300),  'crc',      5,  'XMODEM 300B  CRC16     (3 blocks, partial)')
test_xmodem(make_data(1024), 'checksum', 9,  'XMODEM 1024B checksum  (8 blocks, exact mult)')
test_xmodem(make_data(1024), 'crc',      9,  'XMODEM 1024B CRC16     (8 blocks, exact mult)')


def test_ymodem(data, filename, expect_frames, desc=''):
    """YMODEM test: trigger 'C' + ACK-driven loop (no CRCPKT between ACKs)."""
    global all_pass
    tx_log = []
    def uart_send(d): tx_log.append(bytes(d))
    s = YmodemSender(uart_send, lambda *_: None)
    s.start(data, filename)
    s.on_uart_byte(CRCPKT)   # trigger block 0

    while True:
        s.on_uart_byte(ACK)  # ACK sends next frame or sets DONE after EOT
        if s._state == xm.XferState.DONE:
            break
        if len(tx_log) > expect_frames + 3:
            print(f'FAIL {desc}: >{expect_frames} frames')
            for i, b in enumerate(tx_log):
                t = 'SOH' if b[0]==SOH else 'STX' if b[0]==STX else 'EOT'
                print(f'  [{i}] {t} num={b[1]} len={len(b)}')
            all_pass = False
            return

    frames = tx_log
    ok = True
    if not frames or frames[-1][0] != EOT:
        print(f'FAIL {desc}: last frame not EOT'); ok = False
    if frames[0][0] != SOH or frames[0][1] != 0:
        print(f'FAIL {desc}: frame 0 should be SOH block 0'); ok = False
    if len(frames) != expect_frames:
        print(f'FAIL {desc}: {len(frames)} frames, expected {expect_frames}'); ok = False
    if ok:
        print(f'PASS {desc}  ({len(frames)} frames)')
    else:
        all_pass = False

# YMODEM: ACK-only loop
test_ymodem(make_data(512),  'f512.bin',  4, 'YMODEM 512B  (1 block, partial -> SOH pad)')
test_ymodem(make_data(1024), 'f1024.bin', 3, 'YMODEM 1024B (1 block, exact mult)')
test_ymodem(make_data(2048), 'f2048.bin', 4, 'YMODEM 2048B (2 blocks, exact mult)')
test_ymodem(make_data(3000), 'f3k.bin',   6, 'YMODEM 3000B (3 blocks, partial -> SOH pad)')


# ============================================================
# Frame byte-count structural checks
# ============================================================
print()
print('--- Frame byte-count structural checks ---')

tx_log = []
s = XmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(128)); s.on_uart_byte(NAK)
if len(tx_log[0]) == 132:
    print(f'PASS XMODEM checksum frame = {len(tx_log[0])} B  (SOH+blk+~blk+128B+1Bchk)')
else:
    print(f'FAIL XMODEM checksum: {len(tx_log[0])} B, expected 132'); all_pass = False

tx_log = []
s = XmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(128)); s.on_uart_byte(CRCPKT)
if len(tx_log[0]) == 133:
    print(f'PASS XMODEM CRC frame      = {len(tx_log[0])} B  (SOH+blk+~blk+128B+2B)')
else:
    print(f'FAIL XMODEM CRC: {len(tx_log[0])} B, expected 133'); all_pass = False

tx_log = []
s = YmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(1024), 't.bin'); s.on_uart_byte(CRCPKT)
if len(tx_log[0]) == 133:
    print(f'PASS YMODEM block0         = {len(tx_log[0])} B  (SOH+0+FF+128B+CRC16)')
else:
    print(f'FAIL YMODEM block0: {len(tx_log[0])} B, expected 133'); all_pass = False

s.on_uart_byte(ACK)  # sends block 1
if len(tx_log) > 1 and len(tx_log[1]) == 1029:
    print(f'PASS YMODEM data block     = {len(tx_log[1])} B  (STX+blk+~blk+1024B+CRC16)')
else:
    print(f'FAIL YMODEM data block: got {[len(f) for f in tx_log]}, expected [..., 1029]'); all_pass = False

tx_log = []
s = YmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(512), 'p.bin'); s.on_uart_byte(CRCPKT)
for _ in range(20):
    s.on_uart_byte(ACK)
    if s._state == xm.XferState.DONE: break
soh_frames = [f for f in tx_log if f[0] == SOH and f[1] > 0]
if soh_frames:
    pad = soh_frames[-1]
    if len(pad) == 133 and pad[3:131] == b'\x00'*128:
        print(f'PASS YMODEM SOH pad block = {len(pad)} B  (SOH+blk+~blk+128B+CRC16, all zeros)')
    else:
        print(f'FAIL YMODEM SOH pad: len={len(pad)}, zeros={pad[3:131]==bytes(128)}'); all_pass = False


# ============================================================
# NAK retry mid-transfer (XMODEM)
# ============================================================
print()
print('--- NAK retry mid-transfer (XMODEM) ---')
tx_log = []
s = XmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(300))
s.on_uart_byte(NAK)                     # sends block 1
s.on_uart_byte(ACK)                     # sends block 2
blk2_sent = bytes(tx_log[-1])
blk2_num = tx_log[-1][1]
s.on_uart_byte(NAK)                     # NAK block 2 -> resend block 2
blk2_resent = bytes(tx_log[-1])
resent_num = tx_log[-1][1]
if blk2_resent == blk2_sent and resent_num == blk2_num:
    print(f'PASS NAK retry: block {blk2_num} resent byte-identically')
else:
    print(f'FAIL NAK retry: num={resent_num} vs {blk2_num}, data match={blk2_resent==blk2_sent}'); all_pass = False
s.on_uart_byte(ACK); s.on_uart_byte(ACK); s.on_uart_byte(ACK)  # wrap up


# ============================================================
# NAK -> CRC mid-transfer switch (XMODEM)
# Sequence: trigger(NAK) -> block1 | ACK -> block2 | CRCPKT -> block2_CRC | ACK -> block3 | ACK -> EOT
# CRCPKT is called as a SEPARATE on_uart_byte call (after the ACK that sent block2).
# This means: ACK sends block2, then CRCPKT switches to CRC and resends block2.
# ============================================================
print()
print('--- NAK->CRC mode switch (XMODEM) ---')
tx_log = []
s = XmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(256)); s.on_uart_byte(NAK)   # checksum mode
s.on_uart_byte(ACK)    # sends block 2 (checksum, 132 bytes)
frame_before = bytes(tx_log[-1])
len_before = len(frame_before)
# CRCPKT in a SEPARATE on_uart_byte call after ACK
s.on_uart_byte(CRCPKT)  # 'C': switch to CRC and resend block 2
frame_after = bytes(tx_log[-1])
len_after = len(frame_after)
if len_before == 132 and len_after == 133:
    print(f'PASS NAK->CRC switch: {len_before}B -> {len_after}B (checksum -> CRC16)')
else:
    print(f'FAIL NAK->CRC: {len_before}B -> {len_after}B (expected 132 -> 133)'); all_pass = False
s.on_uart_byte(ACK); s.on_uart_byte(ACK)  # block3 + EOT, then wrap up


# ============================================================
# YMODEM block0 NAK retry
# ============================================================
print()
print('--- YMODEM block0 NAK retry ---')
tx_log = []
s = YmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(1024), 't.bin'); s.on_uart_byte(CRCPKT)
blk0_sent = bytes(tx_log[-1])
s.on_uart_byte(NAK)   # NAK on block0 -> resend block0
blk0_resent = bytes(tx_log[-1])
if blk0_resent == blk0_sent:
    print(f'PASS YMODEM block0 NAK: resent byte-identically')
else:
    print(f'FAIL YMODEM block0 NAK: frames differ'); all_pass = False
s.on_uart_byte(ACK); s.on_uart_byte(CRCPKT); s.on_uart_byte(ACK); s.on_uart_byte(ACK)


# ============================================================
# Block number sequence (XMODEM 300B)
#  trigger -> block1 | ACK1 -> block2 | ACK2 -> block3 | ACK3 -> SOHpad(3) | ACK4 -> EOT
# SOH frames: [1, 2, 3, 3(pad)]
# ============================================================
print()
print('--- Block number sequence (XMODEM 300B) ---')
tx_log = []
s = XmodemSender(lambda d: tx_log.append(bytes(d)), lambda *_: None)
s.start(make_data(300)); s.on_uart_byte(NAK)
while True:
    s.on_uart_byte(ACK)
    if s._state == xm.XferState.DONE: break
soh_frames = [f for f in tx_log if f[0] == SOH and f[1] > 0]
n = [f[1] for f in soh_frames]
expected = [1, 2, 3, 4]  # 3 data + SOH pad(block 4)
if n == expected:
    print(f'PASS block sequence: {n}  (1,2,3 data + 3 padding)')
else:
    print(f'FAIL block sequence: {n}, expected {expected}'); all_pass = False


print()
print('ALL TESTS PASSED' if all_pass else 'SOME TESTS FAILED')
