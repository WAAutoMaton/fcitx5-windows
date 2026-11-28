# Fcitx5 Windows

[English](README.md) | [中文文档](README_zh-cn.md) | [日本語](README_ja.md)

AMD64용으로 개발 중인 Fcitx5 Windows 이식 버전입니다. Core는 별도의 프로세스로 실행되고, x64 및 x86 TSF DLL이 애플리케이션과 통합되며, WinUI 3 설정 앱이 입력기와 엔진 옵션을 관리합니다.

`base` 에디션은 병음(Pinyin), 쌍병음(Shuangpin) 및 `fcitx5-chinese-addons` 테이블 입력기를 제공합니다. `full` 에디션에는 Hangul(한국어), Mozc(일본어), Rime, SKK(일본어), 원저우어(Wenzhounese), 주음(Zhuyin) 및 `fcitx5-table-extra`가 추가됩니다. 통합 x64 설치 프로그램은 CI 및 Nightly 릴리스 워크플로에서 배포됩니다.

## 주요 기능

- **중국어·일본어·다국어 입력**:
  - 전체 병음 및 8가지 내장 쌍병음 프로필(샤오허, 자연마 등)을 지원하며 사전 편집, 후보 선택, 중국어 입력 확정을 제공합니다.
  - base 테이블 입력기: 오필(Wubi), 오필 병음(Wubi Pinyin), 창힐(Cangjie), 전보마(Dianbaoma), 이필(Erbi), 빙찬(Bingchan), 완펑(Wanfeng), 자연마(Ziranma) (해당 엔진과 테이블 데이터가 설치된 경우 사용 가능).
  - full 에디션 추가 기능: Hangul(한국어), Mozc(일본어), Rime, SKK(일본어), 원저우어, 주음 및 25종의 추가 테이블(오필 98, 정마, 보샤미, 창힐 5세대, 광둥어 병음 테이블 등).
  - 현재 활성화된 Windows 키보드 레이아웃을 그대로 통과시키는 다국어 키보드 항목.
- **후보 창**:
  - 포커스를 빼앗지 않는 DirectWrite/Direct2D 후보 팝업(컬러 글꼴 렌더링, 소프트웨어 DC 폴백, Per-Monitor V2 DPI 배치 지원).
  - **전역 설정 → 일반**에서 후보 레이아웃을 **세로**(기본값) 또는 자동 줄바꿈 **가로**로 전환 가능.
  - 숫자 키 또는 스페이스바로 후보 선택, 모던 앱 호스트 렌더링을 위한 TSF 후보 UIElement/Behavior 지원.
- **입력 표시기 및 상태 메뉴**:
  - 현재 입력기의 아이콘 또는 간략 레이블(영어 키보드는 `A`)과 도구 설명 이름 표시.
  - 우클릭 메뉴 상단에 현재 엔진의 동적 상태 동작 및 하위 메뉴(Rime 스키마 전환, 중·영문 문장부호 전환 등)를 표시하고, 하단에 설정 및 서비스 제어 명령 배치.
- **설정 및 단축키**:
  - 활성화된 입력기 추가, 제거, 순서 변경 지원. 별도의 추가 창에서 언어별로 그룹화되며 비키보드 입력기가 있는 언어가 상단에 배치됩니다.
  - 순환 전환 단축키(기본값 `Ctrl+Space`) 및 1~5번 항목 직접 전환 단축키(기본값 `Ctrl+Alt+1…5`) 설정 가능. 전역 단축키와 엔진 키 목록 옵션 모두 단축키 기록과 수동 텍스트 편집을 지원합니다.
  - 타사 병음 사전 로드 상태 확인 및 백그라운드 자동 다시 불러오기.
  - **전역 설정 → 플러그인**에서 검색된 애드온 상태를 읽기 전용으로 표시하며, 독립 엔진/리소스 플러그인 패키지 자동 검색을 지원합니다.
  - 중국어(간체), 중국어(번체), 일본어, 한국어, 영어 UI 및 시스템 언어 따르기 옵션 지원.
- **실행 환경 및 진단**:
  - 일반 데스크톱 세션에서 TSF 활성화 시 Core를 백그라운드에서 자동 시작하며, 설치 프로그램에 선택적 로그인 시 시작 옵션을 제공합니다.
  - 관리자 권한 데스크톱 앱(UAC linked-token), AppContainer 제한 입력 전송 및 명시적 파이프 규칙을 통한 Sandboxie 호스트 공유를 지원합니다.
  - Core의 Fcitx5 엔진 로그를 상시 기록(기본값 `Info`, 상세 로그 활성화 시 즉시 `Debug`로 전환)하며, 상세 로그 활성화 시 Core·TSF·설정 프로그램의 구조화된 개발자 로그(설정 종료/예외 진단 포함)를 추가로 기록합니다. 구조화된 로그에는 입력 텍스트가 포함되지 않습니다.

## 설치

[Nightly 릴리스](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)에서는 Windows 10 버전 1809(build 17763) 이상용 4종의 x64 설치 프로그램과 SHA256 체크섬을 제공합니다. 모든 에디션에 Core, x64/x86 TSF DLL 및 설정 프로그램이 포함됩니다.

- `base`: `fcitx5-chinese-addons` 포함.
- `full`: `fcitx5-chinese-addons`, `fcitx5-hangul`, `fcitx5-mozc`, `fcitx5-rime`, `fcitx5-skk`, `fcitx5-table-extra`, `fcitx5-wenzhounese`, `fcitx5-zhuyin` 포함.

각 에디션은 런타임 포함 여부에 따라 두 가지 설치 파일로 제공됩니다.

- `Fcitx5-<run>-<base|full>-x64-setup.exe`: Visual C++ 및 Windows App Runtime 설치 프로그램 포함.
- `Fcitx5-<run>-<base|full>-x64-no-runtime-setup.exe`: 현재 사용자에게 Visual C++ 런타임 및 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`이 사전에 설치되어 있어야 합니다.

설치 프로그램은 Program Files에 런타임 트리를 배포하고, 동일한 x64 Core를 공유하는 x64/x86 TSF DLL을 등록한 뒤 설치 실행 사용자의 입력기를 구성하고 Core를 시작합니다. 로그인 시 시작은 기본 해제되어 있으며 수동 서비스 중지 상태를 존중합니다. 자세한 내용은 [설치 프로그램 설계](docs/windows-installer-design.md)를 참조하세요.

업그레이드 시 현재 버전을 계속 사용할 수 있도록 유지한 채 새 버전을 스테이징하고 종료 코드 `3010`을 반환하며, Windows 재시작 후 적용을 완료합니다. 상태 조회, 취소 및 복구 절차는 [지연 재시작 업그레이드](docs/windows-reboot-upgrade-design.md)를 참조하세요.

설치 프로그램으로 관리되는 버전을 제거하려면 시작 메뉴의 **Uninstall Fcitx5** 또는 Windows의 **설치된 앱**을 사용하세요. 사용자 데이터와 공유 런타임은 유지되며, 사용 중인 파일은 Windows 재시작 후 자동으로 정리됩니다. 아래의 PowerShell 등록 스크립트는 개발용 배포 전용입니다.

## 빌드 및 배포

사전 요구 사항: CMake 3.27+, Git, MSYS2 `clang64` 패키지, 독립 LLVM(MSVC 14.52+ 사용 시 LLVM 22+), ImageMagick, 그리고 C++ 도구·Windows SDK·ATL·MSBuild·NuGet이 포함된 Visual Studio. 설정 프로그램을 실행하려면 Visual C++ 런타임 및 x64 Windows App SDK 1.8 Framework/DDLM >= `8000.994.2142.0`이 필요합니다. 전체 의존성 목록은 [사전 요구 사항](docs/build.md#prerequisites)을 참조하세요.

저장소 루트의 Windows PowerShell에서 `base` 에디션을 빌드 및 배포합니다:

```powershell
./scripts/build-and-deploy.ps1
```

이 스크립트는 고정 버전 서브모듈을 초기화하고 Windows 패치를 적용한 뒤 AMD64 Release Core/Settings, x64/x86 TSF DLL 및 설치/업그레이드 도우미를 빌드하고 기본 TSF CTest 스위트를 실행한 다음, SHA256이 검증된 사전 빌드 사전/모델 데이터와 함께 `dist/pinyin`에 배포합니다. 이후 실행 시 빌드 캐시를 재사용합니다.

동일 빌드의 SDK를 사용하여 `full` 에디션의 추가 플러그인을 모두 추가하려면 다음을 실행합니다:

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

추가 플러그인 빌드에는 Git LFS, Rime용 `clang64` librime/스키마 패키지, 주음용 GLib/Berkeley DB 및 SKK용 GLib/`libgee`/`json-glib`가 필요합니다. 자세한 내용은 [독립 플러그인 빌드](docs/build.md#independent-input-method-plugins)를 참조하세요.

기존 배포를 덮어쓰기 전에 **서비스 중지**를 선택하고 해당 TSF DLL을 로드한 애플리케이션을 종료하세요. 사용자 지정 경로, 개별 빌드 및 검증 방법은 [빌드·배포·검증 가이드](docs/build.md)를 참조하세요.

## 입력기 플러그인

입력기는 독립 엔진 또는 리소스 패키지로 빌드 및 설치할 수 있습니다. `fcitx5-chinese-addons`는 Core와 함께 빌드됩니다. `fcitx5-table-extra`는 `base`의 `table` 엔진을 재사용해 25종의 테이블을 제공합니다. `fcitx5-hangul`은 고정 버전 libhangul 소스에서 빌드한 `libhangul-1.dll`과 한자·기호 사전을 포함합니다. `fcitx5-zhuyin`은 고정 버전 libpinyin 소스에서 빌드한 libzhuyin과 컴파일된 모델을 포함합니다. `fcitx5-skk`는 고정 버전 libskk 소스에서 빌드한 `libskk.dll`, 규칙 파일 및 `SKK-JISYO.L`을 포함하며, `fcitx5-mozc`는 내장 데이터 기반 인프로세스 `libmozc.dll`을, Rime은 공유 스키마와 OpenCC 데이터를 제공합니다.

### 설치 및 활성화

패키지를 다음 디렉터리 중 하나에 배치합니다:

| 위치 | 용도 |
| --- | --- |
| `%LOCALAPPDATA%/Fcitx5/plugins/<패키지명>` | 관리자 권한이 필요 없는 사용자별 설치 |
| `<Core 설치 경로>/plugins/<패키지명>` | `bin/Fcitx5.exe`와 같은 트리에 두는 포터블 배포 |

1. 입력 표시기의 우클릭 메뉴에서 **서비스 중지**를 선택합니다.
2. 패키지의 `bin`, `lib`, `share`, `licenses` 디렉터리를 `<패키지명>` 안에 배치합니다(엔진 DLL은 `lib/fcitx5`, 전용 의존성 DLL은 `bin`, 리소스는 `lib`/`share`). Core는 직접 하위 디렉터리를 스캔하며 기존의 공유 `plugins/bin`, `plugins/lib`, `plugins/share` 구조도 호환 지원합니다.
3. **서비스 다시 시작**을 선택한 뒤 **입력기 설정**을 열어 새 입력기를 활성화 목록에 추가합니다.

플러그인을 업데이트하거나 제거할 때도 먼저 서비스를 중지한 뒤 완료 후 다시 시작하세요. 독립 패키지를 제거하려면 `<패키지명>` 디렉터리를 삭제합니다. **전역 설정 → 플러그인**에서 검색된 애드온의 로드됨/로드되지 않음/비활성화됨 상태를 확인하고 개인 플러그인 폴더를 열 수 있습니다.

### 호환성

각 플러그인의 빌드 지침은 해당 플러그인 저장소에 있습니다. 플러그인은 Core와 동일한 아키텍처, Fcitx5/libime 버전, Windows 패치, Clang Windows GNU 툴체인 및 런타임 라이브러리에 일치해야 하며, Core가 제공하는 공용 런타임 DLL을 덮어쓰지 않고 공유해야 합니다. 레이아웃 및 SDK 세부 정보는 [Windows 플러그인 가이드](docs/windows-plugins.md)를 참조하세요.

## 사용 방법

### 개발용 배포 등록

```powershell
./win32/scripts/install.ps1
```

필요 시 관리자 권한 상승을 요청합니다. `dist/pinyin` 트리 전체를 유지하고 이전 DLL을 로드했던 앱을 다시 여세요. `Win+Space`로 Fcitx5를 선택하면(목록에 없을 경우 Windows 중국어 언어 옵션에서 추가) 활성화 시 Core가 자동으로 시작됩니다.

개발용 배포 등록을 해제하려면 먼저 **서비스 중지**를 선택한 뒤 다음을 실행하세요:

```powershell
./win32/scripts/uninstall.ps1
```

DLL 언로드를 위해 열려 있는 앱을 다시 시작하세요. 미리 보기, 대체 경로 및 잠긴 DLL 해제 도우미는 [등록 및 DLL 관리](docs/build.md#registration-and-dll-management)를 참조하세요.

### 일상 입력 및 전환

- 설정된 순환 단축키(기본값 `Ctrl+Space`) 또는 입력 표시기 좌클릭으로 활성화된 입력기를 순환 전환합니다. 사용할 수 없는 항목은 자동으로 건너뜁니다.
- 기본 목록은 영어 키보드와 병음이며, 설정에서 쌍병음, 테이블 입력기, Hangul, Mozc, SKK, Rime, 원저우어, 주음 또는 다국어 키보드를 추가할 수 있습니다.
- 스페이스바 또는 숫자 키로 후보를 선택합니다. 입력기를 전환하거나 포커스를 잃으면 완료되지 않은 사전 편집이 취소됩니다.
- **전역 설정 → 단축키**에서 순환 전환 및 1~5번 항목 직접 전환 단축키를 설정할 수 있습니다(비워 두면 비활성화). 직접 전환은 목록의 원래 위치를 기준으로 하며, 항목이 없거나 사용할 수 없으면 키를 그대로 통과시킵니다.
- 중·영문 문장부호는 입력 표시기의 상태 메뉴에서 전환하며, 병음·쌍병음 및 punctuation 모듈을 사용하는 테이블 입력기가 같은 상태를 공유합니다.
- Rime 선택 시 상태 메뉴의 스키마 하위 메뉴에서 스키마를 전환하거나 `F4` 키로 후보 형식의 스키마 메뉴를 열 수 있습니다. 자세한 내용은 [상태 메뉴 설계](docs/windows-status-menu-design.md)를 참조하세요.

### 표시기 메뉴 및 설정

입력 표시기를 우클릭하면 상단에 현재 엔진의 동적 상태 동작과 하위 메뉴가, 하단에 5개의 고정 명령이 표시됩니다:

- **입력기 설정**: 활성화된 입력기 목록 관리(즉시 저장), 엔진 옵션 편집(**적용** 클릭 시 저장) 및 타사 병음 사전 상태 확인.
- **서비스 다시 시작**: Core를 시작 또는 재시작하며, 설정 창이 열려 있던 경우 자동으로 다시 엽니다.
- **서비스 중지**: Core와 설정 프로그램을 중지하고 다시 시작할 때까지 자동 시작을 일시 중지합니다.
- **사용자 데이터 폴더**: `%APPDATA%/Fcitx5`(설정 및 병음 학습 데이터)를 엽니다.
- **정보**: 아키텍처, CI 빌드 번호, 커밋 및 프로젝트/라이선스 링크를 표시합니다.

**전역 설정** 구성:

- **일반**: UI 언어 선택(적용 후 Core와 설정 프로그램에 반영하려면 서비스 재시작 필요; 기본 메뉴와 정보 창은 백그라운드에서 갱신) 및 후보 창 레이아웃(**세로** 또는 **가로**).
- **개발자 옵션**: 상세 로그를 즉시 전환하고 `%LOCALAPPDATA%/Fcitx5/logs`를 엽니다. Core는 엔진 로그를 상시 저장하며(기본값 `Info`, 상세 로그 시 `Debug`), 상세 모드에서는 Core·TSF·설정 프로그램의 구조화된 JSON Lines 로그를 추가로 기록합니다. 로그는 8 MiB 및 UTC 날짜 변경 시 순환되며 7일 보관, 디렉터리 총 512 MiB 한도로 관리됩니다. 자세한 내용은 [개발자 로그 설계](docs/windows-developer-logging-design.md)를 참조하세요.
- **플러그인**: 검색된 애드온 상태를 확인하고 개인 플러그인 폴더를 엽니다.
- **단축키**: 입력기 순환 및 1~5번 항목 직접 전환 단축키를 설정합니다.

### 사전 및 특수 실행 환경

- **타사 병음 사전**: 호환되는 Fcitx5/libime 바이너리 `.dict` 파일을 `%APPDATA%/Fcitx5/pinyin/dictionaries`에 배치하면 자동으로 로드됩니다. 자세한 내용은 [사전 안내](docs/pinyin.md#third-party-dictionaries)를 참조하세요.
- **Sandboxie**: [Sandboxie 가이드](docs/windows-sandboxie-design.md)에 따라 명시적 파이프 허용 규칙을 구성하세요. 호스트 Core가 이미 실행 중이어야 합니다.
- **모던 앱 / AppContainer**: CI/Nightly 설치 프로그램은 기본적으로 모던 TSF 카테고리를 활성화합니다. 로컬 빌드는 `-ModernApps` 지정 후 재등록이 필요합니다. 호스트 Core가 미리 실행되어 있어야 하며, AppContainer 메뉴에서는 설정만 노출됩니다. 자세한 내용은 [모던 앱 지원](docs/windows-modern-apps-design.md)을 참조하세요.
- **관리자 권한 데스크톱 앱(UAC)**: 실행 중인 일반 권한 호스트 Core에 연결하여 서비스 명령을 위임합니다. 자세한 내용은 [UAC IPC 설계](docs/windows-uac-ipc-design.md)를 참조하세요.

## 문서

- [빌드, 배포 및 검증](docs/build.md)
- [Windows 설치 프로그램](docs/windows-installer-design.md)
- [지연 재시작 업그레이드 및 복구](docs/windows-reboot-upgrade-design.md)
- [x86 TSF 빌드, 배포 및 검증](docs/windows-x86-tsf-design.md)
- [UAC 상승 클라이언트 및 호스트 서비스 제어](docs/windows-uac-ipc-design.md)
- [입력기 아키텍처 및 개별 빌드](docs/pinyin.md)
- [독립 Windows 입력기 플러그인 및 SDK 빌드](docs/windows-plugins.md)
- [Windows 설정 앱 설계](docs/windows-settings-design.md)
- [입력기 상태 메뉴 및 Rime 스키마](docs/windows-status-menu-design.md)
- [후보 창 렌더링](docs/windows-candidate-rendering.md)
- [UI 언어 및 현지화](docs/windows-ui-localization-design.md)
- [Sandboxie 호스트 공유 및 구성](docs/windows-sandboxie-design.md)
- [모던 앱 전송 및 TSF UIElement](docs/windows-modern-apps-design.md)
- [개발자 로그 및 설정 진단](docs/windows-developer-logging-design.md)

## 크레딧

- [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
- [weasel](https://github.com/rime/weasel): GPL-3.0-only
