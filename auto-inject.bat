@echo off

set launcher_path="%USERPROFILE%\Documents\Minecraft\FAVIO\FAVIO.Launcher.exe"

if exist %launcher_path% (
    echo "Launched!"
    %launcher_path% --inject "%~1"
    echo "Injected!"
) else (
    echo "Launcher not found."
)
