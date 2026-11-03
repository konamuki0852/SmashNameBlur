# BlurName（相手名マスク）

スマブラSP のオンライン対戦を OBS で録画・配信するとき、**相手のニックネームを自動でモザイクにする** OBS Studio の
フィルタープラグインです（Windows）。録画にも配信にもリアルタイム（60fps／30fps）で効きます。

- **隠す場所**: VS 画面の名札・バトル中の HUD・リザルト画面（位置の決まった 3 か所）と、キャラと一緒に動く**頭上タグ**
- **しくみ**: 小さな画像認識のモデル 2 つ（画面の種類の判定・頭上タグの検出）を GPU（DirectML）か CPU で動かす。
  映像を約 0.13 秒（60fps）遅らせて先の映像まで調べ、名前が出る少し前から隠し始める。同じソースの音声は OBS が自動で合わせる
- **プライバシー**: 処理はすべて OBS の中で完結し、映像も利用状況も外へ送らない。ログに名前・画像を書かない

> 見逃し（名前が見えてしまうこと）を出さないことを優先しています。名前の近くや画面の切り替わりの前後に、
> 少し余計にモザイクがかかることがあります。100% を保証するものではないので、大事な動画は公開前に見返してください。

## 必要なもの

| 項目 | 内容 |
|---|---|
| OS | Windows 10（1903 以降）／11、64 ビット |
| OBS | OBS Studio 31.0.1 以降の 31 系（32 系以降は動作を確かめていません） |
| GPU | DirectX 12 対応（推奨。無ければ CPU で動くが、60fps では追いつかないことがある） |
| 映像 | キャプチャボードの映像を「映像キャプチャデバイス」で 1920×1080 のまま取り込む |
| ゲーム | スマブラSP（日本語表示）、オンラインの 1 対 1（自分が 1P・相手が 2P） |

## 入れ方と使い方

1. OBS を閉じ、配布物（`blur-name-<版>-windows-x64.zip`）を展開する
2. `blur-name` フォルダを、フォルダごと `%ProgramData%\obs-studio\plugins\` に置く（`obs-studio`・`plugins` のフォルダが無ければ作る）
3. OBS を起動し、映像キャプチャデバイスの「フィルタ」を開き、**「音声/映像フィルタ」の＋** から「相手名マスク」を追加する
   （「エフェクトフィルタ」の＋には出ません）
4. 右側に「遅延時間」が出て、いちばん上に黄色や赤の文が出ていなければ完了。以後は何もしなくても隠れます

詳しい手順・状態の文の意味・困ったときは、配布物の `取扱説明書/index.html`（このリポジトリでは
[docs/取扱説明書/index.html](docs/取扱説明書/index.html)）を見てください。

## ソースからビルドする

Visual Studio 2022（C++ によるデスクトップ開発）・Windows 11 SDK 10.0.22621・CMake 3.28 以上が要ります。

```powershell
cd plugin
cmake --preset windows-x64          # 初回は OBS のソース・依存物・ONNX Runtime を版とハッシュを固定して取得（約 2 GB）
cmake --build --preset windows-x64  # 警告はエラー扱い
ctest --preset windows-x64          # 単体テスト・推論の結合テスト
cmake --install build_x64 --config RelWithDebInfo --prefix <出力先>   # <出力先>/blur-name にプラグインのフォルダを作る
```

## ライセンス

- 本体: GNU General Public License v2.0 以降（[LICENSE](LICENSE)。OBS のプラグインのため）。Copyright (C) 2026 Konamuki
- 同梱の ONNX Runtime 1.24.4（DirectML 版）: MIT License。第三者の利用条件は配布物の `licenses/onnxruntime/` にあります
- DirectML は Windows に含まれるものを使い、同梱しません
- モデル（`plugin/data/models/`）は本プロジェクトで学習した重みです。学習に使った録画・画像は含みません

非公式のファンツールです。任天堂・ソラ・バンダイナムコスタジオとは関係ありません。「大乱闘スマッシュブラザーズ SPECIAL」は任天堂の商標です。

## 問い合わせ

不具合の報告・要望は、このリポジトリの [Issues](https://github.com/konamuki0852/SmashNameBlur/issues) へどうぞ。

---

## English (summary)

An OBS Studio filter plugin for Windows that automatically mosaics the opponent's nickname in Super Smash Bros.
Ultimate online matches, in real time for both recording and streaming. It masks the VS screen name plate, the
in-battle HUD, the results screen and the moving overhead tag, using two small on-device models (DirectML or CPU).
The video is delayed by about 0.13 s (at 60 fps) so that masking can start slightly before a name appears; audio
of the same source is kept in sync by OBS. Nothing is sent over the network.

Install: close OBS, copy the `blur-name` folder into `%ProgramData%\obs-studio\plugins\`, then add
"BlurName" from the **Audio/Video Filters** "+" of your Video Capture Device. Requires OBS Studio 31.0.1
or later (31.x) and a 1920×1080 capture. License: GPL-2.0-or-later (bundled ONNX Runtime: MIT).
