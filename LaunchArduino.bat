@echo off
:: ====================================================================
:: ESP32 日本語ユーザー名コンパイルバグ回避用 起動スクリプト
:: ====================================================================

:: キャッシュ・ビルド先フォルダをASCII文字のみのパスに偽装します
set LOCALAPPDATA=C:\Users\Public\ArduinoLocal

:: Arduino IDEを起動します
start "" "%USERPROFILE%\AppData\Local\Programs\Arduino IDE\Arduino IDE.exe"
