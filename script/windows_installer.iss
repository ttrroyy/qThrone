#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionMajor
  #define AppVersionMajor "0"
#endif
#ifndef AppVersionMinor
  #define AppVersionMinor "0"
#endif
#ifndef AppVersionPatch
  #define AppVersionPatch "0"
#endif
#ifndef AppVersionBuild
  #define AppVersionBuild "0"
#endif

[Setup]
AppId={{791A78A7-02DF-4E18-990D-1A73186F6778}
AppName=qThrone
AppVersion={#AppVersion}
AppVerName=qThrone {#AppVersion}
AppPublisher=qThrone
VersionInfoVersion={#AppVersionMajor}.{#AppVersionMinor}.{#AppVersionPatch}.{#AppVersionBuild}
VersionInfoProductName=qThrone
VersionInfoDescription=qThrone Setup
VersionInfoCopyright=Throne
SourceDir=..
OutputDir=deployment
OutputBaseFilename=qThroneSetup
SetupIconFile=res\Throne.ico
UninstallDisplayName=qThrone
UninstallDisplayIcon={app}\qThrone.exe
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={code:DefaultInstallDir}
DirExistsWarning=no
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=win64
CloseApplications=force
RestartApplications=no
; Uninstall removes the associations Throne registers at runtime, so Explorer has to reload them.
ChangesAssociations=yes
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
; The default block is 4x the dictionary (256 MB), which would leave two of the four threads idle.
LZMABlockSize=118784

[Messages]
SelectDirBrowseLabel=To continue, click Next. If the folder you choose is not named qThrone, Setup creates a qThrone folder inside it, so uninstalling only ever removes qThrone's own folder.

[Files]
Source: "deployment\windows-amd64\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion; Check: IsX64OS; MinVersion: 10.0.17763
Source: "deployment\windowslegacy-amd64\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion; Check: IsX64OS; OnlyBelowVersion: 10.0.17763
Source: "deployment\windows-arm64\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion; Check: IsArm64
Source: "deployment\windowslegacy-386\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion; Check: IsX86OS

[Icons]
Name: "{autoprograms}\qThrone"; Filename: "{app}\qThrone.exe"
Name: "{autodesktop}\qThrone"; Filename: "{app}\qThrone.exe"

[Registry]
Root: HKA; Subkey: "Software\qThrone"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey

[UninstallDelete]
Type: files; Name: "{app}\qThroneUpdater.old"

[Run]
Filename: "{app}\qThrone.exe"; Description: "{cm:LaunchProgram,qThrone}"; Flags: postinstall nowait skipifsilent

[Code]
const
  LegacyUninstall = 'Microsoft\Windows\CurrentVersion\Uninstall\qThrone';

var
  DeleteUserData: Boolean;

// The NSIS installer was 32-bit, so on 64-bit Windows its HKLM keys sit under WOW6432Node of this installer's 64-bit view.
function LegacyKey(const SubKey: String): String;
begin
  if IsAdminInstallMode and Is64BitInstallMode then
    Result := 'Software\WOW6432Node\' + SubKey
  else
    Result := 'Software\' + SubKey;
end;

function LegacyValue(const SubKey, Name: String; var Value: String): Boolean;
begin
  if IsAdminInstallMode then
    Result := RegQueryStringValue(HKEY_LOCAL_MACHINE, LegacyKey(SubKey), Name, Value)
  else
    Result := RegQueryStringValue(HKEY_CURRENT_USER, LegacyKey(SubKey), Name, Value);
  Result := Result and (Value <> '');
end;

function SameAsApp(const Dir: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(Dir), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0;
end;

// An NSIS install keeps its folder, since Throne's config lives next to the exe.
function DefaultInstallDir(Param: String): String;
begin
  if LegacyValue('qThrone', 'InstallPath', Result) then
    Exit;
  if IsAdminInstallMode then
    Result := ExpandConstant('{autopf}\qThrone')
  else
    Result := ExpandConstant('{localappdata}\qThrone');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Probe: String;
  Created: Boolean;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;
  Dir := RemoveBackslashUnlessRoot(WizardDirValue);
  // Uninstalling can delete <dir>\config, so Throne must get a folder of its own.
  if CompareText(ExtractFileName(Dir), 'qThrone') <> 0 then
  begin
    Dir := AddBackslash(Dir) + 'qThrone';
    WizardForm.DirEdit.Text := Dir;
  end;
  if IsAdminInstallMode then
    Exit;
  Created := not DirExists(Dir);
  Probe := AddBackslash(Dir) + '.throne-write-test';
  Result := ForceDirectories(Dir) and SaveStringToFile(Probe, '', False);
  DeleteFile(Probe);
  if Created then
    RemoveDir(Dir);
  if not Result then
    SuppressibleMsgBox('You do not have permission to install to "' + Dir + '".' + #13#10#13#10 +
      'Choose a different folder, or restart Setup and choose to install for all users.', mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir: String;
begin
  if CurStep <> ssPostInstall then
    Exit;
  // Otherwise the NSIS installer's Apps & Features entry and uninstall.exe outlive the migration and remove these files.
  if LegacyValue(LegacyUninstall, 'InstallLocation', Dir) and SameAsApp(Dir) then
    if IsAdminInstallMode then
      RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, LegacyKey(LegacyUninstall))
    else
      RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, LegacyKey(LegacyUninstall));
  DeleteFile(ExpandConstant('{app}\uninstall.exe'));
end;

procedure StopThrone;
var
  Locator, Service, Processes, Process: Variant;
  Prefix, ExePath: String;
  I: Integer;
  Stopped: Boolean;
begin
  Prefix := Lowercase(AddBackslash(ExpandConstant('{app}')));
  Stopped := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT * FROM Win32_Process WHERE Name = ''qThrone.exe'' OR Name = ''qThroneCore.exe'' OR Name = ''qwdtt.exe''');
    for I := 0 to Processes.Count - 1 do
    begin
      Process := Processes.ItemIndex(I);
      if not VarIsNull(Process.ExecutablePath) then
      begin
        // Pascal Script converts a Variant to String on assignment, but not when passed as a String parameter.
        ExePath := Process.ExecutablePath;
        if Pos(Prefix, Lowercase(ExePath)) = 1 then
        begin
          Process.Terminate(0);
          Stopped := True;
        end;
      end;
    end;
  except
    Log('Could not stop Throne: ' + GetExceptionMessage);
  end;
  if Stopped then
    Sleep(1000);
end;

// Throne writes these at runtime; an entry that points at another copy by now belongs to that copy.
function PointsAtApp(const SubKey: String): Boolean;
var
  Command: String;
begin
  Result := RegQueryStringValue(HKEY_CURRENT_USER, SubKey + '\shell\open\command', '', Command) and
    (Pos(Lowercase(ExpandConstant('{app}\qThrone.exe')), Lowercase(Command)) > 0);
end;

procedure RemoveOpenWith(const Ext: String);
begin
  RegDeleteValue(HKEY_CURRENT_USER, 'Software\Classes\' + Ext + '\OpenWithProgids', 'qThrone.Config');
end;

procedure RemoveAssociations;
begin
  if PointsAtApp('Software\Classes\throne') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\throne');
  if PointsAtApp('Software\Classes\Applications\qThrone.exe') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\Applications\qThrone.exe');
  if not PointsAtApp('Software\Classes\qThrone.Config') then
    Exit;
  RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\qThrone.Config');
  RemoveOpenWith('.json');
  RemoveOpenWith('.conf');
  RemoveOpenWith('.yaml');
  RemoveOpenWith('.yml');
  // Claimed before 1.3.
  RemoveOpenWith('.ini');
  RemoveOpenWith('.txt');
end;

// Throne writes these to HKLM when it runs elevated, so a per-user uninstall lacks the rights to remove them.
procedure RemoveCrashDumpKey(const ExeName: String);
var
  SubKey, Folder: String;
begin
  SubKey := 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\' + ExeName;
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, SubKey, 'DumpFolder', Folder) then
    Exit;
  Folder := Lowercase(AddBackslash(Folder));
  if (Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Folder) = 1) or
     (Pos(Lowercase(ExpandConstant('{localappdata}\qThrone\')), Folder) = 1) then
    RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, SubKey);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  App := ExpandConstant('{app}');
  if CurUninstallStep = usUninstall then
  begin
    StopThrone;
    RemoveAssociations;
    RemoveCrashDumpKey('qThrone.exe');
    RemoveCrashDumpKey('qThroneCore.exe');
    DeleteUserData := SuppressibleMsgBox('Also delete your qThrone profiles, settings and logs?' + #13#10#13#10 +
      'Choose No if you plan to reinstall qThrone later and want to keep them.', mbConfirmation, MB_YESNO, IDYES) = IDYES;
  end
  else if (CurUninstallStep = usPostUninstall) and DeleteUserData then
  begin
    if FileExists(App + '\config\throne.db') then
      DelTree(App + '\config', True, True, True);
    // Where Throne keeps its config when its own folder is not writable (Qt's AppConfigLocation).
    DelTree(ExpandConstant('{localappdata}\qThrone\config'), True, True, True);
    RemoveDir(ExpandConstant('{localappdata}\qThrone'));
    DelTree(ExpandConstant('{userappdata}\qThrone'), True, True, True);
    RemoveDir(App);
  end;
end;
