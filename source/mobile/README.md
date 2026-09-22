# 乱将棋エンジンのアプリ組み込み (iOS / Android)

USIの標準入出力ループを使わず、C APIの関数呼び出しでエンジンを動かすための一式。

```
mobile/
  ranshogi_api.h        C API (Unityから呼ぶ関数)
  ranshogi_api.cpp      その実装
  test_main.cpp         ホスト(Mac)での動作確認用テスト。ライブラリには含まれない
  unity/
    RanshogiEngine.cs   Unity側のP/Invokeバインディング
```

## 1. ビルド

### ホスト(Mac)で動作確認

```
make -j8 libtest TARGET_CPU=OTHER OBJDIR=../obj_nosse
./ranshogi_apitest evalsave/d12_rl_v1_final 8
```

`TARGET_CPU=OTHER` は `-DNO_SSE` を立てる。実機(ARM)と同じSIMD無しのコードパスになるので、
実機に持っていく前の確認はこの構成で行うこと。

### iOS実機(arm64)

```
make -j8 ios
# → mobile/build/ios-device/libranshogi.a  (arm64, iOS 13.0+)
```

ビルドフラグで押さえておくべき点が3つある。

| フラグ | 理由 |
|---|---|
| `TARGET_CPU=OTHER` (`-DNO_SSE`) | ARMにはSSEが無い |
| `-DIS_64BIT` | **必須**。config.hの64bit判定が `__x86_64__` 決め打ちで、これが無いとarm64が32bit環境と誤判定され、EvalHashなどが黙って無効化される |
| `-DFOR_TOURNAMENT` | 学習機能・テストコマンド・GlobalOptionsを落とす。アプリには不要 |

## 2. Unityへの組み込み (iOS)

```
# ライブラリ
cp mobile/build/ios-device/libranshogi.a  <UnityProject>/Assets/Plugins/iOS/

# 評価関数 (13.6MB)
mkdir -p <UnityProject>/Assets/StreamingAssets/eval
cp evalsave/d12_rl_v1_final/nn.bin        <UnityProject>/Assets/StreamingAssets/eval/

# バインディング
cp mobile/unity/RanshogiEngine.cs         <UnityProject>/Assets/Scripts/
```

- `Assets/Plugins/iOS/` に置いた `.a` はUnityが生成するXcodeプロジェクトに自動でリンクされる。
  Inspectorのプラットフォーム設定が iOS になっていることだけ確認する。
- iOSでは `Application.streamingAssetsPath` をそのままファイルパスとして読めるので、
  nn.binの展開は不要。Androidはapk内を直接開けないので `RanshogiEngine.cs` が
  初回起動時に `persistentDataPath` へコピーする。
- 評価関数のファイル名は `nn.bin` 固定 (エンジンの `EvalFile` の既定値)。
  `rs_init()` に渡すのは**ファイルではなくディレクトリ**のパス。

### 使い方

```csharp
await RanshogiEngine.InitAsync();                    // 起動時に1回 (Mac実測 約0.3秒)

string move = await RanshogiEngine.BestMoveAsync(sfen, RanshogiEngine.Level.Normal);
// move: "3c3d" / "B*3b" / 合法手が無ければ "resign"
int score = RanshogiEngine.LastScore;                // 手番側から見た評価値
```

## 3. 難易度と思考時間

Intel Mac / SIMD無し / 1スレッドでの実測 (乱将棋の中盤局面5つの平均)。
実機はこれより数倍遅いと見ておく。

| Level | depth | 1手あたり |
|---|---|---|
| Easy   |  4 |    ~5 ms |
| Normal |  8 |   ~25 ms |
| Hard   | 12 |  ~500 ms |
| Max    | 16 |  ~2.6 s  |

depth 8 は教師データ生成・対局評価に使ってきた条件と同じ。
`BestMoveAsync(..., movetimeMs: 1000)` のように時間上限も併用できる。

## 4. 制約

- **エンジンの状態はプロセスにグローバルで1つ**。`rs_bestmove()` を複数スレッドから
  同時に呼んではいけない。`RanshogiEngine.cs` は内部のセマフォで直列化している。
- `rs_init()` はプロセスにつき1回。
- 定跡は使わない (`BookFile=no_book`)。
