[Setup]
AppId=ScreenOff
AppName=ScreenOff
AppVersion={#ReleaseVersion}
VersionInfoVersion={#ReleaseVersion}
DefaultDirName={localappdata}\Programs\ScreenOff
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
MinVersion=10.0.22000
OutputDir=dist
OutputBaseFilename=ScreenOff-Setup
SetupIconFile=assets\ScreenOff.ico
UninstallDisplayIcon={app}\ScreenOff.exe
RestartApplications=no

[Files]
Source: "{#AppExecutable}"; DestDir: "{app}"; DestName: "ScreenOff.exe"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"

[Icons]
Name: "{autoprograms}\ScreenOff"; Filename: "{app}\ScreenOff.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\ScreenOff Settings"; Filename: "{app}\ScreenOff.exe"; Parameters: "--settings"; WorkingDir: "{app}"
Name: "{app}\ScreenOff Settings"; Filename: "{app}\ScreenOff.exe"; Parameters: "--settings"; WorkingDir: "{app}"

[Run]
Filename: "{app}"; Description: "Open installation folder"; Flags: postinstall shellexec skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'ScreenOff';
  SignInSuffix = '\ScreenOff.exe" --tray';
  ProfileKey = 'Software\ScreenOff';
  ListenerClass = 'ScreenOffShortcutListener';
  ListenerMutex = 'Local\ScreenOff-ShortcutListener';
  BlackoutMutex = 'Local\ScreenOff-MVP';
  CloseMessage = $0010;

var
  PreviousListener: String;
  Installed: Boolean;
  Uninstalled: Boolean;

function GetWindowThreadProcessId(Wnd: HWND; var ProcessId: Cardinal): Cardinal;
  external 'GetWindowThreadProcessId@user32.dll stdcall';
function FindWindowEx(Parent, After: HWND; ClassName, Caption: String): HWND;
  external 'FindWindowExW@user32.dll stdcall';
function OpenProcess(Access: Cardinal; Inherit: Boolean; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function QueryFullProcessImageName(Process: THandle; Flags: Cardinal; Path: String;
                                   var Length: Cardinal): Boolean;
  external 'QueryFullProcessImageNameW@kernel32.dll stdcall';
function WaitForSingleObject(Process: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function StopListener(var Executable: String): Boolean;
var
  Listener, Dialog: HWND;
  ProcessId, DialogProcessId, PathLength: Cardinal;
  Process: THandle;
  Path: String;
begin
  Result := False;
  Listener := FindWindowByClassName(ListenerClass);
  if Listener = 0 then
  begin
    Result := not CheckForMutexes(ListenerMutex);
    Exit;
  end;

  GetWindowThreadProcessId(Listener, ProcessId);
  Process := OpenProcess($101000, False, ProcessId); { Synchronize and query limited information. }
  if Process = 0 then Exit;
  try
    PathLength := 32768;
    SetLength(Path, PathLength);
    if not QueryFullProcessImageName(Process, 0, Path, PathLength) then Exit;
    SetLength(Path, PathLength);
    if CompareText(ExtractFileName(Path), 'ScreenOff.exe') <> 0 then Exit;

    { Older versions open an unowned Settings dialog. Close it before the listener. }
    Dialog := FindWindowEx(0, 0, '#32770', 'Setting');
    while Dialog <> 0 do
    begin
      GetWindowThreadProcessId(Dialog, DialogProcessId);
      if DialogProcessId = ProcessId then
      begin
        PostMessage(Dialog, CloseMessage, 0, 0);
        Break;
      end;
      Dialog := FindWindowEx(0, Dialog, '#32770', 'Setting');
    end;
    PostMessage(Listener, CloseMessage, 0, 0);
    Executable := Path;
    Result := WaitForSingleObject(Process, 5000) = 0;
  finally
    CloseHandle(Process);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if CheckForMutexes(BlackoutMutex) then
    Result := 'Resume the desktop before installing ScreenOff.'
  else if not StopListener(PreviousListener) then
    Result := 'Setup could not close the ScreenOff shortcut listener. Try again.';
end;

procedure StartListener(Executable: String);
var
  ErrorCode: Integer;
begin
  if (Executable <> '') and not Exec(Executable, '--tray', ExtractFileDir(Executable),
                                     SW_SHOWNORMAL, ewNoWait, ErrorCode) then
    SuppressibleMsgBox('Could not start the ScreenOff shortcut listener.',
                       mbError, MB_OK, IDOK);
end;

function InstalledSignInCommand: String;
begin
  Result := '"' + ExpandConstant('{app}') + SignInSuffix;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  if CurStep <> ssPostInstall then Exit;
  if
     RegQueryStringValue(HKCU, RunKey, RunValue, Command) and
     (Copy(Command, 1, 1) = '"') and
     (CompareText(Copy(Command, Length(Command) - Length(SignInSuffix) + 1,
                      Length(SignInSuffix)), SignInSuffix) = 0) and
     (CompareText(Command, InstalledSignInCommand) <> 0) then
  begin
    if (Length(InstalledSignInCommand) >= 260) or
       not RegWriteStringValue(HKCU, RunKey, RunValue, InstalledSignInCommand) then
      SuppressibleMsgBox('Could not update Start shortcut listener at sign-in. Open ScreenOff Settings to update it.',
                         mbError, MB_OK, IDOK);
  end;
  Installed := True;
end;

procedure DeinitializeSetup;
begin
  if Installed then
  begin
    if RegValueExists(HKCU, ProfileKey, 'Settings') then
      StartListener(ExpandConstant('{app}\ScreenOff.exe'));
  end
  else
    StartListener(PreviousListener);
end;

function InitializeUninstall: Boolean;
begin
  Result := not CheckForMutexes(BlackoutMutex);
  if Result then
    Result := StopListener(PreviousListener);
  if not Result then
    SuppressibleMsgBox('Resume the desktop or close ScreenOff before uninstalling.',
                       mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  if RegValueExists(HKCU, ProfileKey, 'Settings') and
     not RegDeleteValue(HKCU, ProfileKey, 'Settings') then
    SuppressibleMsgBox('Could not remove the ScreenOff profile.', mbError, MB_OK, IDOK);
  RegDeleteKeyIfEmpty(HKCU, ProfileKey);
  if RegValueExists(HKCU, RunKey, RunValue) and not RegDeleteValue(HKCU, RunKey, RunValue) then
    SuppressibleMsgBox('Could not remove the ScreenOff sign-in entry.', mbError, MB_OK, IDOK);
  Uninstalled := True;
end;

procedure DeinitializeUninstall;
begin
  if not Uninstalled then StartListener(PreviousListener);
end;
