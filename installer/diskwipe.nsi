; diskwipe Installer – wird von "make dist" gebaut.
; Erwartet: -DVERSION=x.y.z -DEXE=<Pfad zu diskwipe.exe> -DOUTFILE=<Pfad zum Setup> -DICON=<Pfad zu diskwipe.ico>
Unicode true

!ifndef VERSION
  !error "VERSION fehlt (-DVERSION=x.y.z)"
!endif
!ifndef EXE
  !error "EXE fehlt (-DEXE=...)"
!endif
!ifndef OUTFILE
  !error "OUTFILE fehlt (-DOUTFILE=...)"
!endif

!ifndef ICON
  !error "ICON fehlt (-DICON=...)"
!endif

!define APP "diskwipe"
!define PUBLISHER "timyyboyyy"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APP} ${VERSION}"

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1031 "ProductName" "${APP}"
VIAddVersionKey /LANG=1031 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=1031 "FileDescription" "${APP} Installer"
VIAddVersionKey /LANG=1031 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1031 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1031 "LegalCopyright" "${PUBLISHER}"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\diskwipe.exe"
!define MUI_FINISHPAGE_RUN_TEXT "diskwipe jetzt starten"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "German"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "diskwipe benötigt ein 64-Bit-Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "diskwipe (erforderlich)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "/oname=diskwipe.exe" "${EXE}"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\diskwipe.lnk" "$INSTDIR\diskwipe.exe" "" "$INSTDIR\diskwipe.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP}\diskwipe deinstallieren.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\diskwipe.exe" 0

  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\diskwipe.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section /o "Desktop-Verknüpfung" SecDesktop
  CreateShortcut "$DESKTOP\diskwipe.lnk" "$INSTDIR\diskwipe.exe" "" "$INSTDIR\diskwipe.exe" 0
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\diskwipe.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP}\diskwipe.lnk"
  Delete "$SMPROGRAMS\${APP}\diskwipe deinstallieren.lnk"
  RMDir "$SMPROGRAMS\${APP}"
  Delete "$DESKTOP\diskwipe.lnk"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
