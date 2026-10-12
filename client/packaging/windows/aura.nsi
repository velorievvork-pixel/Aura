; Установщик Aura для Windows (NSIS). Собирается в CI после windeployqt:
;   makensis client\packaging\windows\aura.nsi
; Ставит программу в профиль пользователя (права администратора не нужны),
; создаёт ярлыки в «Пуске» и на рабочем столе, регистрирует удаление.
Unicode true
!define APP "Aura"
!define DIST "..\..\..\dist"
!define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\Aura"

Name "${APP}"
OutFile "..\..\..\Aura-Setup.exe"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\Aura"
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Aura"
  SetOutPath "$INSTDIR"
  File /r "${DIST}\*.*"
  CreateDirectory "$SMPROGRAMS\Aura"
  CreateShortcut "$SMPROGRAMS\Aura\Aura.lnk" "$INSTDIR\aura-client.exe"
  CreateShortcut "$DESKTOP\Aura.lnk" "$INSTDIR\aura-client.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UNINST}" "DisplayName" "Aura"
  WriteRegStr HKCU "${UNINST}" "Publisher" "Aura"
  WriteRegStr HKCU "${UNINST}" "DisplayIcon" "$INSTDIR\aura-client.exe"
  WriteRegStr HKCU "${UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Aura\Aura.lnk"
  RMDir "$SMPROGRAMS\Aura"
  Delete "$DESKTOP\Aura.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINST}"
SectionEnd
