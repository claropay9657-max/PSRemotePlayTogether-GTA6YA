; SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
;
; The Windows installer of PSRemotePlayTogether: the same folder the .zip holds, put where
; Windows keeps a user's own programs, with an entry in the Start menu and in the list of
; installed apps. scripts/ci/build-windows.sh builds it with makensis:
;
;   makensis -DVERSION=1.2.3 -DFILEVERSION=1.2.3.0 -DSOURCE=dist/PSRemotePlayTogether \
;            -DICON=gui/chiaking.ico -DOUTFILE=dist/PSRemotePlayTogether-1.2.3-windows-x64-setup.exe \
;            scripts/ci/windows-installer.nsi
;
; It installs for the user alone, into %LOCALAPPDATA%\Programs: no administrator is asked, and
; the app can still replace its own folder when it updates itself (gui/src/appupdater.cpp),
; which it could not under "Program Files". The uninstaller it leaves in the folder is carried
; over by the app on every such update.
;
; The app's folder is the app's alone: an update, a newer installer and the uninstaller each
; replace or remove it as a whole. So the installer only takes a folder that is new, empty, or
; already holds the app.

Unicode true
; A 64-bit installer for a 64-bit app. (It also lets the build try the installer with the
; 64-bit Wine it already has; NSIS' usual 32-bit one would need a second Wine.)
Target amd64-unicode
ManifestDPIAware true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!define APP "PSRemotePlayTogether"
!define EXE "PSRemotePlayTogether.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

!ifndef VERSION
  !error "say which version: -DVERSION=1.2.3"
!endif
!ifndef FILEVERSION
  !define FILEVERSION "0.0.0.0"
!endif
!ifndef SOURCE
  !error "say where the app's folder is: -DSOURCE=dist/PSRemotePlayTogether"
!endif
!ifndef OUTFILE
  !define OUTFILE "${APP}-${VERSION}-windows-x64-setup.exe"
!endif

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
; Where it was installed before, if it was
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP} ${VERSION}"

VIProductVersion "${FILEVERSION}"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "CompanyName" "GamerNova.net"
VIAddVersionKey "LegalCopyright" "AGPLv3. GamerNova.net; chiaki-ng by Street Pea; Chiaki by Florian Markl"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TEXT "This puts ${APP} ${VERSION} on this computer, for you alone: no administrator is needed, and the app can update itself later.$\r$\n$\r$\nClose ${APP} first if it is running."
!insertmacro MUI_PAGE_WELCOME

; The licence says what one may do with the app and its source code. Nobody has to agree to it to use the app.
!define MUI_LICENSEPAGE_TEXT_TOP "${APP} is free software under the GNU Affero General Public License:"
!define MUI_LICENSEPAGE_TEXT_BOTTOM "You do not have to agree to it to use the app. It says what you may do with the app and its source code."
!define MUI_LICENSEPAGE_BUTTON "$(^NextBtn)"
; (paths on the computer that builds this are written with /: makensis runs on Linux here)
!insertmacro MUI_PAGE_LICENSE "${SOURCE}/LICENSE.txt"

!define MUI_DIRECTORYPAGE_TEXT_TOP "${APP} needs a folder of its own: a new one, an empty one, or the one it is already in. The folder is replaced as a whole when the app updates."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- what both sides check

; The app's program cannot be replaced or removed while it runs: opening it for writing tells.
; $R8 is 1 when the app is closed (or not there), 0 when it runs and the user gave up.
!macro WaitUntilClosed
  StrCpy $R8 1
  IfFileExists "$INSTDIR\${EXE}" 0 closed_done
  closed_try:
    ClearErrors
    FileOpen $R0 "$INSTDIR\${EXE}" a
    IfErrors closed_running
    FileClose $R0
    Goto closed_done
  closed_running:
    StrCpy $R8 0
    ; Nobody to ask when it runs without its windows
    IfSilent closed_done
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APP} is still running.$\r$\n$\r$\nClose it, then choose Retry." IDRETRY closed_again
    Goto closed_done
  closed_again:
    StrCpy $R8 1
    Goto closed_try
  closed_done:
!macroend

; $R9 is 1 when $INSTDIR can be the app's folder (new, empty, or the app is in it), 0 when
; something else is in it
Function FolderIsOurs
  StrCpy $R9 1
  IfFileExists "$INSTDIR\*.*" 0 ours
  IfFileExists "$INSTDIR\${EXE}" ours
  FindFirst $R0 $R1 "$INSTDIR\*.*"
  look:
    StrCmp $R1 "" close
    StrCmp $R1 "." next
    StrCmp $R1 ".." next
    StrCpy $R9 0
    Goto close
  next:
    FindNext $R0 $R1
    Goto look
  close:
    FindClose $R0
  ours:
FunctionEnd

; "Next" on the page for the folder stays grey while the folder holds something else
Function .onVerifyInstDir
  Call FolderIsOurs
  StrCmp $R9 1 +2
    Abort
FunctionEnd

; ---------------------------------------------------------------- installing

Section "Install"
  Call FolderIsOurs
  StrCmp $R9 1 folder_ok
    ; Reached only without the pages (/S with /D=): there the page could not say no
    SetErrorLevel 2
    DetailPrint "The folder holds other things; ${APP} needs a folder of its own."
    Abort "The folder holds other things; ${APP} needs a folder of its own."
  folder_ok:

  !insertmacro WaitUntilClosed
  StrCmp $R8 1 app_closed
    SetErrorLevel 3
    Abort "${APP} is still running."
  app_closed:

  ; An older version goes first: what it had and the new one has not would otherwise stay
  IfFileExists "$INSTDIR\${EXE}" 0 +2
    RMDir /r "$INSTDIR"

  SetOutPath "$INSTDIR"
  File /r "${SOURCE}/*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; For this user only, like everything else here
  SetShellVarContext current
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}"

  ; The list of installed apps (Settings → Apps)
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "GamerNova.net"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Crypto90/PSRemotePlayTogether"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

; ---------------------------------------------------------------- removing

Section "Uninstall"
  !insertmacro WaitUntilClosed
  StrCmp $R8 1 un_closed
    SetErrorLevel 3
    Abort "${APP} is still running."
  un_closed:

  SetShellVarContext current
  Delete "$SMPROGRAMS\${APP}.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"

  ; The app's folder, and only if the app is in it. Settings, invites and logs are not in
  ; here and stay: installing again finds them.
  IfFileExists "$INSTDIR\${EXE}" 0 +2
    RMDir /r "$INSTDIR"
SectionEnd
