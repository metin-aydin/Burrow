; Burrow's Windows installer (ADR 0010), shaped like JumpDesk's electron-builder "one-click" NSIS installer:
; per-user (no administrator rights), always %LOCALAPPDATA%\Programs\Burrow, Start menu and desktop shortcuts,
; an uninstaller, and Burrow starts when it finishes. The vault and settings (%APPDATA%\Burrow) are never touched.
;
; makensis /DVERSION=X.Y.Z /DSTAGE=<folder with Burrow.exe and its files> /DICON=<Burrow.ico> /DOUTFILE=<setup.exe> burrow.nsi
;
; In-app updates run it as: setup.exe /S /RELAUNCH /WAITPID=<pid of the running Burrow>

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !error "VERSION is required"
!endif
!define APP "Burrow"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
Icon "${ICON}"
UninstallIcon "${ICON}"
BrandingText "${APP} ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow
AutoCloseWindow true

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT License"

Page instfiles
UninstPage instfiles

Function .onInit
  ; An update waits until the Burrow that started it has quit (its files are in use until then).
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/WAITPID=" $1
  ${IfNot} ${Errors}
    System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $1) p .r2'   ; SYNCHRONIZE
    ${If} $2 P<> 0
      System::Call 'kernel32::WaitForSingleObject(p r2, i 60000)'
      System::Call 'kernel32::CloseHandle(p r2)'
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Install"
  ; The folder is Burrow's own (never chosen by the user), so an update replaces it whole and no file of an
  ; older version lingers.
  RMDir /r "$INSTDIR"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\Burrow.exe"
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\Burrow.exe"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "metin-aydin"
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/metin-aydin/Burrow"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Burrow.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"

  ; Start Burrow when done: after an interactive install, and after an update that asked for it.
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/RELAUNCH" $1
  ${If} ${Silent}
    ${IfNot} ${Errors}
      Exec '"$INSTDIR\Burrow.exe"'
    ${EndIf}
  ${Else}
    Exec '"$INSTDIR\Burrow.exe"'
  ${EndIf}
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
