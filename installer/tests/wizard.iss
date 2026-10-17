#define BundleRuntimes 0

[Setup]
AppName=Fcitx5 wizard test
AppVersion=1.0
SetupArchitecture=x64
ArchitecturesAllowed=x64os
DefaultDirName={tmp}\fcitx5-wizard-test-unused
PrivilegesRequired=lowest
Uninstallable=no
CreateUninstallRegKey=no
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
OutputDir={#TestOutput}
OutputBaseFilename=wizard-test-{#RebootUpgrade}

[Tasks]
Name: "loginstart"; Description: "Test task"; Flags: unchecked

[Code]
#include TestCode

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ExpectedUpgrade: Boolean;
  AppRoot, Marker, ExpectedRuntime, ExpectedDependencies: String;
begin
  Result := True;
  if CurPageID <> wpReady then
    exit;
  Result := False;
  ExpectedUpgrade := ExpandConstant('{param:EXPECTUPGRADE|0}') = '1';
  AppRoot := ExpandConstant('{app}');
  Marker := AppRoot + '\setup\managed-install';
  if not ModeKnown then
    RaiseException('Mode was not detected before the tasks page');
  if StageUpgrade <> ExpectedUpgrade then
    RaiseException('Incorrect install mode');
  if ShouldSkipPage(wpSelectTasks) <> ExpectedUpgrade then
    RaiseException('Incorrect tasks page visibility');
  if NormalInstall = ExpectedUpgrade then
    RaiseException('Incorrect normal install checks');
  ExpectedRuntime := AppRoot;
  ExpectedDependencies := AppRoot + '\setup';
  if ExpectedUpgrade then begin
    ExpectedRuntime := AppRoot + '\setup\upgrades\current\payload';
    ExpectedDependencies := AppRoot + '\setup\upgrades\current\dependencies';
  end;
  if RuntimeDestination('') <> ExpectedRuntime then
    RaiseException('Incorrect runtime destination');
  if DependenciesDestination('') <> ExpectedDependencies then
    RaiseException('Incorrect dependencies destination');
  { Installing or removing the marker must not change the cached mode. }
  if FileExists(Marker) then begin
    if not DeleteFile(Marker) then
      RaiseException('Cannot remove test marker');
  end else if not SaveStringToFile(Marker, 'FCITX5-MANAGED-1', False) then
    RaiseException('Cannot create test marker');
  if NormalInstall = ExpectedUpgrade then
    RaiseException('Install mode changed after the marker changed');
  Log('FCITX5-WIZARD-TEST-PASSED');
  WizardForm.Close;
end;
