; Instalador de Barullo Tune para Windows (Inno Setup 6)
; Se compila desde el workflow con:  iscc /DAppVersion=0.4.0 /DArtDir=<artefactos Release> /DOutDir=<salida> packaging\windows.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ArtDir
  #define ArtDir "..\build\BarulloTune_artefacts\Release"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

[Setup]
AppId={{3D8E1F6A-5B2C-4E7D-A9F0-7C6B5A4D3E21}
AppName=Barullo Tune
AppVersion={#AppVersion}
AppPublisher=Escoita
AppPublisherURL=https://www.escoita.link
DefaultDirName={autopf}\Barullo Tune
DefaultGroupName=Barullo Tune
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=BarulloTune-{#AppVersion}-windows-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Barullo Tune

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Todo (VST3, CLAP e aplicación)"
Name: "custom"; Description: "Escoller"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (para el DAW)"; Types: full custom
Name: "clap"; Description: "Plugin CLAP (para el DAW)"; Types: full custom
Name: "app";  Description: "Aplicación independente"; Types: full custom

[Files]
; VST3: va a la carpeta estándar C:\Program Files\Common Files\VST3
Source: "{#ArtDir}\VST3\Barullo Tune.vst3\*"; DestDir: "{commoncf64}\VST3\Barullo Tune.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtDir}\CLAP\Barullo Tune.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#ArtDir}\Standalone\Barullo Tune.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{group}\Barullo Tune"; Filename: "{app}\Barullo Tune.exe"; Components: app
Name: "{autodesktop}\Barullo Tune"; Filename: "{app}\Barullo Tune.exe"; Components: app; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Icona no escritorio"; Components: app; Flags: unchecked

[Run]
Filename: "{app}\Barullo Tune.exe"; Description: "Abrir Barullo Tune"; Components: app; Flags: nowait postinstall skipifsilent
