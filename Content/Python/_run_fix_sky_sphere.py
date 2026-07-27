import sys
import time
from pathlib import Path

sys.path.insert(0, r"E:\Unreal\MarineDemolition\Saved")
from remote_execution import MODE_EXEC_FILE, RemoteExecution

CODE = Path(r"E:\Unreal\MarineDemolition\Content\Python\fix_sky_sphere_and_cloud_dither.py").read_text(
    encoding="utf-8"
)
re = RemoteExecution()
re.start()
deadline = time.time() + 8
while time.time() < deadline and not re.remote_nodes:
    time.sleep(0.25)
if not re.remote_nodes:
    print("NO_NODES")
    re.stop()
    raise SystemExit(2)
re.open_command_connection(re.remote_nodes[0]["node_id"])
print(re.run_command(CODE, exec_mode=MODE_EXEC_FILE))
re.stop()
print("DONE")
