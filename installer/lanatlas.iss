; LanAtlas installer (Inno Setup 6). Run by scripts\windows\package-win.ps1, which defines:
;   /DAppVersion=<version>  /DAppVersionNum=<a.b.c.0>  /DStageDir=<folder to install>
; ASCII only and no BOM, like every other text file of the repository.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionNum
  #define AppVersionNum "0.0.0.0"
#endif
#ifndef StageDir
  #error StageDir must be defined (see package-win.ps1)
#endif

[Setup]
; Never change this identifier: it is how a new version finds and replaces the installed one.
AppId={{7A4E9B60-2C58-4D3E-A8F1-0B6D92C7E5A3}
AppName=LanAtlas
AppVersion={#AppVersion}
AppVerName=LanAtlas {#AppVersion}
AppPublisher=morfredus
AppPublisherURL=https://morfredus.fr
VersionInfoVersion={#AppVersionNum}
DefaultDirName={autopf}\LanAtlas
DefaultGroupName=LanAtlas
AllowNoIcons=yes
UninstallDisplayIcon={app}\LanAtlas.exe
OutputBaseFilename=LanAtlas-{#AppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no administrator rights); the dialog still offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Close a running LanAtlas instead of failing on a locked file.
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LanAtlas"; Filename: "{app}\LanAtlas.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\LanAtlas"; Filename: "{app}\LanAtlas.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\LanAtlas.exe"; Description: "{cm:LaunchProgram,LanAtlas}"; Flags: nowait postinstall skipifsilent

[Code]
// The user's data (data and settings.ini under %APPDATA%\morfredus\LanAtlas, legacy settings under HKCU\Software\morfredus\LanAtlas) is kept
// by default; the question defaults to "No".
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    if MsgBox(ExpandConstant('{cm:KeepData}'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      DelTree(ExpandConstant('{userappdata}\morfredus\LanAtlas'), True, True, True);
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\morfredus\LanAtlas');
    end;
end;

[CustomMessages]
english.KeepData=Also delete your LanAtlas data (session, labels and settings, including saved keys and passwords)?%n%nChoose No to keep them for a later reinstall.
french.KeepData=Supprimer aussi vos donnees LanAtlas (session, etiquettes et reglages, y compris cles et mots de passe enregistres) ?%n%nChoisir Non pour les conserver en vue d'une reinstallation.
