# Fcitx5 Windows

[English](README.md) | [中文文档](README_zh-cn.md)

AMD64 向けに開発中の Fcitx5 Windows 移植版です。Core は独立したプロセスとして動作し、x64 / x86 の TSF DLL がアプリケーションと連携し、WinUI 3 設定アプリで入力メソッドやエンジン設定を管理します。

`base` 版ではピンイン、双拼および `fcitx5-chinese-addons` のテーブル入力メソッドを提供します。`full` 版ではこれらに加えて Mozc（日本語）、Rime、SKK（日本語）、温州語、注音および `fcitx5-table-extra` を収録しています。統合 x64 インストーラーは CI および Nightly リリースワークフローで公開されています。

## 主な機能

- **中国語・日本語・多言語入力**:
  - フルピンインおよび 8 種類の組み込み双拼プロファイル（小鶴・自然碼など）に対応し、プリエディット、候補選択、確定入力を行えます。
  - base テーブル入力: 五筆字型、五筆ピンイン、倉頡、電報碼、二筆、氷蟾、晩風、自然碼（対応するエンジンおよびテーブルデータが導入されている場合に利用可能）。
  - full 版追加機能: Mozc（日本語）、Rime、SKK（日本語）、温州語、注音および 25 種類の追加テーブル（五筆 98、鄭碼、嘸蝦米、倉頡五代、広東語ピンインテーブルなど）。
  - 現在の Windows キーボードレイアウトを使ってキーをそのまま透過する多言語キーボード項目。
- **候補ウィンドウ**:
  - フォーカスを奪わない DirectWrite / Direct2D 候補ポップアップ（カラーフォント描画、ソフトウェア DC フォールバック、Per-Monitor V2 DPI 配置に対応）。
  - **グローバル設定 → 全般**から候補レイアウトを**縦並び**（既定）または自動折り返しの**横並び**に切り替え可能。
  - 数字キーやスペースキーでの候補選択、およびモダンアプリのホスト描画向け TSF 候補 UIElement / Behavior に対応。
- **入力インジケーターとステータスメニュー**:
  - 現在の入力メソッドのアイコンまたは短いラベル（英語キーボードは `A`）とツールチップ名を表示。
  - 右クリックメニューの上部にアクティブなエンジンのステータス操作やサブメニュー（Rime のスキーム切り替え、中英文句読点切り替えなど）を動的に表示し、下部に設定およびサービス操作コマンドを配置。
- **設定とショートカット**:
  - 有効な入力メソッドの追加・削除・並べ替えに対応。独立した追加ウィンドウでは言語別にグループ化され、非キーボード入力メソッドを持つ言語が上位に表示されます。
  - 巡回切り替えショートカット（既定 `Ctrl+Space`）および 1〜5 番目への直接切り替え（既定 `Ctrl+Alt+1…5`）を設定可能。グローバルショートカットおよびエンジンのキー一覧オプションは、ショートカット記録と手動テキスト編集の両方に対応しています。
  - サードパーティ製ピンイン辞書の読み込み状態を表示し、バックグラウンドで自動再読み込み。
  - **グローバル設定 → プラグイン**で検出されたアドオンの状態を読み取り専用で一覧表示し、独立したエンジン / リソースプラグインパッケージの自動検出に対応。
  - 簡体字中国語・繁体字中国語・日本語・英語 UI およびシステムの言語に従う設定に対応。
- **実行環境と診断**:
  - 通常のデスクトップセッションでは TSF 有効化時に Core をバックグラウンド自動起動。インストーラーには任意のログイン時自動起動オプションを用意。
  - 管理者権限のデスクトップアプリ（UAC linked-token）、AppContainer 制限付き入力転送、および明示的なパイプ設定による Sandboxie ホスト共有に対応。
  - Core の Fcitx5 エンジンログを常時保存（既定 `Info`、詳細ログ有効時は即時 `Debug` へ切り替え）。詳細ログ有効時には Core・TSF・Settings の構造化開発者ログ（Settings の終了・例外診断を含む）を追加記録。構造化ログに入力テキストは含まれません。

## インストール

[Nightly リリース](https://github.com/WAAutoMaton/fcitx5-windows/releases/tag/latest)では Windows 10 バージョン 1809（build 17763）以降向けの 4 種類の x64 インストーラーと SHA256 チェックサムを公開しています。いずれのエディションにも Core、x64 / x86 TSF DLL、および Settings が含まれます。

- `base`: `fcitx5-chinese-addons` を同梱。
- `full`: `fcitx5-chinese-addons`、`fcitx5-mozc`、`fcitx5-rime`、`fcitx5-skk`、`fcitx5-table-extra`、`fcitx5-wenzhounese`、`fcitx5-zhuyin` を同梱。

各エディションにはランタイム同梱の有無による 2 種類のインストーラーがあります。

- `Fcitx5-<run>-<base|full>-x64-setup.exe`: Visual C++ および Windows App Runtime インストーラーを同梱。
- `Fcitx5-<run>-<base|full>-x64-no-runtime-setup.exe`: 事前に現在のユーザー向けに Visual C++ ランタイムと x64 Windows App SDK 1.8 Framework / DDLM >= `8000.994.2142.0` のインストールが必要。

インストーラーは Program Files にランタイム一式を配置し、同一の x64 Core を共有する x64 / x86 TSF DLL を登録して、インストール実行ユーザーの入力メソッド設定と Core の起動を行います。ログイン時起動は既定でオフになっており、手動でのサービス停止状態を尊重します。詳細は[インストーラー設計](docs/windows-installer-design.md)を参照してください。

アップグレード時は現行バージョンを利用可能なまま新バージョンをステージングして終了コード `3010` を返し、Windows 再起動後に適用を完了します。状態確認・キャンセル・復旧手順は[遅延再起動アップグレード](docs/windows-reboot-upgrade-design.md)を参照してください。

インストーラーで導入した環境を削除するには、スタートメニューの **Uninstall Fcitx5** または Windows の「インストールされているアプリ」を使用してください。ユーザーデータと共有ランタイムは保持され、使用中のファイルは Windows 再起動後に削除が完了します。後述の PowerShell 登録スクリプトは開発用デプロイ専用です。

## ビルドとデプロイ

前提条件: CMake 3.27+、Git、MSYS2 `clang64` パッケージ、スタンドアロン LLVM（MSVC 14.52+ 使用時は LLVM 22+）、ImageMagick、および C++ ツール・Windows SDK・ATL・MSBuild・NuGet を備えた Visual Studio。Settings の実行には Visual C++ ランタイムと x64 Windows App SDK 1.8 Framework / DDLM >= `8000.994.2142.0` が必要です。詳細な依存一覧は[前提条件](docs/build.md#prerequisites)を参照してください。

リポジトリルートで Windows PowerShell から `base` 版をビルド・デプロイします:

```powershell
./scripts/build-and-deploy.ps1
```

固定バージョンのサブモジュール初期化、Windows 向けパッチ適用、AMD64 Release の Core / Settings・x64 / x86 TSF DLL・セットアップ / アップグレードヘルパーのビルド、既定の TSF CTest 実行を行い、SHA256 検証済みのビルド済み辞書・モデルデータとともに `dist/pinyin` へ配置します。再実行時はビルドキャッシュが再利用されます。

同じビルドの SDK を使って `full` 版の追加プラグインをすべて追加します:

```powershell
./scripts/build-plugins.ps1 -CorePrefix ./build/all/prefix -Prefix ./dist/pinyin
```

追加プラグインのビルドには Git LFS、Rime 用の `clang64` librime / スキームパッケージ、注音用の GLib / Berkeley DB、および SKK 用の GLib / `libgee` / `json-glib` が必要です。詳細は[独立プラグインのビルド](docs/build.md#independent-input-method-plugins)を参照してください。

既存のデプロイ先を上書きする前には、インジケーターのメニューから**サービスを停止**を選び、TSF DLL を読み込んでいるアプリケーションを終了してください。カスタムパス、個別ビルド、検証手順については[ビルド・デプロイ・検証ガイド](docs/build.md)を参照してください。

## 入力メソッドプラグイン

入力メソッドは独立したエンジンまたはリソースパッケージとしてビルド・導入できます。`fcitx5-chinese-addons` は Core と同時にビルドされます。`fcitx5-table-extra` は `base` の `table` エンジンを再利用して 25 種類のテーブルを提供します。`fcitx5-zhuyin` は固定バージョンの libpinyin ソースからビルドした libzhuyin とコンパイル済みモデルを同梱します。`fcitx5-skk` は固定バージョンの libskk ソースからビルドした `libskk.dll`・ルール定義・`SKK-JISYO.L` を同梱します。`fcitx5-mozc` は埋め込みデータ付きのプロセス内 `libmozc.dll` を構成し、Rime は共有スキームと OpenCC データを同梱します。

### 導入と有効化

パッケージは次のいずれかのディレクトリに配置します:

| 配置先 | 用途 |
| --- | --- |
| `%LOCALAPPDATA%/Fcitx5/plugins/<パッケージ名>` | 管理者権限不要のユーザー別インストール |
| `<Core インストール先>/plugins/<パッケージ名>` | `bin/Fcitx5.exe` と同じツリーに置くポータブル配置 |

1. 入力インジケーターの右クリックメニューから**サービスを停止**を選びます。
2. パッケージの `bin`、`lib`、`share`、`licenses` ディレクトリを `<パッケージ名>` 内に配置します（エンジン DLL は `lib/fcitx5`、プライベート依存 DLL は `bin`、リソースは `lib` / `share`）。Core は直接のサブディレクトリを走査し、従来の共有 `plugins/bin`・`plugins/lib`・`plugins/share` 配置にも互換対応しています。
3. **サービスを再起動**を選び、**入力メソッドの設定**を開いて新しい入力メソッドを有効一覧に追加します。

プラグインを更新または削除する際も同様に先にサービスを停止し、作業後に再起動してください。独立パッケージを削除するには `<パッケージ名>` ディレクトリを削除します。**グローバル設定 → プラグイン**では検出されたアドオンの「読み込み済み」「未読み込み」「無効」状態を確認でき、個人用プラグインフォルダーも開けます。

### 互換性

各プラグイン固有のビルド手順はそれぞれのリポジトリにあります。プラグインは Core と同一のアーキテクチャ、Fcitx5 / libime バージョン、Windows 向けパッチ、Clang Windows GNU ツールチェーンおよびランタイムライブラリに一致させる必要があり、Core が提供する共通ランタイム DLL を上書きせず共有してください。ディレクトリ配置と SDK の詳細は [Windows プラグインガイド](docs/windows-plugins.md)を参照してください。

## 使い方

### 開発用デプロイの登録

```powershell
./win32/scripts/install.ps1
```

必要に応じて管理者権限への昇格が要求されます。`dist/pinyin` ツリー一式をそのまま維持し、古い DLL を読み込んでいたアプリケーションを再起動してください。`Win+Space` で Fcitx5 を選択すると（一覧にない場合は Windows の中国語言語オプションから追加）、有効化時に Core が自動起動します。

開発用デプロイの登録を解除するには、先に**サービスを停止**を選んでから以下を実行します:

```powershell
./win32/scripts/uninstall.ps1
```

DLL をアンロードするため、開いているアプリケーションを再起動してください。ドライラン、別パス指定、ロックされた DLL の解放ヘルパーについては[登録と DLL 管理](docs/build.md#registration-and-dll-management)を参照してください。

### 日常の入力と切り替え

- 設定した巡回ショートカット（既定 `Ctrl+Space`）またはインジケーターの左クリックで有効な入力メソッドを順に切り替えます。利用不可の項目は自動的にスキップされます。
- 既定の一覧は英語キーボードとピンインです。設定画面から双拼、テーブル入力、Mozc、SKK、Rime、温州語、注音、多言語キーボードを追加できます。
- スペースキーや数字キーで候補を選択します。入力メソッドの切り替えやフォーカス喪失時には未確定のプリエディットがキャンセルされます。
- **グローバル設定 → ショートカット**で巡回切り替えおよび 1〜5 番目の直接切り替えを設定できます（空欄にすると無効化）。直接切り替えは一覧の元の位置を参照し、該当項目が存在しないか利用不可の場合はキーを透過します。
- 中英文句読点の切り替えはインジケーターのステータスメニューから行えます。ピンイン、双拼および punctuation モジュールを使用するテーブル入力メソッドでこの状態が共有されます。
- Rime を選択中は、ステータスメニューのスキームサブメニューからスキームを切り替えられるほか、`F4` で候補形式のスキームメニューを開けます。詳細は[ステータスメニュー設計](docs/windows-status-menu-design.md)を参照してください。

### インジケーターメニューと設定

入力インジケーターを右クリックすると、上部に現在のエンジンのステータス操作とサブメニュー、下部に 5 つの固定コマンドが表示されます:

- **入力メソッドの設定**: 有効な入力メソッド一覧の管理（即時保存）、エンジン設定の編集（**適用**で保存）、サードパーティ製ピンイン辞書の確認。
- **サービスを再起動**: Core を起動または再起動します。Settings が開いていた場合は自動的に開き直します。
- **サービスを停止**: Core と Settings を停止し、再起動されるまで自動起動を一時停止します。
- **ユーザーデータフォルダー**: `%APPDATA%/Fcitx5`（設定およびピンイン学習データ）を開きます。
- **バージョン情報**: アーキテクチャ、CI ビルド番号、コミットおよびプロジェクト / ライセンスリンクを表示します。

**グローバル設定**の構成:

- **全般**: UI 言語の選択（適用後、Core と Settings への反映にはサービスの再起動が必要。ネイティブメニューとバージョン情報はバックグラウンドで更新）および候補レイアウト（**縦並び** / **横並び**）。
- **開発者オプション**: 詳細ログを即時切り替え、`%LOCALAPPDATA%/Fcitx5/logs` を開きます。Core はエンジンログを常時保存し（既定 `Info`、詳細ログ有効時は `Debug`）、詳細ログ有効時には Core・TSF・Settings の構造化 JSON Lines ログを追加記録します。ログは 8 MiB および UTC 日付変更時にローテーションされ、7 日間保持・ディレクトリ合計 512 MiB 上限で管理されます。詳細は[開発者ログ設計](docs/windows-developer-logging-design.md)を参照してください。
- **プラグイン**: 検出されたアドオンの状態確認と個人用プラグインフォルダーの起動。
- **ショートカット**: 入力メソッドの巡回切り替えおよび 1〜5 番目への直接切り替え設定。

### 辞書と特殊な実行環境

- **サードパーティ製ピンイン辞書**: 互換性のある Fcitx5 / libime バイナリ `.dict` ファイルを `%APPDATA%/Fcitx5/pinyin/dictionaries` に配置すると自動的に読み込まれます。詳細は[サードパーティ辞書](docs/pinyin.md#third-party-dictionaries)を参照してください。
- **Sandboxie**: [Sandboxie ガイド](docs/windows-sandboxie-design.md)に従って明示的なパイプ許可ルールを設定してください。ホスト側の Core が事前に起動している必要があります。
- **モダンアプリ / AppContainer**: CI / Nightly インストーラーは既定でモダン TSF カテゴリを宣言します。ローカルビルドでは `-ModernApps` を指定して再登録してください。ホスト側 Core の事前起動が必要で、AppContainer メニューでは設定のみが公開されます。詳細は[モダンアプリ対応](docs/windows-modern-apps-design.md)を参照してください。
- **管理者権限アプリ（UAC）**: 実行中の通常権限ホスト Core に接続し、サービス操作をホストへ委譲します。詳細は[UAC IPC 設計](docs/windows-uac-ipc-design.md)を参照してください。

## ドキュメント

- [ビルド・デプロイ・検証](docs/build.md)
- [Windows インストーラー](docs/windows-installer-design.md)
- [遅延再起動アップグレードと復旧](docs/windows-reboot-upgrade-design.md)
- [x86 TSF ビルド・デプロイ・検証](docs/windows-x86-tsf-design.md)
- [UAC 昇格クライアントとホストサービス制御](docs/windows-uac-ipc-design.md)
- [入力メソッドアーキテクチャと個別ビルド](docs/pinyin.md)
- [独立した Windows 入力メソッドプラグインと SDK ビルド](docs/windows-plugins.md)
- [Windows 設定アプリ設計](docs/windows-settings-design.md)
- [入力メソッドステータスメニューと Rime スキーム](docs/windows-status-menu-design.md)
- [候補ウィンドウ描画](docs/windows-candidate-rendering.md)
- [UI 言語とローカライズ](docs/windows-ui-localization-design.md)
- [Sandboxie ホスト共有と設定](docs/windows-sandboxie-design.md)
- [モダンアプリ転送と TSF UIElement](docs/windows-modern-apps-design.md)
- [開発者ログと Settings 診断](docs/windows-developer-logging-design.md)

## クレジット

- [fcitx5](https://github.com/fcitx/fcitx5): LGPL-2.1-or-later
- [weasel](https://github.com/rime/weasel): GPL-3.0-only
