from pathlib import Path
import json,re,subprocess,sys
root=Path(__file__).resolve().parents[1]
project=root/'handheld_jpeg_stream'
commands=json.loads((project/'build/compile_commands.json').read_text())
base=(project/'build/config/sdkconfig.h').read_text(encoding='utf-8')
variants={
 'legacy_serial': {'HANDHELD_NEW_JPEG':None,'HANDHELD_LCD_PING_PONG':None,'HANDHELD_LCD_STRIPE_ROWS':32},
 'benchmark_imu': {'HANDHELD_RGB565_BENCHMARK':1,'HANDHELD_BNO085_SERVICE':1},
 'control_udp': {'HANDHELD_CONTROL_UDP':1,'HANDHELD_CONTROL_BACKEND_HOST':'""','HANDHELD_CONTROL_BACKEND_PORT':9200,'HANDHELD_CONTROL_DEVICE_ID':1},
}
for variant,values in variants.items():
 dest=root/'.codex-build'/variant;dest.mkdir(exist_ok=True)
 text=base
 for key,value in values.items():
  text=re.sub(r'^#define CONFIG_'+key+r'\b[^\n]*\n','',text,flags=re.M)
  if value is not None:text+=f'\n#define CONFIG_{key} {value}\n'
 (dest/'sdkconfig.h').write_text(text,encoding='utf-8')
 for name in ['jpeg_lcd_sink.c','lcd_gpio_writer.c','app_main.c','jpeg_stream_client.c']:
  cmd=next(c for c in commands if Path(c['file']).name==name)
  command=cmd['command'].replace((project/'build/config').as_posix(),dest.as_posix())+' -fsyntax-only -Werror'
  result=subprocess.run(command,cwd=cmd['directory'],capture_output=True,text=True)
  if result.returncode:
   print(variant,name,result.stdout,result.stderr);sys.exit(result.returncode)
 print(variant+': 4 translation units passed (-Werror)')
