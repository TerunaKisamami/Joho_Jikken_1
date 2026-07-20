import re

path = r'c:\Users\Oojanaiga\Documents\Joho_Jikken_1\ArduinoUno_Main\ArduinoUno_Main.ino'
with open(path, 'r', encoding='utf-8') as f:
    code = f.read()

# Replace Y coordinates to fit in 32 pixels
code = code.replace('setCursor(0,15)', 'setCursor(0,8)')
code = code.replace('setCursor(0,20)', 'setCursor(0,12)')
code = code.replace('setCursor(0,30)', 'setCursor(0,16)')
code = code.replace('setCursor(0,35)', 'setCursor(0,18)')
code = code.replace('setCursor(0,40)', 'setCursor(0,20)')
code = code.replace('setCursor(0,45)', 'setCursor(0,24)')

with open(path, 'w', encoding='utf-8') as f:
    f.write(code)
print('Y coordinates optimized for 32px height.')
