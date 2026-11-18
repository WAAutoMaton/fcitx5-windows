[Setup]
AppName=Fcitx5 restart choice test
AppVersion=1.0
DefaultDirName={tmp}\fcitx5-restart-test-unused
CreateAppDir=no
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
  StageUpgrade, StageOwned, StageSealed, StageSealAttempted, TestSilent: Boolean;
  Helper, Scenario: String;
  HelperCalls, HelperResult, SealCalls, SealResult, StageSealError, ErrorMessages: Integer;

function RunHelper(Command: String; OriginalUser: Boolean): Integer;
begin
  if OriginalUser then
    RaiseException('Incorrect restart preparation helper');
  if Command = 'seal-stage' then begin
    SealCalls := SealCalls + 1;
    Log('TEST seal-stage');
    Result := SealResult;
  end else if Command = 'suspend-upgrade' then begin
    HelperCalls := HelperCalls + 1;
    Result := HelperResult;
  end else if Command = 'cancel-upgrade' then begin
    if StageSealed then RaiseException('Sealed upgrade was cancelled');
    Log('TEST cancel-upgrade');
    Result := 0;
  end else
    RaiseException('Unexpected helper command');
end;

procedure ResumeInstallation;
begin
end;

function TestMessage(Text: String; Typ: TMsgBoxType; Buttons: Integer): Integer;
begin
  ErrorMessages := ErrorMessages + 1;
  Result := IDOK;
end;

#include TestCode

function InitializeSetup: Boolean;
begin
  Scenario := ExpandConstant('{param:SCENARIO|stage}');
  StageUpgrade := Scenario <> 'fresh';
  StageOwned := StageUpgrade;
  TestSilent := Scenario = 'silent';
  SealResult := 3010;
  if Scenario = 'failure' then SealResult := 5;
  if Scenario = 'zero' then SealResult := 0;
  Result := True;
end;

procedure CheckRestartRouting;
begin
  StageSealed := True;
  { Inno's hidden YesRadio is checked by default, including /NORESTART. }
  WizardForm.YesRadio.Visible := False;
  WizardForm.YesRadio.Checked := True;
  if not UpgradeNextButtonClick(wpFinished) or (HelperCalls <> 0) then
    RaiseException('Hidden restart choice suspended activation');
  WizardForm.YesRadio.Visible := True;
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
  { Sealing failure must not publish success or a successful reboot code. }
  SealResult := 5;
  if NeedRestart then
    RaiseException('Sealing failure requested restart');
  if StageSealed or (GetCustomSetupExitCode <> 5) then
    RaiseException('Failed stage reported success');
  Log('FCITX5-RESTART-CHOICE-TEST-PASSED');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    Log('TEST ssPostInstall');
    if StageSealed <> (StageUpgrade and (Scenario <> 'failure') and (Scenario <> 'zero')) then
      RaiseException('Stage was not sealed before ssPostInstall');
    if StageUpgrade and (SealCalls <> 1) then
      RaiseException('Incorrect sealing count');
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  UpgradeCurPageChanged(CurPageID);
  if CurPageID <> wpFinished then exit;
  if (Scenario = 'failure') or (Scenario = 'zero') then begin
    if StageSealed or (GetCustomSetupExitCode = 0) or (GetCustomSetupExitCode = 3010) or
        (WizardForm.FinishedHeadingLabel.Caption <> 'Cannot prepare the Fcitx5 upgrade') or
        (Pos(Format('Cannot finish preparing the Fcitx5 upgrade (%d)', [SealResult]), WizardForm.FinishedLabel.Caption) <> 1) then
      RaiseException('Failed stage reported success');
    if NeedRestart or (SealCalls <> 1) then
      RaiseException('Failed sealing was replayed or requested restart');
  end else if StageUpgrade then begin
    if WizardForm.FinishedLabel.Caption <> 'The Fcitx5 upgrade is ready. Restart Windows to complete it. You can continue using the current version until then.' then
      RaiseException('Upgrade finished label was overwritten');
    if NeedRestart <> not TestSilent then
      RaiseException('Incorrect restart decision');
    if (SealCalls <> 1) or (GetCustomSetupExitCode <> 3010) then
      RaiseException('Sealing was repeated or lost the restart exit code');
  end else if (SealCalls <> 0) or (GetCustomSetupExitCode <> 0) then
    RaiseException('Fresh install sealed an upgrade');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and (Scenario = 'routing') then begin
    Result := False;
    CheckRestartRouting;
    WizardForm.Close;
    exit;
  end;
  Result := UpgradeNextButtonClick(CurPageID);
  if CurPageID <> wpFinished then exit;
  { All lifecycle runs use /NORESTART, even when TestSilent simulates the
    interactive script path. Windows must never be restarted by this fixture. }
  if WizardForm.YesRadio.Visible or (HelperCalls <> 0) then
    RaiseException('NORESTART suspended activation');
  Log('FCITX5-RESTART-LIFECYCLE-TEST-PASSED');
end;
