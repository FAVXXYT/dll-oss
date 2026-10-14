@echo off

taskkill /IM Minecraft.Windows.exe /F /FI "STATUS eq RUNNING"
taskkill /IM FAVIO.Launcher.exe /F /FI "STATUS eq RUNNING"

echo "Killed Minecraft and FAVIO Launcher"
