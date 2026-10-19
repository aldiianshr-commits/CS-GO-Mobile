#!/bin/bash
# Uji asap di emulator Android (dijalankan oleh CI). Menghasilkan folder smoke/.
set -x
PKG=com.aldiianshr.csgo
mkdir -p smoke
adb install -r -g dist/CSGO-Mobile.apk > smoke/install.txt 2>&1
adb shell appops set $PKG MANAGE_EXTERNAL_STORAGE allow
adb shell mkdir -p /sdcard/CSGO
adb push testgame/. /sdcard/CSGO/ > /dev/null
adb shell getprop ro.product.cpu.abilist > smoke/abi.txt
adb shell dumpsys package $PKG | grep -iE "primaryCpuAbi|versionName" >> smoke/abi.txt
adb shell pm list features | grep -i vulkan > smoke/vulkan_features.txt
adb logcat -c
adb shell am start -W -n $PKG/.MainActivity > smoke/start_main.txt 2>&1
sleep 12
adb exec-out screencap -p > smoke/main.png
adb shell am start -W -n $PKG/.MainActivity --es autostart de_test > smoke/start_game.txt 2>&1
sleep 30
adb exec-out screencap -p > smoke/game.png
# gerak maju & geser kamera lewat input sentuh
adb shell input swipe 1500 500 1300 450 600
sleep 3
adb exec-out screencap -p > smoke/game2.png
adb shell pidof $PKG > smoke/pid.txt
adb logcat -d > smoke/logcat_full.txt
grep -E "CSGO|AndroidRuntime|FATAL|DEBUG|libc|vulkan|Vulkan|adrenotools" smoke/logcat_full.txt | head -400 > smoke/logcat.txt
adb pull /sdcard/CSGO/logs/last.log smoke/last.log
adb shell ls -la /sdcard/CSGO /sdcard/CSGO/saves /sdcard/CSGO/cache > smoke/files.txt 2>&1
true
