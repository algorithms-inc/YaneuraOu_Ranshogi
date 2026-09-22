# 乱将棋版やねうら王

6×6盤の将棋の変種「乱将棋」を指せるようにした、[やねうら王](https://github.com/yaneurao/YaneuraOu)の改造版です。
スマートフォンアプリ「乱将棋」の思考エンジンとして使っています。

枝分かれ元: yaneurao/YaneuraOu の `599378d4`

## ライセンス

**GPLv3** です。元のやねうら王プロジェクト（Stockfish由来のコードを多く含みます）と同じ条件に従います。
詳しくは [Copying.txt](Copying.txt) と、本家の README のライセンスの項を参照してください。

## 乱将棋とは

- 6×6の盤を使う
- 駒の初期配置が対局ごとにランダムに決まる
- 成れるのは相手側3段
- 入玉（トライルール）で勝ちになる

## 主な変更点

| 変更 | 主なファイル |
|---|---|
| 盤を6×6にする（マス・段・筋・Bitboard・Zobrist・評価関数の駒リスト） | `types.h`, `bitboard.cpp/h`, `position.cpp/h`, `config.h` |
| 成れる段を3段にする。桂・香・歩が動けなくなる段での成りの強制 | `types.h`, `movegen.cpp`, `position.cpp` |
| 駒の枚数が標準を超える局面を弾く（評価関数の駒リストがあふれて落ちるため） | `usi.cpp` |
| アプリ組み込み用のC API（`rs_init` / `rs_bestmove` など）とUnity用のバインディング | `mobile/` |
| iOS・Android・macOS向けのビルドターゲット | `Makefile` |
| 局面の生成コマンド。対局開始局面(`grs`)と、勝ち切れ問題用の局面(`gkp`) | `usi.cpp` |
| 負けが決まった局面で無意味な王手を続けないようにする | `engine/yaneuraou-engine/yaneuraou-search.cpp` |

評価関数のファイル（`nn.bin`）と、その学習に使ったスクリプト・データはこのリポジトリには含めていません。

## ビルド

```bash
cd source

# 通常(ホスト用の実行ファイル)
make -j8 normal TARGET=YaneuraOu-kachikire OBJDIR=../obj

# iOS実機(arm64)  → mobile/build/ios-device/libranshogi.a
make -j8 ios

# Android実機(arm64-v8a)  → mobile/build/android-arm64/libranshogi.so
# NDKはUnity同梱のものを既定にしています。別のNDKなら NDK_DIR を渡してください
make -j8 android

# macOS(Unityエディタ用の動的ライブラリ) → mobile/build/macos/ranshogi.bundle
make -j8 mac
```

組み込み方は [source/mobile/README.md](source/mobile/README.md) に書いてあります。

## 局面の生成コマンド

```
# 対局開始局面を作る
grs depth 8 nodes 200000 score 200 tries 1000

# 勝ち切れ問題の局面と、その正解手順を作る
gkp depth 12 verify_depth 16 nodes 500000 verify_nodes 5000000 score 1200 margin 100 tries 3000
```

`gkp` は `kachikire sfen <sfen> score <評価値> player <b|w> pv <正解手順>` の形で出力します。
`pv` は初期局面から実際に並べ直して、指せる手だけを出しています。
