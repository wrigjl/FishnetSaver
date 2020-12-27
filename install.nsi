Unicode True

!include nsDialogs.nsh
!include LogicLib.nsh


page custom getfishkey checkfishkey
page instfiles

outfile "installer.exe"

installDir $PROGRAMFILES64\FishnetSaver

var LogProviderKey
var FishnetKey

var dialog
var fishkey
var thekey

function getfishkey
	nsDialogs::Create 1018
	Pop $Dialog

	${If} $Dialog == error
		Abort
	${EndIf}

	${NSD_CreateLabel} 0 0 100% 12u "Fishnet API key"

	${NSD_CreateText} 0 13u 100% 13u "Fishnet API key"
	Pop $fishkey

	${NSD_CreateLabel} 0 27u 100% 13u "You need a fishnet API key from:"
	${NSD_CreateLabel} 0 40u 100% -13u "https://lichess.org/get-fishnet"

	nsDialogs::Show

	${NSD_SetFocus} $fishkey
functionend

function checkfishkey

	${NSD_GetText} $fishkey $0
	strcpy $thekey $0
	strcmp $thekey "Fishnet API key" error done
	error:
	  MessageBox MB_OK "Invalid API key"
	  abort
	done:

functionend

section
	setregview 64
	setOutPath $INSTDIR

	strcpy $LogProviderKey "SYSTEM\CurrentControlSet\services\eventlog\Application\FishnetProvider"
	strcpy $FishnetKey "SOFTWARE\FishnetSaver"

	file x64\release\DummyFish.exe
	file x64\release\FishnetSaver.scr
	file x64\release\FishWrapper.exe
	file x64\release\fishnet-x86_64-pc-windows-msvc.exe
	file FishnetSaver\messages.dll

	delete $SYSDIR\FishnetSaver.scr

	copyfiles $INSTDIR\FishnetSaver.scr $SYSDIR

	# log/debugging provider
	WriteRegDWORD HKLM $LogProviderKey "CategoryCount" 1
	WriteRegDWORD HKLM $LogProviderKey "TypesSupported" 1
	WriteRegStr HKLM $LogProviderKey "CategoryMessageFile" "$INSTDIR\messages.dll"
	WriteRegStr HKLM $LogProviderKey "EventMessageFile" "$INSTDIR\messages.dll"
	WriteRegStr HKLM $LogProviderKey "ParameterMessageFile" "$INSTDIR\messages.dll"

	WriteRegStr HKLM $FishnetKey "Key" $thekey
	WriteRegStr HKLM $FishnetKey "Program" "$INSTDIR\DummyFish.exe"
	WriteRegStr HKLM $FishnetKey "Wrapper" "$INSTDIR\FishWrapper.exe"

	# Set the logon screensaver
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "SCRNSAVE.EXE" "$SYSDIR\FishnetSaver.scr"
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "ScreenSaveTimeOut" "900"
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "ScreenSaveActive" "1"

	# Set the current user screensaver
	WriteRegStr HKCU "Control Panel\Desktop" "SCRNSAVE.EXE" "$SYSDIR\FishnetSaver.scr"
	WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveTimeOut" "900"
	WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveActive" "1"

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "DisplayName" "FishnetSaver"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "UninstallString" '"$INSTDIR\uninstaller.exe"'
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "URLInfoAbout" "https://github.com/wrigjl/FishnetSaver"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "NoRepair" 1

	writeUninstaller $INSTDIR\uninstaller.exe
sectionend

section "Uninstall"
	setregview 64

	Delete $INSTDIR\uninstaller.exe
	Delete $INSTDIR\fishnet-x86_64-pc-windows-msvc.exe
	Delete $INSTDIR\messages.dll
	Delete $INSTDIR\FishnetSaver.scr
	Delete $INSTDIR\DummyFish.exe
	Delete $INSTDIR\FishWrapper.exe
	Rmdir $INSTDIR

	strcpy $LogProviderKey "SYSTEM\CurrentControlSet\services\eventlog\Application\FishnetProvider"
	strcpy $FishnetKey "SOFTWARE\FishnetSaver"

	# log/debugging provider
	DeleteRegKey HKLM $LogProviderKey
	DeleteRegKey HKLM $FishnetKey

	delete $SYSDIR\FishnetSaver.scr

	# XXX what should we do about HCU\.DEFAULT and HKCU screensavers?

sectionend
