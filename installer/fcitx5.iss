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
#ifndef RebootUpgrade
  #define RebootUpgrade 1
#endif
#ifndef RuntimeManifest
  #error RuntimeManifest is required
#endif
#ifndef RuntimeFiles
  #error RuntimeFiles is required
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
Uninstallable=NormalInstall
CreateUninstallRegKey=NormalInstall
#ifdef SignToolCommand
SignTool=release $f
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Prefix}\setup\Fcitx5Upgrade.exe"; Flags: dontcopy noencryption
Source: "{#RuntimeManifest}"; Flags: dontcopy noencryption
Source: "{#Prefix}\setup\Fcitx5Upgrade.exe"; DestDir: "{app}\setup\maintenance-host"; Flags: ignoreversion uninsrestartdelete; Check: NormalInstall
Source: "{#RuntimeManifest}"; DestDir: "{app}\setup"; DestName: "runtime-manifest.json"; Flags: ignoreversion uninsrestartdelete; Check: NormalInstall
#include RuntimeFiles
#if Int(BundleRuntimes)
Source: "{#Dependencies}\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#Dependencies}\vc_redist.x64.exe"; DestDir: "{code:DependenciesDestination}"; Flags: ignoreversion; Check: CopyUpgradeDependencies
Source: "{#Dependencies}\WindowsAppRuntimeInstall-x64.exe"; DestDir: "{code:DependenciesDestination}"; Flags: ignoreversion uninsrestartdelete; Check: CopyRuntimeFiles
#endif

[Icons]
Name: "{group}\Fcitx5 Settings"; Filename: "{app}\settings\Fcitx5Settings.exe"; Check: NormalInstall
Name: "{group}\Uninstall Fcitx5"; Filename: "{app}\setup\maintenance-host\Fcitx5Upgrade.exe"; Parameters: "uninstall --prefix ""{app}"""; Check: NormalInstall

[Tasks]
Name: "loginstart"; Description: "Start Fcitx5 when I sign in (for modern apps)"; Flags: unchecked

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\Fcitx5\Installer"; Flags: uninsdeletekey; Check: NormalInstall

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
  ModeKnown: Boolean;
  StageUpgrade: Boolean;
  StageOwned: Boolean;
  StageSealed: Boolean;
  AlreadyStaged: Boolean;

procedure DetectMode;
begin
  if not ModeKnown then begin
    StageUpgrade := ({#RebootUpgrade} <> 0) and
      FileExists(ExpandConstant('{app}\setup\managed-install'));
    ModeKnown := True;
  end;
end;

function NormalInstall: Boolean;
begin
  DetectMode;
  Result := not StageUpgrade;
end;

function CopyRuntimeFiles: Boolean;
begin
  DetectMode;
  Result := not AlreadyStaged;
end;

function CopyUpgradeDependencies: Boolean;
begin
  Result := StageUpgrade and not AlreadyStaged;
end;

function RuntimeDestination(Param: String): String;
begin
  DetectMode;
  if StageUpgrade then
    Result := ExpandConstant('{app}\setup\upgrades\current\payload')
  else
    Result := ExpandConstant('{app}');
end;

function DependenciesDestination(Param: String): String;
begin
  if StageUpgrade then
    Result := ExpandConstant('{app}\setup\upgrades\current\dependencies')
  else
    Result := ExpandConstant('{app}\setup');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { The app constant is initialized after wpSelectDir, even when that page is hidden. }
  if PageID = wpSelectTasks then
    DetectMode;
  Result := StageUpgrade and (PageID = wpSelectTasks);
end;

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
  DetectMode;
  if StageUpgrade then begin
    ExtractTemporaryFile('Fcitx5Upgrade.exe');
    ExtractTemporaryFile('{#PackageName}.runtime.json');
    Helper := ExpandConstant('{tmp}\Fcitx5Upgrade.exe');
    Code := RunHelper('begin-stage --manifest ' +
      AddQuotes(ExpandConstant('{tmp}\{#PackageName}.runtime.json')), False);
    if Code = 3010 then begin
      AlreadyStaged := True;
      StageSealed := True;
    end else if Code = 0 then
      StageOwned := True
    else
      Result := Format('Cannot prepare the Fcitx5 upgrade (%d). The current version has not been replaced. Run the maintenance helper query-upgrade command for the current state.', [Code]);
    exit;
  end;
  Helper := ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe');
  if FileExists(ExpandConstant('{app}\setup\maintenance-host\Fcitx5Upgrade.exe')) then
    Helper := ExpandConstant('{app}\setup\maintenance-host\Fcitx5Upgrade.exe');
  if FileExists(Helper) then begin
    Code := RunHelper('preflight-machine', False);
    if Code <> 0 then begin
      NeedsRestart := Code = 3010;
      Result := Format('Cannot install Fcitx5 (%d). Check for conflicting registration, a pending upgrade/recovery, or files pending restart deletion.', [Code]);
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
var
  Code: Integer;
begin
  if CurStep = ssPostInstall then begin
    if StageUpgrade then begin
      if not AlreadyStaged then begin
        Code := RunHelper('seal-stage', False);
        if Code <> 3010 then
          RaiseException(Format('Cannot finish preparing the Fcitx5 upgrade (%d). The current version has not been replaced.', [Code]));
        StageSealed := True;
      end;
      WizardForm.FinishedLabel.Caption := 'The Fcitx5 upgrade is ready. Restart Windows to complete it. You can continue using the current version until then.';
      exit;
    end;
    Helper := ExpandConstant('{app}\setup\Fcitx5SetupHelper.exe');
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
        AddQuotes(ExpandConstant('{app}\setup\maintenance-host\Fcitx5Upgrade.exe')) + ' uninstall --prefix ' + AddQuotes(ExpandConstant('{app}')));
      RegWriteStringValue(HKLM64, UninstallKey, 'QuietUninstallString',
        AddQuotes(ExpandConstant('{app}\setup\maintenance-host\Fcitx5Upgrade.exe')) + ' uninstall --silent --prefix ' + AddQuotes(ExpandConstant('{app}')));
      if not MachineOnly then begin
        if WizardIsTaskSelected('loginstart') then
          RequireHelper('enable-login-start', True)
        else
          RequireHelper('disable-login-start', True);
      end;
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
  if StageUpgrade then begin
    if StageOwned and not StageSealed then
      RunHelper('cancel-upgrade', False);
    exit;
  end;
  ResumeInstallation;
end;

function NeedRestart: Boolean;
begin
  { Silent staging never initiates a reboot, even without /NORESTART. }
  Result := StageSealed and not WizardSilent;
end;

function GetCustomSetupExitCode: Integer;
begin
  if StageSealed then Result := 3010 else Result := 0;
end;

function InitializeUninstall: Boolean;
var
  Code: Integer;
begin
  HelperMode := ExpandConstant('{param:HELPER|0}') = '1';
  if HelperMode then begin
    Result := True;
    exit;
  end;
  Helper := ExpandConstant('{app}\setup\maintenance-host\Fcitx5Upgrade.exe');
  if FileExists(Helper) then begin
    Code := RunHelper('preflight-machine', False);
    if Code <> 0 then begin
      Result := False;
      if not UninstallSilent then
        MsgBox('A Fcitx5 upgrade or recovery is pending. Use the Fcitx5 uninstall entry or maintenance helper to resolve it before removing files.', mbError, MB_OK);
      exit;
    end;
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
