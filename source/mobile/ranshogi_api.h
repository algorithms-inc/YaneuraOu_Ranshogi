// 乱将棋(6x6)エンジンをアプリに組み込むためのC API。
//
// Unity(iOS/Android)から P/Invoke で直接呼ぶことを想定している。
// USIの標準入出力ループは使わず、関数呼び出しで思考させる。
//
// スレッド安全性: エンジンの状態はグローバルに1つだけ。
//   rs_bestmove() を複数スレッドから同時に呼んではいけない。
//   (Unity側では思考用のワーカースレッド1本から呼ぶこと)

#ifndef RANSHOGI_API_H_INCLUDED
#define RANSHOGI_API_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

// エラーコード
#define RS_OK                 0
#define RS_ERR_ALREADY_INIT  -1
#define RS_ERR_NOT_INIT      -2
#define RS_ERR_EVAL_LOAD     -3
#define RS_ERR_BAD_ARG       -4
#define RS_ERR_NO_RESULT     -5
#define RS_ERR_BUFFER        -6
// 駒の枚数が標準を超える局面(以前のルールの乱将棋)。エンジンでは扱えないので探索しない。
#define RS_ERR_UNSUPPORTED_POSITION -7

// エンジンを初期化して評価関数を読み込む。起動後1回だけ呼ぶ。
//   eval_dir : nn.bin の入っているディレクトリの絶対パス
//   threads  : 探索スレッド数 (モバイルは1〜2を推奨)
//   hash_mb  : 置換表サイズ(MB)。16〜32程度で十分。
// 評価関数の読み込みに数百ms〜かかるので、ゲーム開始前に済ませておくこと。
int rs_init(const char* eval_dir, int threads, int hash_mb);

// USIのsetoptionと同じ。rs_init()の後、rs_bestmove()の前に呼ぶ。
int rs_set_option(const char* name, const char* value);

// 現局面の最善手を求める。
//   position_args : USIの"position"コマンドの"position "以降の文字列。
//                   例) "sfen 1psl1k/GLn+Sbg/2Nppp/1G+p2P/p1p2+P/1K2Rs b B 1"
//                       "sfen <...> moves 3c3d 2b2c"
//                       "startpos moves 3c3d"
//   depth         : 探索深さ。0以下なら深さ無制限(movetime_msで打ち切る)。
//   movetime_ms   : 思考時間の上限(ms)。0以下なら時間無制限(depthで打ち切る)。
//                   depth と movetime_ms の両方が0以下ならdepth 8扱い。
//   out_move      : "7g7f" 形式の指し手が入る。合法手が無い場合は "resign"。
// 戻り値: RS_OK または負のエラーコード。
int rs_bestmove(const char* position_args, int depth, int movetime_ms,
                char* out_move, int out_move_len);

// rs_bestmove() と同じだが、負けが確定したあとの王手ラッシュを避けられる。
//   avoid_checks_when_lost : 0以外にすると、探索の結果が「負け確定」(評価値が
//                            -VALUE_SUPERIOR 以下。詰まされる筋を含む) で、かつ
//                            最善手が王手のときに、王手以外の合法手だけで探索し直す。
//                            王手以外の手が無いときは、そのまま最善手を返す。
//                            勝敗が決まっていない間は普通に探索するので、強さは変わらない。
// 戻り値: RS_OK または負のエラーコード。
int rs_bestmove_ex(const char* position_args, int depth, int movetime_ms,
                   int avoid_checks_when_lost, char* out_move, int out_move_len);

// 直近のrs_bestmove()の評価値(手番側から見た値, 歩=100程度のスケール)。
int rs_last_score(void);

// 直近のrs_bestmove()で到達した深さ。
int rs_last_depth(void);

// 直近のrs_bestmove()のPV情報("info ... pv ..."形式)。次のrs_bestmove()まで有効。
const char* rs_last_pv(void);

// 思考中のrs_bestmove()を打ち切らせる。rs_bestmove()はその時点までの最善手を返して戻る。
// rs_bestmove()を呼んでいるのとは別のスレッドから呼ぶ(待ったなどで思考の結果が要らなくなったとき)。
// 思考していないときに呼んでも何もしない(次のrs_bestmove()は普通に思考する)。
void rs_stop(void);

// エンジンを停止してスレッドを片付ける。アプリ終了時に呼ぶ(呼ばなくても可)。
void rs_quit(void);

// エンジンのバージョン文字列。
const char* rs_version(void);

#ifdef __cplusplus
}
#endif

#endif // ndef RANSHOGI_API_H_INCLUDED
