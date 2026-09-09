$env:IDF_PATH='E:/esp/v5.5.2/esp-idf'
$env:IDF_TOOLS_PATH='E:/esp/.espressif'
$env:IDF_PYTHON_ENV_PATH='E:/esp/.espressif/python_env/idf5.5_py3.11_env'
$env:IDF_COMPONENT_CACHE_PATH='E:/RFVisualizer_Workspace/Embedded/.codex-build/component-cache'
$env:PYTHONUTF8='1'
$env:GIT_CONFIG_COUNT='2'
$env:GIT_CONFIG_KEY_0='safe.directory'
$env:GIT_CONFIG_VALUE_0='E:/RFVisualizer_Workspace/Embedded'
$env:GIT_CONFIG_KEY_1='safe.directory'
$env:GIT_CONFIG_VALUE_1='E:/esp/v5.5.2/esp-idf'
$env:PATH='E:/esp/.espressif/python_env/idf5.5_py3.11_env/Scripts;E:/esp/.espressif/tools/cmake/3.30.2/bin;E:/esp/.espressif/tools/ninja/1.12.1;E:/esp/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin;'+$env:PATH
& "$env:IDF_PYTHON_ENV_PATH/Scripts/python.exe" "$env:IDF_PATH/tools/idf.py" -DCCACHE_ENABLE=0 @args
exit $LASTEXITCODE
