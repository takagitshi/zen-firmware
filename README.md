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
ファームウェア側加速があります。低速は完全な1.0倍、中速からsmoothstepで滑らかに
立ち上がり、112 counts/15ms以上で2.0倍を上限とします。X/Yには同じ倍率を適用し、
加速分の端数を保持します。完了したX/Yベクトルから次のレポートの倍率を決めるため、
最大15msの応答遅延と引き換えに、元のPMW3610 device、input queue、sync、15ms周期を
そのまま維持します。倍率だけを上昇50%・下降75%で平滑化し、低速域または60msの
無操作後は次のレポートを即座に1.0倍へ戻します。X/Y座標自体は平均しません。

調整値は
`snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.overlay` の
`pointer_acceleration` に集約しています。

| 項目 | 初期値 | 意味 |
| --- | ---: | --- |
| `base-multiplier-milli` | 1000 | 低速域の倍率1.0倍 |
| `takeoff-speed` | 20 | この正規化ベクトル速度以下は低速倍率 |
| `full-speed` | 112 | この速度以上で最大倍率 |
| `max-multiplier-milli` | 2000 | 最大2.0倍 |
| `attack-smoothing-milli` | 500 | 倍率上昇時に差分の50%を反映 |
| `release-smoothing-milli` | 750 | 倍率下降時に差分の75%を反映 |
| `reference-interval-ms` | 15 | 速度判定の基準レポート間隔 |
| `idle-reset-ms` | 60 | 低速倍率へ戻す無操作時間 |

低速感度だけを600 CPI相当に下げて比較する場合は、センサーのCPI 800を保持したまま
`base-multiplier-milli`を`750`にします。センサー自体を600 CPIへ下げるよりraw分解能を
維持できますが、HID出力は整数のため、初期値では原因を分けて評価しやすい1.0倍を
優先します。

元のリニアなPointer 1.0xへ戻す場合は、
`snippets/input-listener-right-pmw3610/input-listener-right-pmw3610.conf` の
`CONFIG_ZEN_POINTER_ACCELERATION=y` を `n` に変更します。CPI 800、Scroll 1/40、
Gesture、AML、レポート周期はこの設定の対象外です。

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
