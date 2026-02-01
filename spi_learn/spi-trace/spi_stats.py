import re
from collections import defaultdict

with open("spi_trace_len30.log") as f:
    lines = f.readlines()

transfers = []
messages = []
controller = []
last_transfer_stop = None
last_message_done = None

for line in lines:
    # 提取时间戳
    ts_match = re.search(r"\s\[\d+\]\s+(\d+\.\d+):",line)
    if not ts_match:
        continue
    timestamp = float(ts_match.group(1))

    # transfer_start
    if "spi_transfer_start" in line:
        dev_match = re.search(r"spi\d+\.\d+", line)
        dev = dev_match.group() if dev_match else "spi?"
        len_match = re.search(r"len=(\d+)", line)
        length = int(len_match.group(1)) if len_match else 0
        transfers.append({"dev": dev, "start": timestamp, "len": length})

    # transfer_stop
    elif "spi_transfer_stop" in line:
        if transfers:
            transfers[-1]["stop"] = timestamp
            duration = timestamp - transfers[-1]["start"]
            transfers[-1]["duration_us"] = duration * 1e6
            if duration > 0:
                transfers[-1]["speed_bps"] = (transfers[-1]["len"] * 8) / duration
            if last_transfer_stop:
                transfers[-1]["inter_transfer_gap_us"] = (transfers[-1]["start"] - last_transfer_stop) * 1e6
            last_transfer_stop = timestamp

    # message_start
    elif "spi_message_start" in line:
        messages.append({"start": timestamp})

    # message_done
    elif "spi_message_done" in line:
        if messages:
            messages[-1]["stop"] = timestamp
            messages[-1]["duration_us"] = (timestamp - messages[-1]["start"]) * 1e6
            if last_message_done:
                messages[-1]["cs_hold_gap_us"] = (messages[-1]["start"] - last_message_done) * 1e6
            last_message_done = timestamp

    # controller busy
    elif "spi_controller_busy" in line:
        controller.append({"busy": timestamp})

    # controller idle
    elif "spi_controller_idle" in line:
        if controller and "busy" in controller[-1]:
            controller[-1]["idle"] = timestamp
            controller[-1]["busy_duration_us"] = (timestamp - controller[-1]["busy"]) * 1e6

# 输出统计结果
print("\n=== Transfer Stats ===")
for t in transfers:
    print(f"{t['dev']} len={t['len']} duration={t.get('duration_us',0):.1f}us speed={t.get('speed_bps',0):.1f}bps gap={t.get('inter_transfer_gap_us',0):.1f}us")

print("\n=== Message Stats ===")
for m in messages:
    print(f"duration={m.get('duration_us',0):.1f}us CS_hold_gap={m.get('cs_hold_gap_us',0):.1f}us")

print("\n=== Controller Busy Periods ===")
for c in controller:
    print(f"busy_duration={c.get('busy_duration_us',0):.1f}us")

