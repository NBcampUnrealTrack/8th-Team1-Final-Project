import sys
import time

sys.path.insert(0, r"E:\Unreal\MarineDemolition\Saved")
from remote_execution import RemoteExecution, RemoteExecutionConfig, MODE_EXEC_FILE

cfg = RemoteExecutionConfig()
# Match DefaultEditor.ini bind (0.0.0.0); client default 127.0.0.1 often fails discovery
cfg.multicast_bind_address = "0.0.0.0"
cfg.multicast_group_endpoint = ("239.0.0.1", 6766)
cfg.multicast_ttl = 0

re = RemoteExecution(cfg)
re.start()
for i in range(40):
    if re.remote_nodes:
        break
    time.sleep(0.5)
    if i % 4 == 0:
        print("waiting nodes...", i, "bind", cfg.multicast_bind_address)
if not re.remote_nodes:
    # retry 127.0.0.1
    re.stop()
    cfg.multicast_bind_address = "127.0.0.1"
    re = RemoteExecution(cfg)
    re.start()
    for i in range(20):
        if re.remote_nodes:
            break
        time.sleep(0.5)
if not re.remote_nodes:
    print("NO_NODES")
    re.stop()
    raise SystemExit(2)

print("node", re.remote_nodes[0])
re.open_command_connection(re.remote_nodes[0]["node_id"])
r = re.run_command(
    r"E:/Unreal/MarineDemolition/Content/Python/apply_mats_to_bp_sky.py",
    exec_mode=MODE_EXEC_FILE,
    unattended=True,
)
for o in r.get("output", []):
    print(str(o.get("output", ""))[:600])
print("success", r.get("success"))
if not r.get("success"):
    print(str(r.get("result", ""))[:2500])
re.stop()
