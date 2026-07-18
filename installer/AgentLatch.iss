#ifndef AppVersion
  #define AppVersion "0.2.0-dev"
#endif
#ifndef VersionInfoVersion
  #define VersionInfoVersion "0.2.0.0"
#endif
#ifndef Architecture
  #define Architecture "x64"
#endif
#ifndef SourceExecutable
  #error SourceExecutable must point to AgentLatch.exe
#endif
#ifndef RepoRoot
  #define RepoRoot ".."
#endif
#ifndef OutputDirectory
  #define OutputDirectory "..\dist"
#endif
#ifndef AgentLatchAppIdValue
  #define AgentLatchAppIdValue "{{BBC37307-15F1-4F00-8936-60BC06B5FAB5}"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "AgentLatch-Setup-" + AppVersion + "-" + Architecture
#endif

[Setup]
AppId={#AgentLatchAppIdValue}
AppName=AgentLatch
AppVersion={#AppVersion}
AppVerName=AgentLatch {#AppVersion}
AppPublisher=AgentLatch contributors
AppPublisherURL=https://github.com/byassin/agent-latch
AppSupportURL=https://github.com/byassin/agent-latch/issues
AppUpdatesURL=https://github.com/byassin/agent-latch/releases
DefaultDirName={localappdata}\AgentLatch
DefaultGroupName=AgentLatch
DisableProgramGroupPage=yes
DisableWelcomePage=no
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDirectory}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#RepoRoot}\resources\app.ico
UninstallDisplayIcon={app}\AgentLatch.exe
UninstallDisplayName=AgentLatch {#AppVersion}
VersionInfoCompany=AgentLatch contributors
VersionInfoDescription=AgentLatch Windows Setup
VersionInfoProductName=AgentLatch
VersionInfoProductVersion={#VersionInfoVersion}
VersionInfoVersion={#VersionInfoVersion}
WizardStyle=modern dynamic
WizardSizePercent=110
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=lowest
#if Architecture == "ARM64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes

[Tasks]
Name: "startup"; Description: "Start AgentLatch when I sign in"; GroupDescription: "Startup:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceExecutable}"; DestDir: "{app}"; DestName: "AgentLatch.exe"; Flags: ignoreversion
Source: "{#SourceExecutable}"; DestName: "AgentLatch-bootstrap.exe"; Flags: dontcopy
Source: "{#RepoRoot}\scripts\install-integrations.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\scripts\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\docs\INTEGRATIONS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RepoRoot}\docs\PRIVACY.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\AgentLatch"; Filename: "{app}\AgentLatch.exe"; Check: NotTestMode
Name: "{group}\Uninstall AgentLatch"; Filename: "{uninstallexe}"; Check: NotTestMode
Name: "{autodesktop}\AgentLatch"; Filename: "{app}\AgentLatch.exe"; Tasks: desktopicon; Check: NotTestMode

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "AgentLatch"; ValueData: """{app}\AgentLatch.exe"" --background"; Tasks: startup; Flags: uninsdeletevalue; Check: NotTestMode
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "AgentLatch"; Flags: deletevalue; Check: StartupTaskNotSelected
Root: HKCU; Subkey: "Software\AgentLatch"; Flags: uninsdeletekey; Check: NotTestMode

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-integrations.ps1"" -AgentLatchPath ""{app}\AgentLatch.exe"" -InstallMarkerPath ""{app}\.integrations-installed""{code:GetConfigRootArgument}"; StatusMsg: "Connecting AgentLatch to your coding agents..."; Flags: runhidden waituntilterminated; Check: ShouldInstallHooks
Filename: "{app}\AgentLatch.exe"; Description: "Launch AgentLatch"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\AgentLatch.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "StopAgentLatch"; Check: ShouldStopAgentLatch
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-integrations.ps1"" -AgentLatchPath ""{app}\AgentLatch.exe"" -InstallMarkerPath ""{app}\.integrations-installed"" -Uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveAgentLatchIntegrations"

[UninstallDelete]
Type: files; Name: "{app}\.integrations-installed"
Type: dirifempty; Name: "{app}"

[Code]
function CommandLineContains(const Value: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 1 to ParamCount do
    if CompareText(ParamStr(Index), Value) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function GetConfigRootArgument(Param: String): String;
var
  ConfigRoot: String;
begin
  ConfigRoot := ExpandConstant('{param:AGENTLATCHCONFIGROOT|}');
  if (ConfigRoot <> '') and (Pos('"', ConfigRoot) = 0) then
    Result := ' -ConfigRoot "' + ConfigRoot + '"'
  else
    Result := '';
end;

function NotTestMode(): Boolean;
begin
  Result := not CommandLineContains('/TESTMODE');
end;

function StartupTaskNotSelected(): Boolean;
begin
  Result := (not WizardIsTaskSelected('startup')) and NotTestMode();
end;

function ShouldInstallHooks(): Boolean;
begin
  Result := not CommandLineContains('/SKIPHOOKS');
end;

function ShouldStopAgentLatch(): Boolean;
begin
  Result := not CommandLineContains('/NOSTOP');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if not ShouldStopAgentLatch() then
    Exit;
  ExtractTemporaryFile('AgentLatch-bootstrap.exe');
  if not Exec(
    ExpandConstant('{tmp}\AgentLatch-bootstrap.exe'),
    '--quit',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ExitCode) then
    Result := 'AgentLatch could not stop the currently running copy. Close AgentLatch and try Setup again.';
  Sleep(300);
end;
