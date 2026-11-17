[Setup]
AppName=Fcitx5 restart choice test
AppVersion=1.0
DefaultDirName={tmp}\fcitx5-restart-test-unused
PrivilegesRequired=lowest
Uninstallable=no
CreateUninstallRegKey=no
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#TestOutput}
OutputBaseFilename=restart-choice-test

[Code]
var
  StageSealed, TestSilent: Boolean;
  Helper: String;
  HelperCalls, HelperResult, ErrorMessages: Integer;

function RunHelper(Command: String; OriginalUser: Boolean): Integer;
begin
  if (Command <> 'suspend-upgrade') or OriginalUser then
    RaiseException('Incorrect restart preparation helper');
  HelperCalls := HelperCalls + 1;
  Result := HelperResult;
end;

function TestMessage(Text: String; Typ: TMsgBoxType; Buttons: Integer): Integer;
begin
  ErrorMessages := ErrorMessages + 1;
  Result := IDOK;
end;

#include TestCode

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID <> wpReady then exit;
  Result := False;
  StageSealed := True;
  WizardForm.YesRadio.Checked := False;
  if not UpgradeNextButtonClick(wpFinished) or (HelperCalls <> 0) then
    RaiseException('Restart later suspended activation before shutdown');
  WizardForm.YesRadio.Checked := True;
  if not UpgradeNextButtonClick(wpFinished) or (HelperCalls <> 1) then
    RaiseException('Restart now did not prepare activation');
  HelperResult := 5;
  if UpgradeNextButtonClick(wpFinished) or (ErrorMessages <> 1) then
    RaiseException('Preparation failure did not block restart');
  TestSilent := True;
  if not UpgradeNextButtonClick(wpFinished) or (HelperCalls <> 2) then
    RaiseException('Silent installation suspended activation immediately');
  TestSilent := False;
  StageSealed := False;
  if not UpgradeNextButtonClick(wpFinished) or (HelperCalls <> 2) then
    RaiseException('Fresh installation suspended activation');
  Log('FCITX5-RESTART-CHOICE-TEST-PASSED');
  WizardForm.Close;
end;
