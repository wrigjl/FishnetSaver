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
	setregview 64
	nsDialogs::Create 1018
	Pop $Dialog

	${If} $Dialog == error
		Abort
	${EndIf}


	${NSD_CreateLabel} 0 0 100% 12u "Fishnet API key"

	ReadRegStr $thekey HKLM SOFTWARE\FishnetSaver Key
	strcmp $thekey "Fishnet API key" error done
	error:
		strcpy $thekey "Fishnet API key"
	done:

	${NSD_CreateText} 0 13u 100% 13u $thekey
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

section "Visual Stdio Runtime"
	setoutpath "$INSTDIR"
	file "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Redist\MSVC\14.28.29325\vcredist_x64.exe"
	execwait '"$INSTDIR\vcredist_x64.exe" /quiet'
	delete "$INSTDIR\vcredist_x64.exe"
SectionEnd

section
	setregview 64
	setOutPath $INSTDIR

	strcpy $LogProviderKey "SYSTEM\CurrentControlSet\services\eventlog\Application\FishnetProvider"
	strcpy $FishnetKey "SOFTWARE\FishnetSaver"

	file x64\release\DummyFish.exe
	file x64\release\FishnetSaver.exe
	file x64\release\FishWrapper.exe
	file x64\release\fishnet-x86_64-pc-windows-gnu.exe
	file FishnetSaver\messages.dll
	file "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Redist\MSVC\14.28.29325\vcredist_x64.exe"

	delete $SYSDIR\FishnetSaver.scr
	copyfiles "$INSTDIR\FishnetSaver.exe" "$SYSDIR\FishnetSaver.scr"

	# log/debugging provider
	WriteRegDWORD HKLM $LogProviderKey "CategoryCount" 1
	WriteRegDWORD HKLM $LogProviderKey "TypesSupported" 1
	WriteRegStr HKLM $LogProviderKey "CategoryMessageFile" "$INSTDIR\messages.dll"
	WriteRegStr HKLM $LogProviderKey "EventMessageFile" "$INSTDIR\messages.dll"
	WriteRegStr HKLM $LogProviderKey "ParameterMessageFile" "$INSTDIR\messages.dll"

	WriteRegStr HKLM $FishnetKey "Key" $thekey
	WriteRegStr HKLM $FishnetKey "Program" "$INSTDIR\fishnet-x86_64-pc-windows-gnu.exe"
	WriteRegStr HKLM $FishnetKey "Wrapper" "$INSTDIR\FishWrapper.exe"

	# Set the logon screensaver
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "SCRNSAVE.EXE" "$SYSDIR\FishnetSaver.scr"
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "ScreenSaveTimeOut" "900"
	WriteRegStr HKU ".DEFAULT\Control Panel\Desktop" "ScreenSaveActive" "1"

	# Set the current user screensaver
	WriteRegStr HKCU "Control Panel\Desktop" "SCRNSAVE.EXE" "$SYSDIR\FishnetSaver.scr"
	WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveTimeOut" "900"
	WriteRegStr HKCU "Control Panel\Desktop" "ScreenSaveActive" "1"

	# Call the dll to make them active, now.
	System::Call 'user32::SystemParametersInfo(i 0x000f, i 900, i 0, i 3)i' ;SPI_SETSCREENSAVETIMEOUT
	System::Call 'user32::SystemParametersInfo(i 0x0011, i 1, i 0, i 3)i'   ;SPI_SETSCREENSAVEACTIVE

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "DisplayName" "FishnetSaver"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "UninstallString" '"$INSTDIR\uninstaller.exe"'
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "URLInfoAbout" "https://github.com/wrigjl/FishnetSaver"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver" "NoRepair" 1

	delete "$INSTDIR\vcredist_x64.exe"

	writeUninstaller $INSTDIR\uninstaller.exe
sectionend

section "Uninstall"
	setregview 64

	Delete $INSTDIR\uninstaller.exe
	Delete $INSTDIR\fishnet-x86_64-pc-windows-gnu.exe
	Delete $INSTDIR\messages.dll
	Delete $INSTDIR\FishnetSaver.exe
	Delete $INSTDIR\DummyFish.exe
	Delete $INSTDIR\FishWrapper.exe
	delete "$INSTDIR\vcredist_x64.exe"
	Rmdir $INSTDIR

	strcpy $LogProviderKey "SYSTEM\CurrentControlSet\services\eventlog\Application\FishnetProvider"
	strcpy $FishnetKey "SOFTWARE\FishnetSaver"

	# log/debugging provider
	DeleteRegKey HKLM $LogProviderKey
	DeleteRegKey HKLM $FishnetKey
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FishnetSaver"

	delete $SYSDIR\FishnetSaver.scr

	# XXX what should we do about HCU\.DEFAULT and HKCU screensavers?

sectionend
