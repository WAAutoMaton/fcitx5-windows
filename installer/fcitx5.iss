#ifndef Prefix
  #error Prefix is required
#endif
#ifndef BundleRuntimes
  #define BundleRuntimes 1
#endif
#if Int(BundleRuntimes)
  #ifndef Dependencies
    #error Dependencies is required
  #endif
#endif
#ifndef OutputDirectory
  #error OutputDirectory is required
#endif
#ifndef PackageVersion
  #define PackageVersion "0.1.0.0"
#endif
#ifndef PackageName
  #define PackageName "Fcitx5-0.1.0-x64-setup"
#endif

[Setup]
AppId={{0D07698A-591B-44B4-940F-BBD38AA62F0E}
AppName=Fcitx5
AppVersion={#PackageVersion}
AppPublisher=Fcitx5 Windows contributors
AppPublisherURL=https://github.com/fcitx/fcitx5
DefaultDirName={autopf}\Fcitx5
DefaultGroupName=Fcitx5
SetupArchitecture=x64
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
PrivilegesRequired=admin
CloseApplications=no
RestartApplications=no
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir={#OutputDirectory}
OutputBaseFilename={#PackageName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#Prefix}\licenses\Fcitx5-Windows-LICENSE.txt
UninstallDisplayIcon={app}\tsf\fcitx5-x86_64.dll
SetupLogging=yes
VersionInfoVersion={#PackageVersion}
Uninstallable=yes
#ifdef SignToolCommand
SignTool=release $f
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Prefix}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe,WindowsAppRuntimeInstall-x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs uninsrestartdelete
#if Int(BundleRuntimes)
Source: "{#Dependencies}\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#Dependencies}\WindowsAppRuntimeInstall-x64.exe"; DestDir: "{app}\setup"; Flags: uninsrestartdelete
#endif

[Icons]
Name: "{group}\Fcitx5 Settings"; Filename: "{app}\settings\Fcitx5Settings.exe"
Name: "{group}\Uninstall Fcitx5"; Filename: "{app}\setup\Fcitx5SetupHelper.exe"; Parameters: "uninstall"

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\Fcitx5\Installer"; Flags: uninsdeletekey

[Code]
const
  InstallerKey = 'SOFTWARE\Fcitx5\Installer';
  UninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{0D07698A-591B-44B4-940F-BBD38AA62F0E}_is1';
var
  Helper: String;
  Maintenance: Boolean;
  Registered: Boolean;
  Configured: Boolean;
  Paused: Boolean;
  MachineOnly: Boolean;
  Pending: Boolean;
  HelperMode: Boolean;

function RunHelper(Command: String; OriginalUser: Boolean): Integer;
var
  Success: Boolean;
begin
  if OriginalUser then
    Success := ExecAsOriginalUser(Helper, Command + ' --prefix ' + AddQuotes(ExpandConstant('{app}')),
      '', SW_HIDE, ewWaitUntilTerminated, Result)
  else
    Success := Exec(Helper, Command + ' --prefix ' + AddQuotes(ExpandConstant('{app}')),
      '', SW_HIDE, ewWaitUntilTerminated, Result);
  if not Success then Result := -1;
  Log(Format('Helper %s: %d', [Command, Result]));
end;

procedure RequireHelper(Command: String; OriginalUser: Boolean);
var
  Code: Integer;
begin
  Code := RunHelper(Command, OriginalUser);
  if Code <> 0 then
    RaiseException(Format('Fcitx5 %s failed (%d). See the setup and installer-helper logs.', [Command, Code]));
end;

procedure ResumeInstallation;
begin
  if Maintenance then begin
    RunHelper('clear-maintenance', False);
    Maintenance := False;
  end;
  if Paused and not MachineOnly then begin
    RunHelper('resume-user', True);
    Paused := False;
  end;
end;

function InitializeSetup: Boolean;
begin
  MachineOnly := ExpandConstant('{param:MACHINEONLY|0}') = '1';
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  Helper := ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe');
  if FileExists(Helper) then begin
    Code := RunHelper('preflight-machine', False);
    if Code <> 0 then begin
      NeedsRestart := Code = 3010;
      Result := Format('Cannot install Fcitx5 (%d). Another DLL is registered, or this directory has files pending restart deletion.', [Code]);
      exit;
    end;
  end;
  if FileExists(ExpandConstant('{app}\setup\managed-install')) then begin
    if not MachineOnly then begin
      Code := RunHelper('pause-user', True);
      if Code <> 0 then begin
        Result := Format('Cannot stop the existing Fcitx5 services (%d).', [Code]);
        exit;
      end;
      Paused := True;
    end;
    Code := RunHelper('set-maintenance', False);
    Maintenance := Code = 0;
    if Code = 0 then begin
      Sleep(3500);
      Code := RunHelper('inspect-locks', False);
    end;
    if Code <> 0 then begin
      ResumeInstallation;
      Result := Format('Fcitx5 files are still in use (%d). Close the applications using this input method, or sign out/restart and run Setup again.', [Code]);
      exit;
    end;
  end;
#if Int(BundleRuntimes)
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart',
      '', SW_HIDE, ewWaitUntilTerminated, Code) then Code := -1;
  if (Code <> 0) and (Code <> 3010) and (Code <> 1638) then begin
    ResumeInstallation;
    Result := Format('Visual C++ runtime installation failed (%d).', [Code]);
  end;
  if Code = 3010 then NeedsRestart := True;
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
#if Int(BundleRuntimes)
var
  Code: Integer;
#endif
begin
  if CurStep = ssPostInstall then begin
    try
      RequireHelper('register-machine', False);
      Registered := True;
      RequireHelper('clear-maintenance', False);
      Maintenance := True;
#if Int(BundleRuntimes)
      if not Exec(ExpandConstant('{app}\setup\WindowsAppRuntimeInstall-x64.exe'), '--quiet',
          '', SW_HIDE, ewWaitUntilTerminated, Code) then Code := -1;
      { A newer shared runtime is acceptable only when the user bootstrap probe succeeds. }
      if (Code <> 0) and (Code <> Integer($80073D06)) then
        RaiseException(Format('Windows App Runtime installation failed (%d).', [Code]));
#endif
      if not MachineOnly then begin
        Configured := True;
        RequireHelper('configure-user', True);
      end;
      RegWriteStringValue(HKLM64, InstallerKey, 'NativeUninstaller', ExpandConstant('{uninstallexe}'));
      RegWriteStringValue(HKLM64, UninstallKey, 'UninstallString',
        AddQuotes(ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe')) + ' uninstall');
      RegWriteStringValue(HKLM64, UninstallKey, 'QuietUninstallString',
        AddQuotes(ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe')) + ' uninstall --silent');
      Maintenance := False;
      Paused := False;
      if MachineOnly then
        WizardForm.FinishedLabel.Caption := 'Fcitx5 files and DLL registration are installed. User initialization has not run (machine-only deployment).'
      else
        WizardForm.FinishedLabel.Caption := 'Fcitx5 is installed and enabled for your account.';
    except
      if Configured then RunHelper('rollback-user', True);
      if Registered then RunHelper('unregister-machine', False);
      RunHelper('mark-pending', False);
      Maintenance := False;
      RaiseException(GetExceptionMessage);
    end;
  end;
end;

procedure DeinitializeSetup;
begin
  ResumeInstallation;
end;

function InitializeUninstall: Boolean;
begin
  HelperMode := ExpandConstant('{param:HELPER|0}') = '1';
  if HelperMode then begin
    Result := True;
    exit;
  end;
  Helper := ExpandConstant('{tmp}\Fcitx5SetupHelper.exe');
  Result := CopyFile(ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe'), Helper, True);
  if not Result then
    MsgBox('Cannot prepare the Fcitx5 uninstall helper. Program files were not removed.', mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    if not HelperMode then begin
      RequireHelper('set-maintenance', False);
      Sleep(3500);
      Code := RunHelper('inspect-locks', False);
      Pending := Code <> 0;
      RequireHelper('unregister-machine', False);
      RequireHelper('mark-pending', False);
    end;
  end;
  if CurUninstallStep = usPostUninstall then begin
    if Pending and not UninstallSilent then
      MsgBox('Fcitx5 has been unregistered. Some files are still in use. Signing out releases this session''s DLLs; restarting Windows completes automatic file cleanup. Save your work before signing out or restarting.', mbInformation, MB_OK);
  end;
end;

function UninstallNeedRestart: Boolean;
begin
  Result := Pending;
end;
