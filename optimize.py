import re
import os

path = r'c:\Users\Oojanaiga\Documents\Joho_Jikken_1\ArduinoUno_Main\ArduinoUno_Main.ino'
with open(path, 'r', encoding='utf-8') as f:
    code = f.read()

# Replace SCREEN_HEIGHT back to 64
code = code.replace('#define SCREEN_HEIGHT 32', '#define SCREEN_HEIGHT 64')

# Reduce MAX_CARDS and MAX_PASS_LENGTH
code = code.replace('constexpr int MAX_CARDS = 20;', 'constexpr int MAX_CARDS = 10;')
code = code.replace('constexpr int MAX_PASS_LENGTH = 32;', 'constexpr int MAX_PASS_LENGTH = 16;')

# Add F() to display.print/println
code = re.sub(r'display\.(print(?:ln)?)\(\"([^\"]*)\"\)', r'display.\1(F("\2"))', code)

# Add F() to triggerError string literals (first and second args)
code = re.sub(r'triggerError\(\"([^\"]*)\",\s*\"([^\"]*)\",', r'triggerError(F("\1"), F("\2"),', code)

# Fix espSerial.println with strings
code = re.sub(r'espSerial\.println\(\"([^\"]*)\"\s*\+', r'espSerial.println(String(F("\1")) +', code)

with open(path, 'w', encoding='utf-8') as f:
    f.write(code)
print('Optimized memory usage.')
