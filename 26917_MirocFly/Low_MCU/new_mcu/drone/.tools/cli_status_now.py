import sys
sys.path.insert(0, r'D:\RM\drone\.tools')
import esc4way_debug_probe2 as d
s, b = d.enter_cli()
out = d.cli(s, 'status', 4.0)
s.close()
print(out.decode('utf-8', 'replace'))
