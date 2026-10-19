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
adb shell am force-stop $PKG
sleep 2
for P in 2 4 0; do
  adb shell am force-stop $PKG
  sleep 2
  adb shell am start -W -n $PKG/.MainActivity --es autostart de_test --ei preset $P > smoke/start_game_$P.txt 2>&1
  sleep 30
  adb exec-out screencap -p > smoke/game_preset$P.png
  adb pull /sdcard/CSGO/logs/last.log smoke/last_preset$P.log
  if [ "$P" = "2" ]; then
    # geser kamera & tahan joystick maju
    adb shell input swipe 1500 500 1300 450 600
    adb shell input swipe 300 900 300 700 3000
    sleep 2
    adb exec-out screencap -p > smoke/game_moved.png
    # simpan posisi lewat tombol SIMPAN (posisi relatif 0.86, 0.08)
    adb shell input tap 1651 96
    sleep 2
  fi
done
adb shell pidof $PKG > smoke/pid.txt
adb logcat -d > smoke/logcat_full.txt
grep -E "CSGO|AndroidRuntime|FATAL|DEBUG|libc|vulkan|Vulkan|adrenotools" smoke/logcat_full.txt | head -400 > smoke/logcat.txt
adb shell ls -la /sdcard/CSGO /sdcard/CSGO/saves /sdcard/CSGO/cache > smoke/files.txt 2>&1
true
