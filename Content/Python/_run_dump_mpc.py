import sys
import time

sys.path.insert(0, r"E:\Unreal\MarineDemolition\Saved")
from remote_execution import RemoteExecution, MODE_EXEC_FILE

re = RemoteExecution()
re.start()
for _ in range(25):
    if re.remote_nodes:
        break
    time.sleep(0.3)
if not re.remote_nodes:
    print("NO_NODES")
    re.stop()
    raise SystemExit(2)
re.open_command_connection(re.remote_nodes[0]["node_id"])
r = re.run_command(
    r"E:/Unreal/MarineDemolition/Content/Python/_dump_sostylized_mpc.py",
    exec_mode=MODE_EXEC_FILE,
    unattended=True,
)
for o in r.get("output", []):
    print(str(o.get("output", ""))[:500])
print("success", r.get("success"))
re.stop()
