; NexusLink Windows Installer (NSIS)
; Build from project root after dist/ contains NexusLink.exe and data/

!include "MUI2.nsh"

!define APP_NAME "NexusLink"
!define APP_EXE "NexusLink.exe"
!define APP_VERSION "1.0.0"
!define APP_PUBLISHER "NexusLink"

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

Name "${APP_NAME} ${APP_VERSION}"
OutFile "NexusLink-Setup-${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES64\NexusLink"
InstallDirRegKey HKLM "Software\NexusLink" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch NexusLink"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Function .onInit
    IfFileExists "dist\${APP_EXE}" +3 0
    MessageBox MB_ICONSTOP "Missing dist\${APP_EXE}. Build and package dist before running installer build."
    Abort
FunctionEnd

Section "Install"
    SetShellVarContext all
    SetOutPath "$INSTDIR"

    ; Package prebuilt distribution payload
    File /r "dist\*.*"

    ; Ensure required runtime folders exist even if missing in payload
    CreateDirectory "$INSTDIR\data"
    CreateDirectory "$INSTDIR\data\downloads"
    CreateDirectory "$INSTDIR\data\uploads"
    CreateDirectory "$INSTDIR\data\logs"

    ; Shortcuts
    CreateDirectory "$SMPROGRAMS\NexusLink"
    CreateShortcut "$SMPROGRAMS\NexusLink\NexusLink.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut "$SMPROGRAMS\NexusLink\Uninstall NexusLink.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortcut "$DESKTOP\NexusLink.lnk" "$INSTDIR\${APP_EXE}"

    ; Uninstaller registration
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\NexusLink" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "DisplayName" "NexusLink"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink" "NoRepair" 1
SectionEnd

Section "Uninstall"
    SetShellVarContext all
    Delete "$DESKTOP\NexusLink.lnk"

    Delete "$SMPROGRAMS\NexusLink\NexusLink.lnk"
    Delete "$SMPROGRAMS\NexusLink\Uninstall NexusLink.lnk"
    RMDir "$SMPROGRAMS\NexusLink"

    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "Software\NexusLink"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusLink"
SectionEnd
