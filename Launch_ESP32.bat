@echo off
:: ====================================================================
:: ESP32 日本語ユーザー名コンパイルバグ回避用 起動スクリプト
:: ====================================================================

:: 既存のArduino IDEが裏で動いていると環境変数が反映されないため強制終了します
taskkill /F /IM "Arduino IDE.exe" >nul 2>&1
timeout /t 2 /nobreak >nul

:: キャッシュ・ビルド先フォルダをASCII文字のみのパスに偽装します
set LOCALAPPDATA=C:\Users\Public\ArduinoLocal

:: プロジェクトを安全な場所（C:\ArduinoProjects）から開きます
start "" "%USERPROFILE%\AppData\Local\Programs\Arduino IDE\Arduino IDE.exe" "C:\ArduinoProjects\sketch_jul14a\ESP32_WiFi\ESP32_WiFi.ino"
