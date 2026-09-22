# ZEN ファームウェア

ZEN用のファームウェアを作成するためのリポジトリです。

GitHubに慣れていない方でも、リポジトリをforkして、Keymap Editorでキー配置を編集し、GitHub Actionsでファームウェアを作成できます。

## はじめに読むもの

詳しい手順は以下のガイドにまとめています。

- [ZEN ファームウェア案内](https://e24-gh.github.io/zen/firmware.html)

## このリポジトリでできること

- ZEN用ファームウェアのビルド
- Keymap Editorでのキー配置編集
- GitHub ActionsからのUF2ファイル作成

## 作成されるファームウェア

GitHub Actionsで作成される `firmware` というzipファイルには、以下のUF2ファイルが含まれます。

- 設定リセット用UF2ファイル
- `zen_right_trackball_pmw3610_central.uf2`
- `zen_right_trackball_paw3222_central.uf2`
- `zen_right_trackpad_central.uf2`
- `zen_left_peripheral.uf2`
- `zen_left_trackball_pmw3610_peripheral.uf2`
- `zen_left_trackball_paw3222_peripheral.uf2`
- `zen_left_trackpad_peripheral.uf2`

通常使用するファイルは以下です。

- 右手: `zen_right_trackball_pmw3610_central.uf2`
- 左手: `zen_left_peripheral.uf2`

右手にPAW3222やTrackpadを使用する場合は、それぞれ名前に対応する右手用UF2ファイルを使用します。

左手にTrackballやTrackpadを使用する場合は、以下を使用します。

- 左手PMW3610: `zen_left_trackball_pmw3610_peripheral.uf2`
- 左手PAW3222: `zen_left_trackball_paw3222_peripheral.uf2`
- 左手Trackpad: `zen_left_trackpad_peripheral.uf2`

標準構成では、右手はPMW3610用、左手は `zen_left_peripheral.uf2` を使用してください。

無線接続がうまくいかない場合や、別のファームウェアから切り替える場合は、先に設定リセット用のUF2ファイルを使います。

## 最短手順

1. このリポジトリを自分のGitHubアカウントへforkします
2. Keymap Editorでforkしたリポジトリを開きます
3. 必要に応じてキー配置を編集して保存します
4. GitHub Actionsでファームウェアがbuildされるのを待ちます
5. `firmware` のダウンロードボタンからzipファイルをダウンロードします
6. 右手と左手の対応コントローラへUF2ファイルを書き込みます
7. PCやMacの接続設定から `ZEN` として接続します

## 対象

このリポジトリは、PMW3610トラックボール版を標準構成とし、左右それぞれでPAW3222およびTrackpadを使用する構成にも対応しています。

## PMW3610ポインター加速

標準の右PMW3610構成には、macOS側のポインター加速をOFFにして使うことを想定した
ファームウェア側加速があります。センサーは1200 CPI、低速出力は0.5倍
（600 CPI相当）とし、中速から最大3.0倍へ穏やかに近づけます。X/Yベクトル速度から
同じフレームの共通倍率を求めるため、方向比を崩さず、高速フレームの倍率が次の低速
フレームへ残りません。短い処理間隔を高速移動と誤認しないよう、15ms未満の間隔では
速度を水増ししません。

カーブは倍率そのものではなく、出力速度の傾き（gain）が連続になるように構成して
います。これにより、カーブ途中で実効的な加速が設定上限を超える現象を防ぎます。
座標の移動平均や時間方向の倍率平滑化は行わず、入力遅延や高速から低速へ戻した際の
追従遅れを加えません。X/Yそれぞれの100万分率の端数は保持し、方向反転または60msの
無操作で破棄します。送信待ちで複数周期分が蓄積した場合だけ実収集時間で速度を補正し、
無操作時間そのものは次の高速操作を遅く判定しません。既存のPMW3610 device、input
queue、sync、15ms周期は維持します。
Scrollレイヤー2とGestureレイヤー3では加速を迂回し、従来のraw deltaを使います。

調整値は
`snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.overlay` の
`&pointing_device` の `zen-pointer-acceleration-*` に集約しています。

| 項目 | 初期値 | 意味 |
| --- | ---: | --- |
| `zen-pointer-acceleration-base-gain-milli` | 500 | 低速域0.5倍（600 CPI相当） |
| `zen-pointer-acceleration-takeoff-speed` | 32 | この正規化ベクトル速度以下は低速倍率 |
| `zen-pointer-acceleration-full-speed` | 160 | 出力速度の傾きが最大gainへ到達する速度 |
| `zen-pointer-acceleration-max-gain-milli` | 3000 | 出力速度の傾きと倍率の上限3.0倍 |
| `zen-pointer-acceleration-reference-interval-ms` | 15 | 速度判定の基準レポート間隔 |
| `zen-pointer-acceleration-idle-reset-ms` | 60 | 端数を破棄する無操作時間 |

`full-speed` では倍率が急に3.0倍になるわけではありません。初期値では倍率は速度160で
約1.5倍、320で約2.25倍となり、その後も連続的に3.0倍へ近づきます。まず速度感だけを
変える場合は `base-gain-milli`、`takeoff-speed`、`full-speed`、`max-gain-milli` の順に
一項目ずつ調整してください。

元のリニアなPointer 1.0xへ戻す場合は、
`snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.conf` の
`CONFIG_ZEN_POINTER_ACCELERATION=y` を `n` に変更します。ScrollはCPI増加を相殺する
1/60（旧800 CPI・1/40と同じ物理感度）です。PMW3610 Gestureのしきい値は200、
AMLとレポート周期は従来どおりです。

## うまく接続できない場合

無線接続がうまくいかない場合は、以下を試してください。

1. PCやMacの接続設定から `ZEN` を削除します
2. 設定リセット用のUF2ファイルを書き込みます
3. USBを外します
4. ZENの電源を入れます
5. もう一度通常ファームウェアを書き込みます
6. 接続設定から `ZEN` を再接続します

## ライセンス

MIT
