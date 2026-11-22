; Installer di Ribalta per Windows (Inno Setup 6): "make installer" nell'ambiente MSYS2 UCRT64.
; Il Makefile passa il compilatore ISCC con /DAppVersion=<versione> e /DSourceDir=<cartella di make dist>.
; Solo ASCII: Inno Setup legge questo file come ANSI se non ha il BOM.
;
; Installazione per l'utente corrente, senza amministratore, in %LOCALAPPDATA%\Programs\Ribalta
; (chi vuole puo' scegliere "per tutti gli utenti" dalla finestra iniziale o con /ALLUSERS).
; Installare una versione piu' recente sopra una vecchia e' un aggiornamento: l'AppId e' lo stesso.
;
; I dati dell'utente (impostazioni e statistiche) stanno in %APPDATA%\Ribalta, fuori da {app}:
; il disinstallatore li lascia dove sono, per scelta (vedi la fine del file).

; versione del programma (x.y.z): se manca il /D usa 0.0.0, cosi' lo script compila anche a mano
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; cartella prodotta da "make dist" (exe, DLL, assets, LICENSE): relativa a questo file se non e' assoluta
#ifndef SourceDir
  #define SourceDir "..\..\build\dist\Ribalta"
#endif

[Setup]
; GUID fisso: identifica l'applicazione per aggiornamenti e disinstallazione, NON va mai cambiato.
; "{{" e' la graffa aperta letterale di Inno; la chiave nel registro e' Uninstall\{GUID}_is1
AppId={{142E70C6-A819-47F3-B36F-9C7CB97474DB}
AppName=Ribalta
AppVersion={#AppVersion}
AppPublisher=Ribalta
AppPublisherURL=https://github.com/Ginteaa/Ribalta
AppSupportURL=https://github.com/Ginteaa/Ribalta
AppUpdatesURL=https://github.com/Ginteaa/Ribalta
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Installer di Ribalta
; senza amministratore per default; la finestra iniziale e /ALLUSERS | /CURRENTUSER permettono di cambiare
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
; {autopf} = %LOCALAPPDATA%\Programs per un utente, C:\Program Files per tutti gli utenti
DefaultDirName={autopf}\Ribalta
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\..\assets\ribalta.ico
UninstallDisplayIcon={app}\ribalta.exe
LicenseFile=..\..\LICENSE
OutputDir=..\..\build
OutputBaseFilename=Ribalta-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
; Restart Manager: se Ribalta e' aperto, Setup e il disinstallatore lo chiudono prima di toccare i file
CloseApplications=yes
; la finestra di scelta della lingua compare solo se la lingua di Windows non e' fra quelle sotto
ShowLanguageDialog=auto

[Languages]
; l'interfaccia di Ribalta e' in italiano: Italian e' la lingua predefinita
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Ribalta"; Filename: "{app}\ribalta.exe"
Name: "{autodesktop}\Ribalta"; Filename: "{app}\ribalta.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ribalta.exe"; Description: "{cm:LaunchProgram,Ribalta}"; Flags: nowait postinstall skipifsilent

; Niente [UninstallDelete]: il disinstallatore toglie solo i file installati qui sopra.
; %APPDATA%\Ribalta (config.ini, sessions.csv: impostazioni e statistiche) NON si tocca MAI,
; ne' alla disinstallazione ne' durante un aggiornamento: sono dati dell'utente.
