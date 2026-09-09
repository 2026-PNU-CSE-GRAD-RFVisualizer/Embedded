import os, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
env=os.environ.copy()
env.update(IDF_PATH='E:/esp/v5.5.2/esp-idf', IDF_TOOLS_PATH='E:/esp/.espressif', IDF_PYTHON_ENV_PATH='E:\\esp\\.espressif\\python_env\\idf5.5_py3.11_env', IDF_COMPONENT_CACHE_PATH=str(root/'.codex-build/component-cache'), PYTHONUTF8='1', GIT_CONFIG_COUNT='2',GIT_CONFIG_KEY_0='safe.directory',GIT_CONFIG_VALUE_0=root.as_posix(),GIT_CONFIG_KEY_1='safe.directory',GIT_CONFIG_VALUE_1='E:/esp/v5.5.2/esp-idf')
env['PATH']=';'.join(['E:/esp/.espressif/python_env/idf5.5_py3.11_env/Scripts','E:/esp/.espressif/tools/ninja/1.12.1','E:/esp/.espressif/tools/cmake/3.30.2/bin','E:/esp/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin',env['PATH']])
sys.exit(subprocess.call([sys.executable,env['IDF_PATH']+'/tools/idf.py','-DCCACHE_ENABLE=0',*sys.argv[1:]],env=env))
