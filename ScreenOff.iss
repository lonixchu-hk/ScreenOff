[Setup]
AppId=ScreenOff
AppMutex=Local\ScreenOff-MVP,Local\ScreenOff-ShortcutListener
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

[Messages]
SetupAppRunningError=Setup has detected that %1 is currently running.%n%nIn the notification area beside the Windows clock, right-click the ScreenOff icon and select Quit. If the icon is hidden, click the up arrow beside the clock first.%n%nIf you still cannot find the icon, close the ScreenOff Settings window and use Task Manager (Ctrl+Shift+Esc) to end ScreenOff.exe under Details.%n%nClick OK to continue, or Cancel to exit Setup.

[Files]
Source: "{#AppExecutable}"; DestDir: "{app}"; DestName: "ScreenOff.exe"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"
Source: "ScreenOff User Guide.txt"; DestDir: "{app}"

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

function InstalledSignInCommand: String;
begin
  Result := '"' + ExpandConstant('{app}') + SignInSuffix;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  if (CurStep = ssPostInstall) and
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
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if (CurUninstallStep = usPostUninstall) and
     RegQueryStringValue(HKCU, RunKey, RunValue, Command) and
     (CompareText(Command, InstalledSignInCommand) = 0) then
  begin
    if not RegDeleteValue(HKCU, RunKey, RunValue) then
      SuppressibleMsgBox('Could not remove the ScreenOff sign-in entry.', mbError, MB_OK, IDOK);
  end;
end;
