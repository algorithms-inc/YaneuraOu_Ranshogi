#include "types.h"
#include "usi.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "misc.h"
#include "testcmd/unit_test.h"
#include "mate/mate.h"

// 局面(sfen の "盤面 手番 持ち駒 ...")の駒の枚数が、エンジンの扱える範囲かを返す。
// 盤上と持ち駒、先後の合計を駒の種類ごとに数え(成り駒は元の駒として数える)、標準の枚数を超えたら false。
// 以前のルールの乱将棋の局面には飛車3枚などがあり、評価関数の駒リスト(EvalList)があふれて落ちるため。
bool is_supported_ranshogi_sfen(const std::string& sfen)
{
	std::istringstream ss(sfen);
	std::string board, side, hand;
	ss >> board >> side >> hand;

	int counts[128] = {};
	auto add = [&](char c, int n) {
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
		if (c >= 'A' && c <= 'Z') counts[static_cast<int>(c)] += n;
	};
	for (char c : board)
		add(c, 1);
	int n = 0;
	for (char c : hand)
	{
		if (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); continue; }
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { add(c, n == 0 ? 1 : n); n = 0; }
	}

	static const std::pair<char, int> limits[] = {
		{'P', 18}, {'L', 4}, {'N', 4}, {'S', 4}, {'G', 4}, {'B', 2}, {'R', 2}, {'K', 2} };
	for (const auto& limit : limits)
		if (counts[static_cast<int>(limit.first)] > limit.second)
			return false;
	return true;
}

#if !defined(YANEURAOU_ENGINE_DEEP)
#include "tt.h"
#endif

#if defined(__EMSCRIPTEN__)
// yaneuraou.wasm
#include <emscripten.h>
#endif

#include <sstream>
#include <queue>
#include <random>

using namespace std;

// ----------------------------------
//      USI拡張コマンド "test"
// ----------------------------------

#if defined(ENABLE_TEST_CMD)

// USI拡張コマンドのうち、開発上のテスト関係のコマンド。
// 思考エンジンの実行には関係しない。

namespace Test
{
	// 通常のテスト用コマンド。コマンドを処理した時 trueが返る。
	bool normal_test_cmd(Position& pos, std::istringstream& is, const std::string& token);

	// 詰み関係のテスト用コマンド。コマンドを処理した時 trueが返る。
	bool mate_test_cmd(Position& pos, std::istringstream& is, const std::string& token);

	void test_cmd(Position& pos, std::istringstream& is)
	{
		// 探索をするかも知れないので初期化しておく。
		is_ready();

		std::string token;
		is >> token;

		// デザパタのDecoratorの呼び出しみたいな感じで書いていく。

		// 通常のテスト用コマンド
		if (normal_test_cmd(pos, is, token))
			return;

		// 詰み関係のテスト用コマンド
		if (mate_test_cmd(pos,is,token))
			return;

		sync_cout << "info string Error! : unknown command = " << token << sync_endl;
	}
}

#endif // defined(ENABLE_TEST_CMD)

//
// あとで整理する
//


// ユーザーの実験用に開放している関数。
// USI拡張コマンドで"user"と入力するとこの関数が呼び出される。
// "user"コマンドの後続に指定されている文字列はisのほうに渡される。
void user_test(Position& pos, std::istringstream& is);

#if defined(ENABLE_TEST_CMD)
	void generate_moves_cmd(Position& pos);
#endif

#if defined(USE_MATE_DFPN)
// "mate"コマンド
void mate_cmd(Position& pos, istream& is);
#endif

// ----------------------------------
//      USI拡張コマンド "makebook"
// ----------------------------------

// 定跡を作るコマンド
#if defined (ENABLE_MAKEBOOK_CMD) && (defined(EVAL_LEARN) || defined(YANEURAOU_ENGINE_DEEP))
namespace Book { extern void makebook_cmd(Position& pos, istringstream& is); }
#endif

// ----------------------------------
//      USI拡張コマンド "learn"
// ----------------------------------

// 棋譜を自動生成するコマンド
#if defined (EVAL_LEARN)
namespace Learner
{
  // 教師局面の自動生成
  void gen_sfen(Position& pos, istringstream& is);

  // 生成した棋譜からの学習
  void learn(Position& pos, istringstream& is);

#if defined(GENSFEN2019)
  // 開発中の教師局面の自動生成コマンド
  void gen_sfen2019(Position& pos, istringstream& is);
#endif

  // 読み筋と評価値のペア。Learner::search(),Learner::qsearch()が返す。
  typedef std::pair<Value, std::vector<Move> > ValueAndPV;

  ValueAndPV qsearch(Position& pos);
  ValueAndPV search(Position& pos, int depth_, size_t multiPV = 1 , u64 nodesLimit = 0 );

}

namespace MyLearner
{
  typedef std::pair<Value, std::vector<Move> > ValueAndPV;

  ValueAndPV qsearch(Position& pos);

}

#endif

// ----------------------------------
//      USI拡張コマンド "bench"
// ----------------------------------

// "bench"コマンドは、"test"コマンド群とは別。常に呼び出せるようにしてある。
extern void bench_cmd(Position& pos, istringstream& is);


// "gameover"コマンドに対するハンドラ
#if defined(USE_GAMEOVER_HANDLER)
void gameover_handler(const string& cmd);
#endif

// ----------------------------------
//   USI拡張コマンド "cluster"
//     やねうら王 The Cluster
// ----------------------------------

#if defined(USE_YO_CLUSTER)
#if defined(YANEURAOU_ENGINE_DEEP) || defined(YANEURAOU_ENGINE_NNUE)
namespace YaneuraouTheCluster
{
	// cluster時のUSIメッセージの処理ループ
	void cluster_usi_loop(Position& pos, std::istringstream& is);
}
#endif
#endif

namespace USI
{
	// --------------------
	//    読み筋の出力
	// --------------------

	// depth : iteration深さ
	std::string pv(const Position& pos, Depth depth, Value alpha, Value beta)
	{
#if defined(YANEURAOU_ENGINE_DEEP)
		// ふかうら王では、この関数呼び出さないからまるっと要らない。

		return string();
#else
		std::stringstream ss;

		TimePoint elapsed = Time.elapsed() + 1;
#if defined(__EMSCRIPTEN__)
		// yaneuraou.wasm
		// Time.elapsed()が-1を返すことがある
		// https://github.com/lichess-org/stockfish.wasm/issues/5
		// https://github.com/lichess-org/stockfish.wasm/commit/4f591186650ab9729705dc01dec1b2d099cd5e29
		elapsed = std::max(elapsed, TimePoint(1));
#endif
		const auto& rootMoves = pos.this_thread()->rootMoves;
		size_t pvIdx = pos.this_thread()->pvIdx;
		size_t multiPV = std::min((size_t)Options["MultiPV"], rootMoves.size());

		uint64_t nodes_searched = Threads.nodes_searched();

		// MultiPVでは上位N個の候補手と読み筋を出力する必要がある。
		for (size_t i = 0; i < multiPV; ++i)
		{
			// この指し手のpvの更新が終わっているのか
			bool updated = rootMoves[i].score != -VALUE_INFINITE;

			if (depth == 1 && !updated && i > 0)
				continue;

			// 1より小さな探索depthで出力しない。
			Depth d = updated ? depth : std::max(1, depth - 1);
			Value v = updated ? rootMoves[i].score : rootMoves[i].previousScore;

			// multi pv時、例えば3個目の候補手までしか評価が終わっていなくて(PVIdx==2)、このとき、
			// 3,4,5個目にあるのは前回のiterationまでずっと評価されていなかった指し手であるような場合に、
			// これらのpreviousScoreが-VALUE_INFINITE(未初期化状態)でありうる。
			// (multi pv状態で"go infinite"～"stop"を繰り返すとこの現象が発生する。おそらく置換表にhitしまくる結果ではないかと思う。)
			if (v == -VALUE_INFINITE)
				v = VALUE_ZERO; // この場合でもとりあえず出力は行う。

			//bool tb = TB::RootInTB && abs(v) < VALUE_MATE_IN_MAX_PLY;
			//v = tb ? rootMoves[i].tbScore : v;

			if (ss.rdbuf()->in_avail()) // 1行目でないなら連結のための改行を出力
				ss << endl;

			ss  << "info"
				<< " depth "    << d
				<< " seldepth " << rootMoves[i].selDepth
#if defined(USE_PIECE_VALUE)
				<< " score "    << USI::value(v)
#endif
				;

			// これが現在探索中の指し手であるなら、それがlowerboundかupperboundかは表示させる
			if (i == pvIdx)
				ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

			// 将棋所はmultipvに対応していないが、とりあえず出力はしておく。
			if (multiPV > 1)
				ss << " multipv " << (i + 1);

			ss << " nodes " << nodes_searched
			   << " nps "   << nodes_searched * 1000 / elapsed;

			// 置換表使用率。経過時間が短いときは意味をなさないので出力しない。
			if (elapsed > 1000)
				ss << " hashfull " << TT.hashfull();

			ss << " time " << elapsed
			   << " pv";


			// PV配列からPVを出力する。
			// ※　USIの"info"で読み筋を出力するときは"pv"サブコマンドはサブコマンドの一番最後にしなければならない。

			auto out_array_pv = [&]()
			{
				for (Move m : rootMoves[i].pv)
					ss << " " << m;
			};

			// 置換表からPVをかき集めてきてPVを出力する。
			auto out_tt_pv = [&]()
			{
				auto pos_ = const_cast<Position*>(&pos);
				Move moves[MAX_PLY + 1];
				StateInfo si[MAX_PLY];
				int ply = 0;

				while ( ply < MAX_PLY )
				{
					// 千日手はそこで終了。ただし初手はPVを出力。
					// 千日手がベストのとき、置換表を更新していないので
					// 置換表上はMOVE_NONEがベストの指し手になっている可能性があるので早めに検出する。
					auto rep = pos.is_repetition(ply);
					if (rep != REPETITION_NONE && ply >= 1)
					{
						// 千日手でPVを打ち切るときはその旨を表示
						ss << " " << rep;
						break;
					}

					Move m;

					// まず、rootMoves.pvを辿れるところまで辿る。
					// rootMoves[i].pv[0]は宣言勝ちの指し手(MOVE_WIN)の可能性があるので注意。
					if (ply < rootMoves[i].pv.size())
						m = rootMoves[i].pv[ply];
					else
					{
						// 次の手を置換表から拾う。
						// ただし置換表を破壊されるとbenchコマンドの時にシングルスレッドなのに探索内容の同一性が保証されなくて
						// 困るのでread_probe()を用いる。
						bool found;
						auto* tte = TT.read_probe(pos.state()->hash_key(), found);

						// 置換表になかった
						if (!found)
							break;

						m = pos.to_move(tte->move());

						// leaf nodeはわりと高い確率でMOVE_NONE
						if (m == MOVE_NONE)
							break;

						// 置換表にはpsudo_legalではない指し手が含まれるのでそれを弾く。
						// 宣言勝ちでないならこれが合法手であるかのチェックが必要。
						if (m != MOVE_WIN)
						{
							// 歩の不成が読み筋に含まれていようともそれは表示できなくてはならないので
							// pseudo_legal_s<true>()を用いて判定。
							if (!(pos.pseudo_legal_s<true>(m) && pos.legal(m)))
								break;
						}
					}

#if defined (USE_ENTERING_KING_WIN)
					// 宣言勝ちである
					if (m == MOVE_WIN)
					{
						// これが合法手であるなら宣言勝ちであると出力。
						if (pos.DeclarationWin() != MOVE_NONE)
							ss << " " << MOVE_WIN;

						break;
					}
#endif

					moves[ply] = m;
					ss << " " << m;

					pos_->do_move(m, si[ply]);
					++ply;
				}
				while (ply > 0)
					pos_->undo_move(moves[--ply]);
			};

			// 検討用のPVを出力するモードなら、置換表からPVをかき集める。
			// (そうしないとMultiPV時にPVが欠損することがあるようだ)
			// fail-highのときにもPVを更新しているのが問題ではなさそう。
			// Stockfish側の何らかのバグかも。
			if (Search::Limits.consideration_mode)
				out_tt_pv();
			else
				out_array_pv();
		}

		return ss.str();
#endif // defined(YANEURAOU_ENGINE_DEEP)
	}
}

// --------------------
// USI関係のコマンド処理
// --------------------

// check sumを計算したとき、それを保存しておいてあとで次回以降、整合性のチェックを行なう。
u64 eval_sum;

// is_ready_cmd()を外部から呼び出せるようにしておく。(benchコマンドなどから呼び出したいため)
// 局面は初期化されないので注意。
void is_ready(bool skipCorruptCheck)
{
	// EvalDirにある"eval_options.txt"を読み込む。
	// ここに評価関数に応じた設定を書いておくことができる。

	USI::read_engine_options(Path::Combine(Options["EvalDir"], "eval_options.txt"));

	// yaneuraou.wasm
	// ブラウザのメインスレッドをブロックしないよう、Keep Alive処理をコメントアウト
#if !defined(__EMSCRIPTEN__)
	// --- Keep Alive的な処理 ---

	// "isready"を受け取ったあと、"readyok"を返すまで5秒ごとに改行を送るように修正する。(keep alive的な処理)
	// →　これ、よくない仕様であった。
	// cf. USIプロトコルでisready後の初期化に時間がかかる時にどうすれば良いのか？
	//     http://yaneuraou.yaneu.com/2020/01/05/usi%e3%83%97%e3%83%ad%e3%83%88%e3%82%b3%e3%83%ab%e3%81%a7isready%e5%be%8c%e3%81%ae%e5%88%9d%e6%9c%9f%e5%8c%96%e3%81%ab%e6%99%82%e9%96%93%e3%81%8c%e3%81%8b%e3%81%8b%e3%82%8b%e6%99%82%e3%81%ab%e3%81%a9/
	// cf. isready後のkeep alive用改行コードの送信について
	//		http://yaneuraou.yaneu.com/2020/03/08/isready%e5%be%8c%e3%81%aekeep-alive%e7%94%a8%e6%94%b9%e8%a1%8c%e3%82%b3%e3%83%bc%e3%83%89%e3%81%ae%e9%80%81%e4%bf%a1%e3%81%ab%e3%81%a4%e3%81%84%e3%81%a6/

	// これを送らないと、将棋所、ShogiGUIでタイムアウトになりかねない。
	// ワーカースレッドを一つ生成して、そいつが5秒おきに改行を送信するようにする。
	// このあと重い処理を行うのでスレッドの起動が遅延する可能性があるから、先にスレッドを生成して、そのスレッドが起動したことを
	// 確認してから処理を行う。

	// スレッドが起動したことを通知するためのフラグ
	auto thread_started = false;

	// この関数を抜ける時に立つフラグ(スレッドを停止させる用)
	auto thread_end = false;

	// 定期的な改行送信用のスレッド
	auto th = std::thread([&] {
		// スレッドが起動した
		thread_started = true;

		int count = 0;
		while (!thread_end)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++count >= 50 /* 5秒 */)
			{
				count = 0;
				sync_cout << sync_endl; // 改行を送信する。

				// 定跡の読み込み部などで"info string.."で途中経過を出力する場合、
				// sync_cout ～ sync_endlを用いて送信しないと、この改行を送るタイミングとかち合うと
				// 変なところで改行されてしまうので注意。
			}
		}
		});
	SCOPE_EXIT({ thread_end = true; th.join(); });

	// スレッド起動待ち
	while (!thread_started)
		Tools::sleep(100);

	// --- Keep Alive的な処理ここまで ---
#endif

	// スレッドを先に生成しないとUSI_Hashで確保したメモリクリアの並列化が行われなくて困る。

#if defined(YANEURAOU_ENGINE_DEEP)

	// ここ、max_gpu == 8固定として扱っている。あとで修正する。(かも)
	int threads_num =
		(int)Options["UCT_Threads1"] + (int)Options["UCT_Threads2"] + (int)Options["UCT_Threads3"] + (int)Options["UCT_Threads4"] +
		(int)Options["UCT_Threads5"] + (int)Options["UCT_Threads6"] + (int)Options["UCT_Threads7"] + (int)Options["UCT_Threads8"];

	Threads.set(std::max(threads_num,1));
#else
	//Options["Threads"] = 1;
	Threads.set(size_t(Options["Threads"]));
#endif

#if defined (USE_EVAL_HASH)
	Eval::EvalHash_Resize(Options["EvalHash"]);
#endif

	// 評価関数の読み込み

#if defined(YANEURAOU_ENGINE_DEEP)

	// 毎回、load_eval()は呼び出すものとする。
	// モデルファイル名に変更がなければ、再読み込みされないような作りになっているならばこの実装のほうがシンプル。
	Eval::load_eval();
	USI::load_eval_finished = true;

#else

	// 評価関数の読み込みなど時間のかかるであろう処理はこのタイミングで行なう。
	// 起動時に時間のかかる処理をしてしまうと将棋所がタイムアウト判定をして、思考エンジンとしての認識をリタイアしてしまう。
	if (!USI::load_eval_finished)
	{
		// 評価関数の読み込み
		Eval::load_eval();

		// チェックサムの計算と保存(その後のメモリ破損のチェックのため)
		eval_sum = Eval::calc_check_sum();

		// ソフト名の表示
		Eval::print_softname(eval_sum);

		USI::load_eval_finished = true;
	}
	else
	{
		// メモリが破壊されていないかを調べるためにチェックサムを毎回調べる。
		// 時間が少しもったいない気もするが.. 0.1秒ぐらいのことなので良しとする。
		if (!skipCorruptCheck && eval_sum != Eval::calc_check_sum())
			sync_cout << "info string Error! : EVAL memory is corrupted" << sync_endl;
	}
#endif

	// isreadyに対してはreadyokを返すまで次のコマンドが来ないことは約束されているので
	// このタイミングで各種変数の初期化もしておく。

#if defined(YANEURAOU_ENGINE_DEEP)
	// ふかうら王では置換表は用いない。
#else
	TT.resize(size_t(Options["USI_Hash"]));
#endif

	Search::clear();

#if defined (USE_EVAL_HASH)
	Eval::EvalHash_Clear();
#endif


	Threads.stop = false;
}

// isreadyコマンド処理部
void is_ready_cmd(Position& pos, StateListPtr& states)
{
	// 対局ごとに"isready","usinewgame"の両方が来る。
	// "isready"が起動後に1度だけしか来ないようなGUI実装は、
	// 実装上の誤りであるから修正すべきである。)

	// 少なくとも将棋のGUI(将棋所、ShogiGUI、将棋神やねうら王)では、
	// "isready"が毎回来るようなので、"usinewgame"のほうは無視して、
	// "isready"に応じて評価関数、定跡、探索部を初期化する。

	is_ready();

	// Positionコマンドが送られてくるまで評価値の全計算をしていないの気持ち悪いのでisreadyコマンドに対して
	// evalの値を返せるようにこのタイミングで平手局面で初期化してしまう。

	// 新しく渡す局面なので古いものは捨てて新しいものを作る。
	states = StateListPtr(new StateList(1));
	pos.set_hirate(&states->back(),Threads.main());

	sync_cout << "readyok" << sync_endl;
}

// "position"コマンド処理部
void position_cmd(Position& pos, istringstream& is , StateListPtr& states)
{
	Move m;
	string token, sfen;

	is >> token;

	if (token == "startpos")
	{
		// 初期局面として初期局面のFEN形式の入力が与えられたとみなして処理する。
		sfen = SFEN_HIRATE;
		is >> token; // もしあるなら"moves"トークンを消費する。
	}
	// 局面がfen形式で指定されているなら、その局面を読み込む。
	// UCI(チェスプロトコル)ではなくUSI(将棋用プロトコル)だとここの文字列は"fen"ではなく"sfen"
	// この"sfen"という文字列は省略可能にしたいので..
	else {
		if (token != "sfen")
			sfen += token + " ";
		while (is >> token && token != "moves")
			sfen += token + " ";
	}

	// 駒の枚数が標準を超える局面(以前のルールの乱将棋)は、評価関数の駒リストがあふれて落ちるので読み込まない。
	if (!is_supported_ranshogi_sfen(sfen))
	{
		sync_cout << "info string Error! : unsupported position (too many pieces of a kind) : " << sfen << sync_endl;
		return;
	}

	// 新しく渡す局面なので古いものは捨てて新しいものを作る。
	states = StateListPtr(new StateList(1));
	pos.set(sfen , &states->back() , Threads.main());

	std::vector<Move> moves_from_game_root;

	// 指し手のリストをパースする(あるなら)
	while (is >> token && (m = USI::to_move(pos, token)) != MOVE_NONE)
	{
		// 1手進めるごとにStateInfoが積まれていく。これは千日手の検出のために必要。
		states->emplace_back();
		if (m == MOVE_NULL) // do_move に MOVE_NULL を与えると死ぬので
			pos.do_null_move(states->back());
		else
			pos.do_move(m, states->back());

		moves_from_game_root.emplace_back(m);
	}

	// やねうら王では、ここに保存しておくことになっている。
	Threads.main()->game_root_sfen = sfen;
	Threads.main()->moves_from_game_root = std::move(moves_from_game_root);

	// 盤面を設定しなおしたのでこのフラグはfalseに。
	Threads.main()->position_is_dirty = false;
}

// "setoption"コマンド応答。
void setoption_cmd(istringstream& is)
{
	string token, name, value;

	while (is >> token && token != "value")
		// "name"トークンはあってもなくても良いものとする。(手打ちでコマンドを打つときには省略したい)
		if (token != "name")
			// スペース区切りで長い名前のoptionを使うことがあるので2つ目以降はスペースを入れてやる
			name += (name.empty() ? "" : " ") + token;

	// valueの後ろ。スペース区切りで複数文字列が来ることがある。
	while (is >> token)
		value += (value.empty() ? "" : " ") + token;

	if (Options.count(name))
		Options[name] = value;
	else
		// この名前のoptionは存在しなかった
		sync_cout << "info string Error! : No such option: " << name << sync_endl;

}

// getoptionコマンド応答(USI独自拡張)
// オプションの値を取得する。
void getoption_cmd(istringstream& is)
{
	// getoption オプション名
	string name = "";
	is >> name;

	// すべてを出力するモード
	bool all = name == "";

	for (auto& o : Options)
	{
		// 大文字、小文字を無視して比較。また、nameが指定されていなければすべてのオプション設定の現在の値を表示。
		if ((!StringExtension::stricmp(name, o.first)) || all)
		{
			sync_cout << "Options[" << o.first << "] == " << (string)Options[o.first] << sync_endl;
			if (!all)
				return;
		}
	}
	if (!all)
		sync_cout << "No such option: " << name << sync_endl;
}


// go()は、思考エンジンがUSIコマンドの"go"を受け取ったときに呼び出される。
// この関数は、入力文字列から思考時間とその他のパラメーターをセットし、探索を開始する。
// ignore_ponder : これがtrueなら、"ponder"という文字を無視する。
void go_cmd(const Position& pos, istringstream& is , StateListPtr& states , bool ignore_ponder = false) {

	// "isready"コマンド受信前に"go"コマンドが呼び出されている。
	if (!USI::load_eval_finished)
	{
		sync_cout << "info string Error! go cmd before isready cmd." << sync_endl;
		return;
	}

	Search::LimitsType limits;
	string token;
	bool ponderMode = false;

	auto main_thread = Threads.main();

	if (!states)
	{
		// 前回から"position"コマンドを処理せずに再度goが呼び出された。
		// 前回、ponderでStochastic Ponderのために局面を壊してしまっている可能性があるので復元しておく。
		// (これがStochastic Ponderの一番簡単な実装)
		// Stochastic Ponderのために局面を2手前に戻して、そのあと現在の局面に対するコマンド("d"など)を実行すると
		// それは2手前の局面が表示されるが、それは仕様であるものとする。(これを修正するとプログラムのフローが複雑になる)
		istringstream iss(main_thread->last_position_cmd_string);
		iss >> token; // "position"
		position_cmd(*const_cast<Position*>(&pos), iss, states);
	}

	// 思考開始時刻の初期化。なるべく早い段階でこれをしておかないとサーバー時間との誤差が大きくなる。
	Time.reset();

	// 終局(引き分け)になるまでの手数
	// 引き分けになるまでの手数。(Options["MaxMovesToDraw"]として与えられる。エンジンによってはこのオプションを持たないこともある。)
	// 0のときは制限なしだが、これをint_maxにすると残り手数を計算するときに桁があふれかねないので100000を設定。

	int max_game_ply = 0;
	if (Options.count("MaxMovesToDraw"))
		max_game_ply = (int)Options["MaxMovesToDraw"];
	limits.max_game_ply = (max_game_ply == 0) ? 100000 : max_game_ply;

#if defined (USE_ENTERING_KING_WIN)
	// 入玉ルール
	limits.enteringKingRule = to_entering_king_rule(Options["EnteringKingRule"]);
#endif

	// すべての合法手を生成するのか
	limits.generate_all_legal_moves = Options["GenerateAllLegalMoves"];

	// エンジンオプションによる探索制限(0なら無制限)
	// このあと、depthもしくはnodesが指定されていたら、その値で上書きされる。(この値は無視される)

	limits.depth = Options.count("DepthLimit") ? (int)Options["DepthLimit"] : 0;
	limits.nodes = Options.count("NodesLimit") ? (u64)Options["NodesLimit"] : 0;

	while (is >> token)
	{
		// 探索すべき指し手。(探索開始局面から特定の初手だけ探索させるとき)
		// これ、Stockfishのコードでこうなっているからそのままにしてあるが、
		// これを指定しても定跡の指し手としてはこれ以外を指したりする問題はある。
		// またふかうら王ではこのオプションをサポートしていない。
		// ゆえに、非対応扱いで考えて欲しい。
		if (token == "searchmoves")
			// 残りの指し手すべてをsearchMovesに突っ込む。
			while (is >> token)
				limits.searchmoves.push_back(USI::to_move(pos, token));

		// 先手、後手の残り時間。[ms]
		else if (token == "wtime")     is >> limits.time[WHITE];
		else if (token == "btime")     is >> limits.time[BLACK];

		// フィッシャールール時における時間
		else if (token == "winc")      is >> limits.inc[WHITE];
		else if (token == "binc")      is >> limits.inc[BLACK];

		// "go rtime 100"だと100～300[ms]思考する。
		else if (token == "rtime")     is >> limits.rtime;

		// 秒読み設定。
		else if (token == "byoyomi") {
			TimePoint t = 0;
			is >> t;

            t = 10000;
			// USIプロトコルで送られてきた秒読み時間より少なめに思考する設定
			// ※　通信ラグがあるときに、ここで少なめに思考しないとタイムアップになる可能性があるので。

			// t = std::max(t - Options["ByoyomiMinus"], Time::point(0));

			// USIプロトコルでは、これが先手後手同じ値だと解釈する。
			limits.byoyomi[BLACK] = limits.byoyomi[WHITE] = t;
		}
		// この探索深さで探索を打ち切る
		else if (token == "depth")     is >> limits.depth;

		// この探索ノード数で探索を打ち切る
		else if (token == "nodes")     is >> limits.nodes;

		// 持ち時間固定(将棋だと対応しているGUIが無いかもしれないが..)
		else if (token == "movetime")  is >> limits.movetime;

		// 詰み探索。"UCI"プロトコルではこのあとには手数が入っており、その手数以内に詰むかどうかを判定するが、
		// "USI"プロトコルでは、ここは探索のための時間制限に変更となっている。
		else if (token == "mate") {
			is >> token;
			if (token == "infinite")
				limits.mate = INT32_MAX;
			else
				// USIプロトコルでは、UCIと異なり、ここは手数ではなく、探索に使う時間[ms]が指定されている。
				limits.mate = stoi(token);
		}

		// パフォーマンステスト(Stockfishにある、合法手N手で到達できる局面を求めるやつ)
		// このあとposition～goコマンドを使うとパフォーマンステストモードに突入し、ここで設定した手数で到達できる局面数を求める
		else if (token == "perft")		is >> limits.perft;

		// 時間無制限。
		else if (token == "infinite")	limits.infinite = 1;

		// ponderモードでの思考。
		else if (token == "ponder" && !ignore_ponder) {
			ponderMode = true;

			if (Options["Stochastic_Ponder"] && main_thread->moves_from_game_root.size() >= 1)
			{
				// 1手前の局面(相手番)に戻して、ponderとして思考する。
				// Threads.main()->moves_from_game_root に保存されているので大丈夫。

				auto m = main_thread->moves_from_game_root.back();
				main_thread->moves_from_game_root.pop_back();
				const_cast<Position*>(&pos)->undo_move(m);
				states->pop_back();
				main_thread->position_is_dirty = true;
			}
		}

		// --- やねうら王独自拡張

		// "wait_stop"指定。
		else if (token == "wait_stop")
			limits.wait_stop = true;

		// "silent"指定。"info ..."も"bestmove ..."も標準出力に書き出さない。
		// (アプリに組み込んで関数から思考させる場合、標準出力は不要なので抑制する用)
		else if (token == "silent")
			limits.silent = true;

#if defined(TANUKI_MATE_ENGINE)
		// MateEngineのデバッグ用コマンド: 詰将棋の特定の変化に対する解析を効率的に行うことが出来る。
		//	cf.https ://github.com/yaneurao/YaneuraOu/pull/115

		else if (token == "matedebug") {
			string token="";
			Move16 m;
			limits.pv_check.clear();
			while (is >> token && (m = USI::to_move16(token)) != MOVE_NONE){
				limits.pv_check.push_back(m);
			}
		}
#endif

	}
	//limits.depth = 15;
	limits.enteringKingRule = EKR_NONE;


	// goコマンド、デバッグ時に使うが、そのときに"go btime XXX wtime XXX byoyomi XXX"と毎回入力するのが面倒なので
	// デフォルトで1秒読み状態で呼び出されて欲しい。
	//if (limits.byoyomi[BLACK] == 0 && limits.inc[BLACK] == 0 && limits.time[BLACK] == 0 && limits.rtime == 0)
	//	limits.byoyomi[BLACK] = limits.byoyomi[WHITE] = 1000;

	// →　これやると、パラメーターなしで"go ponder"されて"ponderhit"したときに、byoyomi 1秒と錯覚する。

	Threads.start_thinking(pos, states , limits , ponderMode);
}

// "ponderhit"に"go"で使うようなwtime,btime,winc,binc,byoyomiが書けるような拡張。(やねうら王独自拡張。USI拡張プロトコル)
// 何かトークンを処理したらこの関数はtrueを返す。
bool parse_ponderhit(istringstream& is)
{
	// 現在のSearch::Limitsに上書きしてしまう。
	auto& limits = Search::Limits;
	string token;
	bool token_processed = false;

	while (is >> token)
	{
		// 何かトークンを処理したらこの関数はtrueを返す。
		token_processed = true;

		// 先手、後手の残り時間。[ms]
		     if (token == "wtime")     is >> limits.time[WHITE];
		else if (token == "btime")     is >> limits.time[BLACK];

		// フィッシャールール時における時間
		else if (token == "winc")      is >> limits.inc[WHITE];
		else if (token == "binc")      is >> limits.inc[BLACK];

		// "go rtime 100"だと100～300[ms]思考する。
		else if (token == "rtime")     is >> limits.rtime;

		// 秒読み設定。
		else if (token == "byoyomi") {
			TimePoint t = 0;
			is >> t;

			// USIプロトコルでは、これが先手後手同じ値だと解釈する。
			limits.byoyomi[BLACK] = limits.byoyomi[WHITE] = t;
		}
	}
	return token_processed;
}

PieceType draw_piece() {
    enum Weight {
        PawnWeight = 9,
        LanceWeight = 4,
        KnightWeight = 4,
        SilverWeight = 4,
        BishopWeight = 2,
        RookWeight = 2,
        GoldWeight = 4,
        ProPawnWeight = 2,
        ProLanceWeight = 2,
        ProKnightWeight = 2,
        ProSilverWeight = 2,
        HorseWeight = 1,
        DragonWeight = 1,
    };
    Weight weights[] = { PawnWeight, LanceWeight, KnightWeight, SilverWeight, GoldWeight, RookWeight, BishopWeight, ProPawnWeight, ProLanceWeight, ProKnightWeight, ProSilverWeight, HorseWeight, DragonWeight };
    PieceType types[] = { PAWN, LANCE, KNIGHT, SILVER, GOLD, ROOK, BISHOP, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER, HORSE, DRAGON };

    uint8_t weight_sum = 0;
    for(auto w : weights) {
        weight_sum += w;
    }

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(0, weight_sum-1);
    uint16_t num = distr(gen);
    uint16_t threshold = 0; 
    uint8_t index = 0;
    for(auto w : weights) {
        threshold += w;
        if(num < threshold) {
            return types[index];
        }
        index++;
    }
    return DRAGON;
}


Square select_sq(Position& pos, Piece pc) {
    Bitboard empty_bb = ~pos.pieces();
    empty_bb &= ~pos.check_squares(type_of(pc));
    if(type_of(pc) == PAWN) {
        if(color_of(pc) == BLACK) {
            empty_bb &= pawn_drop_mask<BLACK>(pos.pieces(PAWN) & pos.pieces(color_of(pc)));
        } else {
            empty_bb &= pawn_drop_mask<WHITE>(pos.pieces(PAWN) & pos.pieces(color_of(pc)));
        }
    } else if(type_of(pc) == LANCE) {
        if(color_of(pc) == BLACK) {
            empty_bb &= pawn_drop_mask<BLACK>(Bitboard(ZERO));
        } else {
            empty_bb &= pawn_drop_mask<WHITE>(Bitboard(ZERO));
        }
    } else if(type_of(pc) == KNIGHT) {
        if(color_of(pc) == BLACK) {
            empty_bb &= ~rank1_n_bb(BLACK, RANK_2);
        } else {
            empty_bb &= rank1_n_bb(BLACK, RANK_4);
        }
    } else if(type_of(pc) == KING) {
        if(color_of(pc) == BLACK) {
            empty_bb &= ~rank1_n_bb(BLACK, RANK_4);
        } else {
            empty_bb &= rank1_n_bb(BLACK, RANK_2);
            empty_bb &= ~kingEffect(pos.king_square(BLACK));
        }
    }

    int pop_count = empty_bb.pop_count();
    Square sq = SQ_INVALID;
    if(pop_count == 0) { return sq; }
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(1, pop_count);
    int sq_index = distr(gen);
    //cout << "sq_index: " << sq_index << endl;
    for(int i=0; i<sq_index; i++) {
        sq = empty_bb.pop();
    }
    
    return sq;
}

bool is_max(PieceType pt, int count) {
    if(pt == PAWN) {
        return count == 18; // 将棋と同じ上限 (両側合計18枚)
    } else if(pt == ROOK || pt == BISHOP) {
        return count == 2;
    } else {
        return count == 4;
    }
}

// TT(置換表)を辿って PV を復元する。
// rootMoves[rootIndex].pv に入っている分はそれを優先し、足りない分を TT から拾う。
// 6x6 等の改造版で暴走しやすいので、PV_LIMIT / TIMEOUT / 循環検出 を必ず入れている。
static std::vector<Move> extract_pv_from_tt(const Position& rootPos, size_t rootIndex)
{
    std::vector<Move> pvMoves;

    // 返ってこない対策：適切な上限（必要なら調整）
    constexpr int PV_LIMIT = 64;

    // 返ってこない対策：時間で打ち切る（ms）
    // 速さ重視なら 10〜20、確実に取りたいなら 50〜100 くらい
    constexpr int TIMEOUT_MS = 50;

    // 6x6 だと PV はそもそも短くて十分なことが多い
    pvMoves.reserve(PV_LIMIT);

    // rootMoves は、その局面を探索していたスレッドに紐づく
    // ※ rootPos.this_thread() が使える前提（pv()内と同じ）
    const auto* th = rootPos.this_thread();
    if (!th)
        return pvMoves;

    const auto& rootMoves = th->rootMoves;
    if (rootMoves.empty() || rootIndex >= rootMoves.size())
        return pvMoves;

    // Position は do/undo するので const を外す（pv()と同じ方針）
    Position* pos = const_cast<Position*>(&rootPos);

    // undo 用
    Move played[PV_LIMIT + 1];
    StateInfo st[PV_LIMIT];
    int ply = 0;

    // 循環検出：同じ局面に戻ったら打ち切る（千日手判定が改造で壊れていても止まる）
    uint64_t seenKeys[PV_LIMIT + 1];
    int seenN = 0;

    // タイムアウト用
    const TimePoint start = Time.elapsed();

    auto seen_before = [&](uint64_t key) -> bool {
        for (int i = 0; i < seenN; ++i)
            if (seenKeys[i] == key)
                return true;
        if (seenN < PV_LIMIT + 1)
            seenKeys[seenN++] = key;
        return false;
    };

    while (ply < PV_LIMIT)
    {
        // TIMEOUT
        if (Time.elapsed() - start > TIMEOUT_MS)
            break;

        // 局面循環検出
        const uint64_t key = pos->state()->hash_key();
        if (seen_before(key))
            break;

        // 千日手等（これが壊れてても循環検出で止まるので保険扱い）
        const auto rep = pos->is_repetition(ply);
        if (rep != REPETITION_NONE && ply >= 1)
            break;

        Move m = MOVE_NONE;

        // まず rootMoves の pv を辿れるだけ辿る
        if (ply < (int)rootMoves[rootIndex].pv.size())
        {
            m = rootMoves[rootIndex].pv[ply];
        }
        else
        {
            // 次の手を TT から拾う
            bool found = false;
            auto* tte = TT.read_probe(pos->state()->hash_key(), found);
            if (!found)
                break;

            m = pos->to_move(tte->move());
            if (m == MOVE_NONE)
                break;

            // TT の指し手は壊れている可能性があるので合法性チェック
            // 歩不成などを表示したいので pseudo_legal_s<true>() を使う（pv()の実装に合わせる）
            if (m != MOVE_WIN)
            {
                if (!(pos->pseudo_legal_s<true>(m) && pos->legal(m)))
                    break;
            }
        }

#if defined(USE_ENTERING_KING_WIN)
        if (m == MOVE_WIN)
            break;
#endif

        pvMoves.push_back(m);
        played[ply] = m;

        pos->do_move(m, st[ply]);
        ++ply;
    }

    // 戻す
    while (ply > 0)
        pos->undo_move(played[--ply]);

    return pvMoves;
}



// pv_info の " pv " 以降を token 化
static std::vector<std::string> extract_pv_tokens_from_info(const std::string& pv_info)
{
    std::vector<std::string> tokens;
    const std::string key = " pv ";
    auto p = pv_info.find(key);
    if (p == std::string::npos)
        return tokens;

    std::istringstream iss(pv_info.substr(p + key.size()));
    std::string t;
    while (iss >> t)
    {
        // MultiPV では複数行の info が連ねられていることがある。2行目以降は別の読み筋なので読まない
        if (t == "info" || t == "bestmove")
            break;
        tokens.push_back(t);
    }

    return tokens;
}

// 先手玉が5段目、後手玉が2段目に来たら true（=NG）
static inline bool kings_reach_forbidden_ranks_6x6(const Position& pos)
{
    // 玉がいない場合、SQ_NB に移動させる仕様（types.hコメント）なので SQ_NB も来うる
    Square bk = pos.king_square(BLACK);
    Square wk = pos.king_square(WHITE);

    // 玉が盤上にいない局面は生成ミス扱いで弾く（好みで false にしてもOK）
    if (bk == SQ_NB || wk == SQ_NB)
        return true;

    // types.h の rank_of() を使うのが正解（6x6でもテーブルが定義済み）
    Rank rb = rank_of(bk);
    Rank rw = rank_of(wk);

    // 指定どおり「先手=5段目」「後手=2段目」を禁止
    if (rb == RANK_2) return true;
    if (rw == RANK_5) return true;

    return false;
}

// pv_info の PV を best 手順として再生し、途中のどこかで禁止段に到達したら false。
// true なら「PV上、どこでも到達しない」。
static bool pv_info_avoids_nyugyoku_ranks_6x6(Position& pos,
                                             const std::string& pv_info,
                                             int ply_limit = 64,
                                             bool check_start_position = true)
{
    if (check_start_position && kings_reach_forbidden_ranks_6x6(pos))
        return false;

    auto tokens = extract_pv_tokens_from_info(pv_info);
    if (tokens.empty())
        return true; // PVが無いなら到達しない扱い（必要なら false に）

    const int n = std::min<int>((int)tokens.size(), ply_limit);

    // do/undo のため StateInfo を生存させる
    std::vector<StateInfo> si(n);
    std::vector<Move> played;
    played.reserve(n);

    for (int i = 0; i < n; ++i)
    {
        // 文字列→Move16（局面非依存）
        Move16 m16 = USI::to_move16(tokens[i]);

        // Move16 には駒(上位16bit)が無いので Position::to_move(Move16) で Move に戻すのが正解（types.hコメント）
        Move m = pos.to_move(m16);

        // 変換不能・不正
        if (m == MOVE_NONE)
        {
            // 安全側：失敗したら弾く
            while (!played.empty()) {
                pos.undo_move(played.back());
                played.pop_back();
            }
            return false;
        }

        // 念のため合法性チェック（重いなら外してOK）
        if (m != MOVE_WIN)
        {
            if (!(pos.pseudo_legal_s<true>(m) && pos.legal(m)))
            {
                while (!played.empty()) {
                    pos.undo_move(played.back());
                    played.pop_back();
                }
                return false;
            }
        }

        pos.do_move(m, si[i]);
        played.push_back(m);

        if (kings_reach_forbidden_ranks_6x6(pos))
        {
            while (!played.empty()) {
                pos.undo_move(played.back());
                played.pop_back();
            }
            return false;
        }
    }

    // 元に戻す
    while (!played.empty()) {
        pos.undo_move(played.back());
        played.pop_back();
    }

    return true;
}

struct CsvRecord {
    std::string sfen;
    int pv_ply_limit = 64;
    std::string pv_info;   // "info ... pv ..."（末尾score除外済み）
    bool has_last_score = false;
    int last_score = 0;
};

// 先頭2つのカンマで分け、残り（pv_info[,last_score]）を処理
static bool parse_csv_line_with_optional_last_score(const std::string& line, CsvRecord& out) {
    if (line.empty()) return false;

    size_t c1 = line.find(',');
    if (c1 == std::string::npos) return false;

    size_t c2 = line.find(',', c1 + 1);
    if (c2 == std::string::npos) return false;

    out.sfen = line.substr(0, c1);

	out.pv_ply_limit = std::stoi(line.substr(c1 + 1, c2 - (c1 + 1)));

    std::string rest = line.substr(c2 + 1);

    // 末尾に ",<int>" があれば last_score として扱い、上書き対象にする
    // pv_info にはカンマが入らない前提（あなたのデータ形式）
    size_t last_comma = rest.rfind(',');
    if (last_comma != std::string::npos) {
        std::string tail = rest.substr(last_comma + 1);
        // tail が整数なら last_score
        bool ok_int = !tail.empty();
        size_t i = 0;
        if (tail[0] == '-' || tail[0] == '+') i = 1;
        for (; i < tail.size(); ++i) if (!std::isdigit((unsigned char)tail[i])) { ok_int = false; break; }

        if (ok_int) {
            out.has_last_score = true;
            out.last_score = std::stoi(tail);
            out.pv_info = rest.substr(0, last_comma);
            return true;
        }
    }

    out.has_last_score = false;
    out.pv_info = rest;
    return true;
}

static int eval_score_by_sfen(const std::string& sfen, int depth)
{
    Position p;
    StateListPtr st(new StateList(1));
    p.set(sfen, &st->back(), Threads.main());

    // これらは不要なら消してOK（あなたが last_result を使うなら残してもよい）
    Threads.main()->game_root_sfen = sfen;
    Threads.main()->moves_from_game_root.clear();

    std::ostringstream oss;
    oss << "depth " << depth;
    std::istringstream go_is(oss.str());

    go_cmd(p, go_is, st);
    Threads.main()->wait_for_search_finished();

    auto& r = Threads.main()->last_result;
    if (!r.valid) return 0;

    return (int)r.score; // Value をそのまま int 化
}

static bool make_last_sfen_from_pv_info(Position& pos,
                                       const std::string& pv_info,
                                       int pv_ply_limit,
                                       std::string& out_last_sfen)
{
    auto tokens = extract_pv_tokens_from_info(pv_info);
    if (tokens.empty())
        return false;

    const int max_pv = std::min<int>((int)tokens.size(), pv_ply_limit);

    std::vector<StateInfo> si(max_pv);
    std::vector<Move> played;
    played.reserve(max_pv);

    for (int i = 0; i < max_pv; ++i)
    {
        Move16 m16 = USI::to_move16(tokens[i]);
        Move m = pos.to_move(m16);
        if (m == MOVE_NONE) {
            while (!played.empty()) { pos.undo_move(played.back()); played.pop_back(); }
            return false;
        }

        // TT由来のPV末尾などで不正が混ざる可能性があるので合法チェック推奨
        if (m != MOVE_WIN) {
            if (!(pos.pseudo_legal_s<true>(m) && pos.legal(m))) {
                while (!played.empty()) { pos.undo_move(played.back()); played.pop_back(); }
                return false;
            }
        }

        pos.do_move(m, si[i]);
        played.push_back(m);
    }

    out_last_sfen = pos.sfen();

    while (!played.empty()) {
        pos.undo_move(played.back());
        played.pop_back();
    }

    return true;
}


static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return (bool)f;
}

// 逐次追記版：途中で落ちても .out に途中まで残る。
// 再実行時は .out を入力として続行（checkpoint）。
static void evalcsv_last_cmd(Position& pos, std::istringstream& is, StateListPtr& states)
{
    std::string token;
    std::string filepath;
    int depth = 15;
    bool checkpoint = true; // デフォルトON推奨（止まることがあるなら）

    while (is >> token) {
        if (token == "file") is >> filepath;
        else if (token == "depth") is >> depth;
        else if (token == "checkpoint") {
            std::string v; is >> v;
            checkpoint = (v == "yes" || v == "1" || v == "true");
        }
    }

    if (filepath.empty()) {
        sync_cout << "info string evalcsv_last: missing file" << sync_endl;
        return;
    }

    // 出力先（逐次追記）
    const std::string outpath = filepath + ".out";

    // checkpointモードなら、入力は「outがあればout」「なければ元ファイル」
    // out には last_score 付きが混ざるので「既に値がある行はスキップ」仕様と相性が良い
    const std::string inpath = (checkpoint && file_exists(outpath)) ? outpath : filepath;

    std::ifstream fin(inpath);
    if (!fin) {
        sync_cout << "info string evalcsv_last: cannot open input " << inpath << sync_endl;
        return;
    }

    // 出力は追記で開く（checkpoint継続）
    std::ofstream fout(outpath, std::ios::app);
    if (!fout) {
        sync_cout << "info string evalcsv_last: cannot open output " << outpath << sync_endl;
        return;
    }

    // checkpoint継続時は「入力も outpath」なので、そのまま追記すると重複して増殖します。
    // そこで、inpath==outpath の場合は "追記" ではなく "新しい out.tmp を作って" 追記するのが安全。
    // しかしユーザー要望は「途中でも追記」なので、増殖回避として以下の戦略を取ります：
    //
    // - inpath が outpath のときは outpath を読み、別の outpath.tmp に「逐次追記」する
    // - 完了したら outpath を outpath.bak に退避し、tmp を outpath に rename
    //
    // これなら「途中でも書ける（tmpに逐次反映）」＋「再開も可能」です。

    fin.close();
    fout.close();

    std::string real_in = inpath;
    std::string real_out = outpath;

    std::string tmpout;
    bool use_tmp = false;

    if (real_in == real_out) {
        use_tmp = true;
        tmpout = outpath + ".tmp";
        real_out = tmpout;
    }

    std::ifstream fin2(real_in);
    if (!fin2) {
        sync_cout << "info string evalcsv_last: cannot open input " << real_in << sync_endl;
        return;
    }

    std::ofstream fout2(real_out, std::ios::app);
    if (!fout2) {
        sync_cout << "info string evalcsv_last: cannot open output " << real_out << sync_endl;
        return;
    }

    int line_no = 0;
    int skipped = 0;
    int evaluated = 0;

    std::string line;
    while (std::getline(fin2, line)) {
        ++line_no;

        CsvRecord rec;
        if (!parse_csv_line_with_optional_last_score(line, rec)) {
            // パースできない行はそのまま出す
            fout2 << line << "\n";
            fout2.flush();
            continue;
        }

        // 既に last_score があるなら、そのまま出してスキップ
        if (rec.has_last_score) {
            fout2 << line << "\n";
            fout2.flush();
            ++skipped;
            continue;
        }

        // 局面セット
        states = StateListPtr(new StateList(1));
        pos.set(rec.sfen, &states->back(), Threads.main());

        // PV末尾局面のSFEN作成
        std::string last_sfen;
        bool ok = make_last_sfen_from_pv_info(pos, rec.pv_info, rec.pv_ply_limit, last_sfen);

        int last_score = 0;
        if (ok) {
            last_score = eval_score_by_sfen(last_sfen, depth);
        }

        // 追記（逐次）
        fout2 << rec.sfen << "," << rec.pv_ply_limit << "," << rec.pv_info << "," << last_score << "\n";
        fout2.flush();
        ++evaluated;

        if ((evaluated + skipped) % 50 == 0) {
            sync_cout << "info string evalcsv_last progress evaluated="
                      << evaluated << " skipped=" << skipped
                      << " line=" << line_no << sync_endl;
        }
    }

    fin2.close();
    fout2.close();

    // in==out のときは tmp を本体 out に差し替える
    if (use_tmp) {
        // outpath をバックアップしてから差し替え
        const std::string bak = outpath + ".bak";
        std::remove(bak.c_str());
        std::rename(outpath.c_str(), bak.c_str());
        std::rename(tmpout.c_str(), outpath.c_str());
    }

    sync_cout << "info string evalcsv_last done evaluated=" << evaluated
              << " skipped=" << skipped << " output=" << outpath << sync_endl;
}



// 学習用の駒配置: 玉の配置制限を外し、全マスをカバーする
// 桂・香は動けない段への配置を禁止（ゲームルール通り）
// 乱数生成器は呼び出し元から渡して使い回す
static Square select_sq_training(Position& pos, Piece pc, std::mt19937& gen) {
    Bitboard empty_bb = ~pos.pieces();
    empty_bb &= ~pos.check_squares(type_of(pc));
    if(type_of(pc) == PAWN) {
        if(color_of(pc) == BLACK) {
            empty_bb &= pawn_drop_mask<BLACK>(pos.pieces(PAWN) & pos.pieces(color_of(pc)));
        } else {
            empty_bb &= pawn_drop_mask<WHITE>(pos.pieces(PAWN) & pos.pieces(color_of(pc)));
        }
    } else if(type_of(pc) == LANCE) {
        if(color_of(pc) == BLACK) {
            empty_bb &= pawn_drop_mask<BLACK>(Bitboard(ZERO));
        } else {
            empty_bb &= pawn_drop_mask<WHITE>(Bitboard(ZERO));
        }
    } else if(type_of(pc) == KNIGHT) {
        if(color_of(pc) == BLACK) {
            empty_bb &= ~rank1_n_bb(BLACK, RANK_2);
        } else {
            empty_bb &= rank1_n_bb(BLACK, RANK_4);
        }
    } else if(type_of(pc) == KING && color_of(pc) == WHITE) {
        empty_bb &= ~kingEffect(pos.king_square(BLACK));
    }

    int pop_count = empty_bb.pop_count();
    if(pop_count == 0) return SQ_INVALID;
    int sq_index = std::uniform_int_distribution<>(1, pop_count)(gen);
    Square sq = SQ_INVALID;
    for(int i=0; i<sq_index; i++) {
        sq = empty_bb.pop();
    }
    return sq;
}

// 学習用ランダム局面生成:
//   - 玉の配置制限なし（全マスカバー）
//   - 桂・香は動けない段への配置禁止（ルール通り）
//   - 駒の枚数: 10〜26枚（玉2枚含む）
//   - 手駒化確率: 15%
//   - 片側のマテリアル合計: 5500以下
//   - 将棋の枚数上限は維持
string generate_ranshogi_training_sfen() {
    thread_local std::mt19937 gen(std::random_device{}());
    uint16_t target = 3500 + std::uniform_int_distribution<>(0, 2000)(gen);
    const uint16_t MAX_MATERIAL = 5500;
    Position pos;
    StateInfo si;
    pos.set("666666 b - 1", &si, Threads.main());

    for(int i=0; i<2; i++) {
        Color c = (Color)i;
        Piece king = make_piece(c, KING);
        auto sq = select_sq_training(pos, king, gen);
        if(sq == SQ_INVALID) return "";
        pos.put_piece(sq, king);
        pos.update_bitboards();
        pos.update_kingSquare();
        pos.set_state(&si);
    }

    int piece_counts[] = {
        0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0
    };
    int total_pieces = 2;

    for(int i=0; i<2; i++) {
        uint16_t sum = 0;
        Color c = (Color)i;
        while(sum < target && sum < MAX_MATERIAL && total_pieces < 26) {
            auto pt = draw_piece();
            auto pc = make_piece(c, pt);
            auto raw_pt = raw_type_of(pc);

            if(is_max(raw_pt, piece_counts[raw_pt]))
                continue;
            if(sum + Eval::PieceValue[pt] > MAX_MATERIAL)
                break;

            piece_counts[raw_pt]++;
            total_pieces++;

            if(std::uniform_int_distribution<>(1, 100)(gen) <= 15) {
                pos.add_hand_ranshogi(c, raw_type_of(pc));
            } else {
                auto sq = select_sq_training(pos, pc, gen);
                if(sq == SQ_INVALID) break;
                pos.put_piece(sq, pc);
            }
            pos.update_bitboards();
            pos.set_state(&si);
            sum += Eval::PieceValue[pt];
        }
        pos.do_null_move(si);
    }
    pos.set_state(&si);

    if (total_pieces < 10) return "";
    if (pos.in_check()) return "";
    if (MoveList<LEGAL>(pos).size() == 0) return "";

    return pos.sfen();
}

// max_board_pieces: 盤上の駒数の上限(玉を含む。先後それぞれ半分まで)。0以下なら上限なし
string generate_ranshogi_random_sfen(int max_board_pieces) {
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(0, 2000);
    uint16_t target = 3500 + distr(gen);
    Position pos;
    StateInfo si;
    pos.set("666666 b - 1", &si, Threads.main());

    for(int i=0; i<2; i++) {
        Color c = (Color)i;
        Piece king = make_piece(c, KING);
        auto sq = select_sq(pos, king);
        pos.put_piece(sq, king);
        pos.update_bitboards();
        pos.update_kingSquare();
        pos.set_state(&si);
    }

    std::uniform_int_distribution<> distr2(1, 100);

	int piece_counts[] = {
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0
	};

    // 盤上の駒数(玉を含む)
    int board_counts[] = { 1, 1 };

    for(int i=0; i<2; i++) {
        uint16_t sum = 0;
        Color c = (Color)i;
        while(sum < target) {
            auto pt = draw_piece();
            auto pc = make_piece(c, pt);
			auto raw_pt = raw_type_of(pc);

			if(is_max(raw_pt, piece_counts[raw_pt])) {
				continue;
			}
			piece_counts[raw_pt]++;

            if(distr2(gen) <= 15) {
                pos.add_hand_ranshogi(c, raw_type_of(pc));
            } else {
                // 盤上の駒数が上限に達したら、この手番側はそれ以上駒を増やさない
                if(max_board_pieces > 0 && board_counts[i] >= max_board_pieces / 2) break;
                auto sq = select_sq(pos, pc);
                if(sq == SQ_INVALID) break;
                pos.put_piece(sq, pc);
                board_counts[i]++;
            }
            pos.update_bitboards();
            pos.set_state(&si);
            sum += Eval::PieceValue[pt];
        }
        pos.do_null_move(si);
    }
    pos.set_state(&si);

    // 王手がかかっている初期局面は無効
    if (pos.in_check())
        return "";

    // 合法手が0 = 詰み局面は無効
    if (MoveList<LEGAL>(pos).size() == 0)
        return "";

    return pos.sfen();
}

// 盤上の駒数に上限をつけない版 (grs や学習部から使う)
string generate_ranshogi_random_sfen() {
    return generate_ranshogi_random_sfen(0);
}

void generate_ranshogi_sfen_cmd(Position& pos, istringstream& is, StateListPtr& states)
{

	string token;
	int depth = 25;
	int n_sfens = 10000;
	while (is >> token)
	{
		if (token == "depth")
			is >> depth;
		if (token == "n_sfen")
			is >> n_sfens;
	}
	int i = 0;
	while(i<n_sfens) {
		string sfen = generate_ranshogi_random_sfen();
		if (sfen.empty())
			continue; // 詰み局面または王手局面は再生成

        states = StateListPtr(new StateList(1));
        pos.set(sfen , &states->back() , Threads.main());
        std::vector<Move> moves_from_game_root;
        Threads.main()->game_root_sfen = sfen;
        Threads.main()->moves_from_game_root.clear();

        std::ostringstream oss;
        oss << "depth " << depth;
        std::istringstream go_is(oss.str());

        go_cmd(pos, go_is, states);
		sync_cout << "wait_for_search_finished" << sync_endl;
		Threads.main()->wait_for_search_finished();

        
		auto& r = Threads.main()->last_result; // あるいは bestThread に持たせたならそれ
		if (r.valid) {
			sync_cout << r.pv_info << sync_endl; // そのまま「info ... pv ...」が取れる
			sync_cout << "score " << r.score << sync_endl;

			if(abs(r.score) < 300 && abs(r.score) < (int)VALUE_MATE_IN_MAX_PLY) {
				if(pv_info_avoids_nyugyoku_ranks_6x6(pos, r.pv_info)) {
					std::ofstream outfile;
					outfile.open("ranshogi_sfen.txt", std::ios_base::app);
					outfile << sfen << ',' << abs(r.score) << ',' << r.pv_info << '\n'; 
				} else {
					std::ofstream outfile;
					outfile.open("ranshogi_bad_sfen.txt", std::ios_base::app);
					outfile << sfen << ',' << abs(r.score) << ',' << r.pv_info << '\n'; 
				}
			} else {
			}
		}

		//auto info = USI::pv(pos, Depth(0), -VALUE_INFINITE, VALUE_INFINITE);

		sync_cout << "wait_for_search_finished done" << sync_endl;
		i++;
	}

}

// 玉が、盤上の今の利きのまま入玉(トライ: 先手玉は1段目、後手玉は6段目)できるまでの最短手数。
// 自分の駒の無いマスのうち、相手に利かされていないマスだけを玉で歩く(相手の応手は考えない)。
// たどり着けなければ大きな値を返す。
static int king_steps_to_try_rank_6x6(const Position& pos, Color c)
{
	const int unreachable = 1000;

	Square ksq = pos.king_square(c);
	if (ksq == SQ_NB)
		return unreachable;

	const Rank try_rank = (c == BLACK) ? RANK_1 : RANK_6;
	const Color them = (c == BLACK) ? WHITE : BLACK;
	if (rank_of(ksq) == try_rank)
		return 0;

	int dist[SQ_NB_PLUS1];
	std::fill(std::begin(dist), std::end(dist), unreachable);
	std::vector<Square> queue{ ksq };
	dist[ksq] = 0;

	for (size_t head = 0; head < queue.size(); ++head)
	{
		Square sq = queue[head];
		Bitboard to_bb = kingEffect(sq) & ~pos.pieces(c);
		while (to_bb)
		{
			Square to = to_bb.pop();
			if (dist[to] != unreachable)
				continue;
			if (pos.attackers_to(them, to, pos.pieces()))
				continue;

			dist[to] = dist[sq] + 1;
			if (rank_of(to) == try_rank)
				return dist[to];
			queue.push_back(to);
		}
	}
	return unreachable;
}

// 勝ち切れ乱将棋の局面を1つ生成する (grs を元にしたもの)。
// 乱将棋のランダムな局面(先手番)を探索し、評価値の絶対値が score±margin に入る局面を探す。
// 浅い depth で候補を探し、verify_depth で探索し直しても範囲内なら採用する。有利な側がプレイヤーになる。
// 盤上の駒数は max_board_pieces まで (玉を含む。先後それぞれ半分まで)。
// どちらかの玉が今の利きのまま king_entry_steps 手以内に入玉(トライ)できる局面は、探索の前に除く。
// 勝ちの順が狭い局面だけを採る: 読み筋に沿って、プレイヤーの最初の narrow_plies 回の手番それぞれで
// 評価値が narrow_score 以上かつ最善手との差が narrow_gap 以内の手(勝ちを保てる手)が 1〜narrow_max 手のもの (MultiPV で narrow_max+1 手を読む)。
//   gkp depth 12 verify_depth 16 nodes 500000 verify_nodes 5000000 score 1200 margin 100 tries 3000 max_board_pieces 22
//       narrow_plies 3 narrow_max 2 narrow_score 600 narrow_gap 300 narrow_depth 14 narrow_nodes 2000000 king_entry_steps 5
// 途中経過: 20局面ごとに "kachikire progress tries <調べた数> elapsed_ms <経過ミリ秒>"
// 出力: "kachikire sfen <sfen> score <先手から見た評価値> player <b|w> pv <正解手順>"
//       見つからなければ "kachikire none"
void generate_kachikire_sfen_cmd(Position& pos, istringstream& is, StateListPtr& states)
{
	string token;
	int depth = 12;
	int verify_depth = 16;
	int target = 1200;
	int margin = 100;
	int tries = 3000;
	int max_board_pieces = 22;
	// 1局面の探索が長引かないよう、読む局面数にも上限をつける (0 なら上限なし)
	u64 nodes = 500000;
	u64 verify_nodes = 5000000;
	// 勝ちの順が狭い局面だけを採る (narrow_plies 0 で判定しない)。narrow_score が負なら目標の半分にする
	int narrow_plies = 3;
	int narrow_max = 2;
	int narrow_score = -1;
	// 最善手との差がこの点数以内の手だけを「勝ちを保てる手」とみなす (有利な局面ではたいていの手が narrow_score を超えるため)
	int narrow_gap = 300;
	int narrow_depth = 14;
	u64 narrow_nodes = 2000000;
	// どちらかの玉が king_entry_steps 手以内に入玉できる局面は除く (0 で判定しない)
	int king_entry_steps = 5;
	// 基準を決めるための調査用: 勝ちの順の狭さで除かずに、候補ごとに各手番の上位の評価値を出して最後まで調べ続ける
	int narrow_report = 0;
	while (is >> token)
	{
		if (token == "depth") is >> depth;
		else if (token == "verify_depth") is >> verify_depth;
		else if (token == "score") is >> target;
		else if (token == "margin") is >> margin;
		else if (token == "tries") is >> tries;
		else if (token == "max_board_pieces") is >> max_board_pieces;
		else if (token == "nodes") is >> nodes;
		else if (token == "verify_nodes") is >> verify_nodes;
		else if (token == "narrow_plies") is >> narrow_plies;
		else if (token == "narrow_max") is >> narrow_max;
		else if (token == "narrow_score") is >> narrow_score;
		else if (token == "narrow_gap") is >> narrow_gap;
		else if (token == "narrow_depth") is >> narrow_depth;
		else if (token == "narrow_nodes") is >> narrow_nodes;
		else if (token == "king_entry_steps") is >> king_entry_steps;
		else if (token == "narrow_report") is >> narrow_report;
	}
	if (narrow_score < 0)
		narrow_score = target / 2;

	// sfen の局面を depth で探索し、手番(先手)から見た評価値を返す
	auto search = [&](const string& sfen, int d, u64 node_limit, bool& valid) -> int {
		states = StateListPtr(new StateList(1));
		pos.set(sfen, &states->back(), Threads.main());
		Threads.main()->game_root_sfen = sfen;
		Threads.main()->moves_from_game_root.clear();
		Threads.main()->last_result.valid = false;

		std::ostringstream oss;
		oss << "depth " << d;
		if (node_limit > 0)
			oss << " nodes " << node_limit;
		std::istringstream go_is(oss.str());
		go_cmd(pos, go_is, states);
		Threads.main()->wait_for_search_finished();

		auto& r = Threads.main()->last_result;
		valid = r.valid;
		return valid ? (int)r.score : 0;
	};

	const auto start_time = std::chrono::steady_clock::now();

	// どの条件で除いたかの数。条件の厳しさを調整するときの目安に、終わりに "info string" で出す
	// ("kachikire " で始まる行はツールが結果として読むので、それ以外の形で出す)
	struct {
		int king_entry = 0, shallow_score = 0, verify_score = 0, pv_nyugyoku = 0, not_narrow = 0;
		int narrow_no_winning = 0, narrow_too_many = 0;
	} stats;
	auto print_stats = [&]() {
		sync_cout << "info string kachikire_stats king_entry " << stats.king_entry
			<< " shallow_score " << stats.shallow_score << " verify_score " << stats.verify_score
			<< " pv_nyugyoku " << stats.pv_nyugyoku << " not_narrow " << stats.not_narrow
			<< " (no_winning " << stats.narrow_no_winning << " too_many " << stats.narrow_too_many << ")" << sync_endl;
	};

	auto in_range = [&](int score) {
		return abs(score) < (int)VALUE_MATE_IN_MAX_PLY && abs(abs(score) - target) <= margin;
	};

	// 勝ちの順が狭いか。深い探索の読み筋に沿って進め、プレイヤー(有利な側)の手番ごとに MultiPV で上位 narrow_max+1 手を読み、
	// 評価値が narrow_score 以上かつ最善手との差が narrow_gap 以内の手(勝ちを保てる手)の数が 1〜narrow_max なら狭いとする。
	// first_ply: プレイヤーの最初の手番の手数。初期局面は先手番なので、先手が有利なら 0、後手が有利なら 1。
	auto has_narrow_winning_line = [&](const string& sfen, const std::string& pv_info, int first_ply) -> bool {
		// 以後の探索で last_result が書き換わるので、読み筋は先に取り出しておく
		const std::vector<std::string> pv_tokens = extract_pv_tokens_from_info(pv_info);
		const int saved_multi_pv = (int)Options["MultiPV"];
		Options["MultiPV"] = std::to_string(narrow_max + 1);

		bool narrow = true;
		// 調査用(narrow_report)のときは、狭くないと分かっても残りの手番も読んで評価値を出す
		for (int k = 0; k < narrow_plies && (narrow || narrow_report); k++)
		{
			const int ply = first_ply + k * 2;
			// 読み筋がそこまで無い(途中で詰むなど)ときは、そこまでで判定を終える
			if (ply >= (int)pv_tokens.size())
				break;

			states = StateListPtr(new StateList(1));
			pos.set(sfen, &states->back(), Threads.main());
			Threads.main()->game_root_sfen = sfen;
			Threads.main()->moves_from_game_root.clear();
			bool replayed = true;
			for (int i = 0; i < ply; i++)
			{
				Move m = pos.to_move(USI::to_move16(pv_tokens[i]));
				if (m == MOVE_NONE || m == MOVE_WIN || !(pos.pseudo_legal_s<true>(m) && pos.legal(m)))
				{
					replayed = false;
					break;
				}
				states->emplace_back();
				pos.do_move(m, states->back());
				Threads.main()->moves_from_game_root.emplace_back(m);
			}
			if (!replayed)
			{
				narrow = false;
				break;
			}

			std::ostringstream oss;
			oss << "depth " << narrow_depth;
			if (narrow_nodes > 0)
				oss << " nodes " << narrow_nodes;
			std::istringstream go_is(oss.str());
			go_cmd(pos, go_is, states);
			Threads.main()->wait_for_search_finished();

			// 探索を打ち切った反復で読み直せなかった手は score が -VALUE_INFINITE なので、前の反復の値を使う (USI::pv と同じ)
			const auto& root_moves = Threads.main()->rootMoves;
			const size_t n = std::min(root_moves.size(), (size_t)(narrow_max + 1));
			int winning = 0;
			int best = 0;
			std::ostringstream scores;
			for (size_t i = 0; i < n; i++)
			{
				Value v = root_moves[i].score != -VALUE_INFINITE ? root_moves[i].score : root_moves[i].previousScore;
				// 上位から並んでいるので、最初の手が最善手
				if (i == 0)
					best = (int)v;
				if ((int)v >= narrow_score && (int)v >= best - narrow_gap)
					winning++;
				scores << ' ' << (int)v;
			}
			if (narrow_report)
				sync_cout << "info string kachikire_narrow ply " << ply << " moves " << root_moves.size()
					<< " scores" << scores.str() << sync_endl;

			// 勝ちを保てる手が無い(読み直すと勝ちが見えない)か、多すぎる局面は除く
			if (winning == 0 || winning > narrow_max)
			{
				narrow = false;
				if (winning == 0) stats.narrow_no_winning++; else stats.narrow_too_many++;
			}
		}

		Options["MultiPV"] = std::to_string(saved_multi_pv);
		return narrow;
	};

	for (int i = 0; i < tries; i++)
	{
		// 進み具合 (ツールの画面に出す)
		if (i % 20 == 0)
			sync_cout << "kachikire progress tries " << i << " elapsed_ms "
				<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
				<< sync_endl;

		string sfen = generate_ranshogi_random_sfen(max_board_pieces);
		// 詰み・王手の局面と、先手番でない局面は使わない (プレイヤーが後手なら AI(先手) から指す)
		if (sfen.empty() || sfen.find(" b ") == string::npos)
			continue;

		// どちらかの玉がすぐ入玉(トライ)できる局面は除く (探索の前に盤面だけで判定する)
		if (king_entry_steps > 0)
		{
			states = StateListPtr(new StateList(1));
			pos.set(sfen, &states->back(), Threads.main());
			if (king_steps_to_try_rank_6x6(pos, BLACK) <= king_entry_steps
				|| king_steps_to_try_rank_6x6(pos, WHITE) <= king_entry_steps)
			{
				stats.king_entry++;
				continue;
			}
		}

		bool valid;
		int score = search(sfen, depth, nodes, valid);
		if (!valid || !in_range(score))
		{
			stats.shallow_score++;
			continue;
		}

		score = search(sfen, verify_depth, verify_nodes, valid);
		if (!valid || !in_range(score))
		{
			stats.verify_score++;
			continue;
		}

		// この局面の読み筋。この後の狭さ調べが別の局面を探索して last_result を書き換えるので、ここで取っておく
		const std::string verified_pv_info = Threads.main()->last_result.pv_info;

		// 読み筋で入玉に向かう局面は除く (grs と同じ判定)
		if (!pv_info_avoids_nyugyoku_ranks_6x6(pos, verified_pv_info))
		{
			stats.pv_nyugyoku++;
			continue;
		}

		// 勝ちの順が狭い(最初の数手の正解が narrow_max 手以内の)局面だけを採る
		const bool is_narrow = narrow_plies <= 0 || has_narrow_winning_line(sfen, verified_pv_info, score > 0 ? 0 : 1);
		if (!is_narrow)
			stats.not_narrow++;
		if (narrow_report)
		{
			// 調査用: 採らずに候補として出し、続けて調べる
			sync_cout << "info string kachikire_candidate narrow " << is_narrow << " score " << score << " sfen " << sfen << sync_endl;
			continue;
		}
		if (!is_narrow)
			continue;

		print_stats();

		// 正解手順も出す。ヒントや答え合わせに使えるよう、DBの列 (500文字) に収まる分だけ。
		// この局面から実際に指せる手だけを出す (指せない手が出てきたら、そこで打ち切る)
		std::string pv_moves;
		{
			StateListPtr pv_states(new StateList(1));
			Position pv_pos;
			pv_pos.set(sfen, &pv_states->back(), Threads.main());
			for (const auto& m : extract_pv_tokens_from_info(verified_pv_info))
			{
				Move move = pv_pos.to_move(USI::to_move16(m));
				if (move == MOVE_NONE || move == MOVE_WIN
					|| !(pv_pos.pseudo_legal_s<true>(move) && pv_pos.legal(move)))
					break;
				if (pv_moves.size() + m.size() + 1 > 480)
					break;
				pv_moves += (pv_moves.empty() ? "" : " ") + m;
				pv_states->emplace_back();
				pv_pos.do_move(move, pv_states->back());
			}
		}

		sync_cout << "kachikire sfen " << sfen << " score " << score << " player " << (score > 0 ? "b" : "w")
				  << " pv " << pv_moves << sync_endl;
		return;
	}
	print_stats();
	sync_cout << "kachikire none" << sync_endl;
}

// 通常の乱将棋の対局開始局面を生成し、本番DBの sfens テーブルに入れる SQL の行として output に追記する。
// 採る局面の条件:
//  - 盤上の駒数は max_board_pieces まで (玉を含む。先後それぞれ半分まで。勝ち切れ乱将棋と同じ)
//  - どちらかの玉が今の利きのまま king_entry_steps 手以内に入玉(トライ)できる局面は除く
//  - 深さ verify_depth で読んだ評価値 (アプリに表示される cp) が ±max_cp 以内。読み筋で入玉に向かう局面は除く
//  - 寄るか: 最善手どうしで play_plies 手まで指し進め、その間に詰む (詰みを読み切る) こと。
//    駒得で評価値が傾くだけでは寄るとは限らないので、既定では詰みだけを見る (decisive_cp を指定すると、評価値がその値を超えても寄ったとみなす)。
//    詰まないまま進む局面 (玉が硬すぎてお互い寄らない) と、途中で玉が入玉に向かう局面 (入玉で寄らない) は除く
//   gnp n_sfen 10 depth 12 verify_depth 16 nodes 500000 verify_nodes 5000000 max_cp 200 max_board_pieces 22
//       king_entry_steps 5 play_plies 100 play_depth 10 play_nodes 300000 decisive_cp 0 output ranshogi_sfen_generated.sql
// 途中経過: "normal progress tries <調べた数> accepted <採った数> elapsed_ms <経過ミリ秒>"
// 採った局面: "normal sfen <sfen> cp <先手から見た評価値> decided_ply <決着した手数>"
// 候補ごとの指し進めの結果 (基準の調整用): "info string normal_play result <decided|not_decisive|nyugyoku> ply <手数> max_cp <途中の最大の評価値の絶対値>"
// usi.cpp の後ろで定義。駒の枚数がエンジンの扱える範囲の局面か
bool is_supported_ranshogi_sfen(const std::string& sfen);

void generate_normal_ranshogi_sfen_cmd(Position& pos, istringstream& is, StateListPtr& states)
{
	string token;
	int n_sfen = 10;
	int tries = 100000;
	int depth = 12;
	int verify_depth = 16;
	u64 nodes = 500000;
	u64 verify_nodes = 5000000;
	int max_cp = 200;
	int max_board_pieces = 22;
	int king_entry_steps = 5;
	int play_plies = 100;
	int play_depth = 10;
	u64 play_nodes = 300000;
	// 0 なら詰みだけを「寄った」とみなす
	int decisive_cp = 0;
	string output = "ranshogi_sfen_generated.sql";
	// 指定すると、新しく作らずに、このファイルの既存の局面 (各行 "<id> <盤面> <持ち駒>") を同じ条件で調べる
	string input;
	while (is >> token)
	{
		if (token == "n_sfen") is >> n_sfen;
		else if (token == "input") is >> input;
		else if (token == "tries") is >> tries;
		else if (token == "depth") is >> depth;
		else if (token == "verify_depth") is >> verify_depth;
		else if (token == "nodes") is >> nodes;
		else if (token == "verify_nodes") is >> verify_nodes;
		else if (token == "max_cp") is >> max_cp;
		else if (token == "max_board_pieces") is >> max_board_pieces;
		else if (token == "king_entry_steps") is >> king_entry_steps;
		else if (token == "play_plies") is >> play_plies;
		else if (token == "play_depth") is >> play_depth;
		else if (token == "play_nodes") is >> play_nodes;
		else if (token == "decisive_cp") is >> decisive_cp;
		else if (token == "output") is >> output;
	}

	// 探索内部の評価値 → アプリに表示する cp (USI::value と同じ換算)
	auto to_cp = [](int v) { return v * 100 / int(Eval::PawnValue); };

	struct {
		int unsupported = 0, board_pieces = 0, king_entry = 0, shallow_score = 0, verify_score = 0, pv_nyugyoku = 0, play_nyugyoku = 0, not_decisive = 0, accepted = 0;
	} stats;
	auto print_stats = [&]() {
		sync_cout << "info string normal_stats unsupported " << stats.unsupported << " board_pieces " << stats.board_pieces
			<< " king_entry " << stats.king_entry
			<< " shallow_score " << stats.shallow_score << " verify_score " << stats.verify_score
			<< " pv_nyugyoku " << stats.pv_nyugyoku << " play_nyugyoku " << stats.play_nyugyoku
			<< " not_decisive " << stats.not_decisive << " accepted " << stats.accepted << sync_endl;
	};

	// sfen から moves を指した局面を pos に作る (千日手の判定のため、指し手の履歴も探索に渡す)
	auto setup_position = [&](const string& sfen, const std::vector<Move>& moves) {
		states = StateListPtr(new StateList(1));
		pos.set(sfen, &states->back(), Threads.main());
		Threads.main()->game_root_sfen = sfen;
		Threads.main()->moves_from_game_root.clear();
		for (Move m : moves)
		{
			states->emplace_back();
			pos.do_move(m, states->back());
			Threads.main()->moves_from_game_root.emplace_back(m);
		}
	};

	// pos の局面を読む。結果は Threads.main()->last_result
	auto think = [&](int d, u64 node_limit) -> const MainThread::LastSearchResult& {
		Threads.main()->last_result.valid = false;
		std::ostringstream oss;
		oss << "depth " << d;
		if (node_limit > 0)
			oss << " nodes " << node_limit;
		oss << " silent";
		std::istringstream go_is(oss.str());
		go_cmd(pos, go_is, states);
		Threads.main()->wait_for_search_finished();
		return Threads.main()->last_result;
	};

	auto is_mate_score = [](int v) { return abs(v) >= (int)VALUE_MATE_IN_MAX_PLY; };

	// 最善手どうしで指し進め、寄るか (詰みに至るか) を調べる。
	// 返り値: 決着した手数 (詰み・詰みの読み切り・投了、または decisive_cp を指定したときは評価値がそれを超えた手数)。
	//         決着しなければ -1、入玉に向かったら -2
	auto play_until_decided = [&](const string& sfen) -> int {
		std::vector<Move> moves;
		int max_abs_cp = 0;
		auto report = [&](const char* result, int ply) {
			sync_cout << "info string normal_play result " << result << " ply " << ply << " max_cp " << max_abs_cp << sync_endl;
		};

		for (int ply = 0; ply < play_plies; ply++)
		{
			setup_position(sfen, moves);
			// 指せる手が無い = 詰み
			if (MoveList<LEGAL_ALL>(pos).size() == 0)
			{
				report("decided", ply);
				return ply;
			}

			const auto& r = think(play_depth, play_nodes);
			if (!r.valid)
			{
				report("not_decisive", ply);
				return -1;
			}
			// 入玉宣言勝ちの手を指そうとした
			if (r.bestmove == MOVE_WIN)
			{
				report("nyugyoku", ply);
				return -2;
			}
			const bool mate_found = is_mate_score((int)r.score);
			const int cp = mate_found ? 100000 : abs(to_cp((int)r.score));
			max_abs_cp = std::max(max_abs_cp, cp);
			if (r.bestmove == MOVE_RESIGN || r.bestmove == MOVE_NONE || mate_found
				|| (decisive_cp > 0 && cp >= decisive_cp))
			{
				report("decided", ply);
				return ply;
			}

			// 指した後に、玉がトライの一歩手前に来たか、すぐ入玉できる位置に来たら、入玉で寄らない局面とみなす
			const Move best = r.bestmove;
			StateInfo si;
			pos.do_move(best, si);
			const bool entering = kings_reach_forbidden_ranks_6x6(pos)
				|| king_steps_to_try_rank_6x6(pos, BLACK) <= 2
				|| king_steps_to_try_rank_6x6(pos, WHITE) <= 2;
			pos.undo_move(best);
			if (entering)
			{
				report("nyugyoku", ply + 1);
				return -2;
			}
			moves.push_back(best);
		}
		report("not_decisive", play_plies);
		return -1;
	};

	// 1局面を条件で調べる。満たせば nullptr、満たさなければ除いた理由を返し、cp / pv_info / decided_ply に結果を入れる。
	// check_shallow: 深い読みの前に浅い読みでふるいにかけるか (生成時は速くするため。既存の局面を調べるときは深い読みだけ)
	auto check_position = [&](const string& sfen, bool check_shallow, int& cp, std::string& pv_info, int& decided_ply) -> const char* {
		// 駒の枚数が標準を超える局面 (以前のルールの局面) はエンジンが落ちるので読み込まない
		if (!is_supported_ranshogi_sfen(sfen))
		{
			stats.unsupported++;
			return "unsupported";
		}

		setup_position(sfen, {});

		// 盤上の駒数 (玉を含めて先後それぞれ max_board_pieces の半分まで)
		if (max_board_pieces > 0
			&& ((int)pos.pieces(BLACK).pop_count() > max_board_pieces / 2 || (int)pos.pieces(WHITE).pop_count() > max_board_pieces / 2))
		{
			stats.board_pieces++;
			return "board_pieces";
		}

		// どちらかの玉がすぐ入玉(トライ)できる局面は除く (探索の前に盤面だけで判定する)
		if (king_entry_steps > 0
			&& (king_steps_to_try_rank_6x6(pos, BLACK) <= king_entry_steps || king_steps_to_try_rank_6x6(pos, WHITE) <= king_entry_steps))
		{
			stats.king_entry++;
			return "king_entry";
		}

		// 浅い読みで互角に近いか (深い読みで少し動くので、少し広めに見る)
		if (check_shallow)
		{
			const auto& r = think(depth, nodes);
			if (!r.valid || is_mate_score((int)r.score) || abs(to_cp((int)r.score)) > max_cp * 3 / 2)
			{
				stats.shallow_score++;
				return "shallow_score";
			}
			setup_position(sfen, {});
		}

		// 深い読みで ±max_cp 以内か
		const auto& vr = think(verify_depth, verify_nodes);
		cp = vr.valid ? to_cp((int)vr.score) : 0;
		if (!vr.valid || is_mate_score((int)vr.score) || abs(cp) > max_cp)
		{
			stats.verify_score++;
			return "verify_score";
		}
		pv_info = vr.pv_info;

		// 読み筋で入玉に向かう局面は除く (pos は初期局面のまま)
		if (!pv_info_avoids_nyugyoku_ranks_6x6(pos, pv_info))
		{
			stats.pv_nyugyoku++;
			return "pv_nyugyoku";
		}

		// 寄るか (詰まないまま進む局面、入玉に向かう局面は除く)
		decided_ply = play_until_decided(sfen);
		if (decided_ply == -2)
		{
			stats.play_nyugyoku++;
			return "play_nyugyoku";
		}
		if (decided_ply < 0)
		{
			stats.not_decisive++;
			return "not_decisive";
		}
		return nullptr;
	};

	const auto start_time = std::chrono::steady_clock::now();
	auto elapsed_ms = [&]() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
	};

	// 既存の局面を調べるモード: input の各行 "<id> <盤面> <持ち駒>" (sfens テーブルの id と sfen) を同じ条件で調べ、
	// 満たさない局面を無効にする UPDATE 文を output に追記する
	if (!input.empty())
	{
		std::ifstream in(input);
		std::vector<std::string> disabled_ids;
		std::string line;
		int checked = 0;
		while (std::getline(in, line))
		{
			std::istringstream ls(line);
			std::string id, board, hand;
			if (!(ls >> id >> board))
				continue;
			if (!(ls >> hand))
				hand = "-";

			const std::string sfen = board + " b " + hand + " 1";
			int cp = 0;
			std::string pv_info;
			int decided_ply = -1;
			const char* reason = check_position(sfen, false, cp, pv_info, decided_ply);
			checked++;
			sync_cout << "normal check id " << id << " result " << (reason ? reason : "ok") << " cp " << cp
				<< " elapsed_ms " << elapsed_ms() << sync_endl;
			if (reason)
				disabled_ids.push_back(id);
		}

		if (!disabled_ids.empty())
		{
			std::ofstream out(output, std::ios_base::app);
			out << "UPDATE sfens SET available = 0 WHERE id IN (";
			for (size_t i = 0; i < disabled_ids.size(); i++)
				out << (i == 0 ? "" : ", ") << disabled_ids[i];
			out << ");\n";
		}

		print_stats();
		sync_cout << "normal check done checked " << checked << " disabled " << disabled_ids.size() << sync_endl;
		return;
	}

	int accepted = 0;

	for (int i = 0; i < tries && accepted < n_sfen; i++)
	{
		if (i % 20 == 0)
			sync_cout << "normal progress tries " << i << " accepted " << accepted << " elapsed_ms " << elapsed_ms() << sync_endl;

		string sfen = generate_ranshogi_random_sfen(max_board_pieces);
		// 詰み・王手の局面と、先手番でない局面は使わない
		if (sfen.empty() || sfen.find(" b ") == string::npos)
			continue;

		int cp = 0;
		std::string pv_info;
		int decided_ply = -1;
		if (check_position(sfen, true, cp, pv_info, decided_ply) != nullptr)
			continue;

		accepted++;
		stats.accepted++;

		// sfens テーブルの sfen は「盤面 持ち駒」(手番と手数は含めない)。評価値は既存の局面と同じく絶対値
		std::istringstream sfen_is(sfen);
		string board, side, hand;
		sfen_is >> board >> side >> hand;

		// 読み筋は列の長さ (500文字) に収まる分だけ
		std::string pv_moves;
		for (const auto& m : extract_pv_tokens_from_info(pv_info))
		{
			if (pv_moves.size() + m.size() + 1 > 480)
				break;
			pv_moves += (pv_moves.empty() ? "" : " ") + m;
		}

		std::ofstream out(output, std::ios_base::app);
		out << "INSERT INTO sfens (sfen, score, pv, available) VALUES ('" << board << " " << hand << "', "
			<< cp << ", '" << pv_moves << "', 1);\n";

		sync_cout << "normal sfen " << sfen << " cp " << cp << " decided_ply " << decided_ply << sync_endl;
	}

	print_stats();
	sync_cout << "normal done accepted " << accepted << sync_endl;
}

std::vector<std::string> split(const std::string &line, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(line);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

#if defined(USE_MATE_DFPN)
// 詰みチェック("mck"コマンド)。positionで設定した現局面について、df-pn詰み探索(王手の連続で詰むか)で調べる。
//   mck [nodes N] [opp_nodes N] [mem MB]
//     nodes     : 手番側の詰み探索のノード上限
//     opp_nodes : 手番側の各合法手のあと、相手側の詰み探索のノード上限
//     mem       : df-pnが使うメモリ[MB]
// 出力(1行):
//   mck mated_at_root                      : 初期局面ですでに手番側が詰んでいる
//   mck stm_mate ply P pv ...              : 手番側に王手の連続で詰みがある(P手詰め)
//   mck opp_mate moves N                   : 手番側がどう指しても、相手に王手の連続で詰みがある
//   mck no_mate escape M [unknown K]       : どちらでもない(Mは相手の詰みを逃れる手番側の指し手の例)
//   mck unknown stm S opp K                : ノード上限内に判定できなかった
void mate_check_cmd(Position& pos, istringstream& is)
{
	u64 nodes = 1000000;
	u64 opp_nodes = 200000;
	size_t mem = 128;
	string token;
	while (is >> token)
	{
		if (token == "nodes") is >> nodes;
		else if (token == "opp_nodes") is >> opp_nodes;
		else if (token == "mem") is >> mem;
	}

	if (pos.is_mated())
	{
		cout << "mck mated_at_root" << endl;
		return;
	}

	Mate::Dfpn::MateDfpnSolver dfpn(Mate::Dfpn::DfpnSolverType::Node64bit);
	dfpn.alloc(mem);

	// 手番側に詰みがあるか
	const Move stm_result = dfpn.mate_dfpn(pos, nodes);
	if (stm_result != MOVE_NONE && stm_result != MOVE_NULL)
	{
		cout << "mck stm_mate ply " << dfpn.get_mate_ply() << " pv";
		for (auto m : dfpn.get_pv())
			cout << ' ' << m;
		cout << endl;
		return;
	}
	const bool stm_unknown = stm_result == MOVE_NONE;

	// 手番側のどの指し手に対しても、相手に詰みがあるか(1つでも逃れる手があれば詰みではない)
	int legal_moves = 0;
	int unknown = 0;
	Move escape = MOVE_NONE;
	for (auto m : MoveList<LEGAL_ALL>(pos))
	{
		++legal_moves;
		StateInfo si;
		pos.do_move(m, si);
		Move opp_result;
		if (pos.is_mated())
			opp_result = MOVE_NULL; // この手で相手を詰ませた(手番側の勝ち)
		else
			opp_result = dfpn.mate_dfpn(pos, opp_nodes);
		pos.undo_move(m);

		if (opp_result == MOVE_NULL)
		{
			escape = m;
			break;
		}
		if (opp_result == MOVE_NONE)
			++unknown;
	}

	if (escape != MOVE_NONE)
	{
		cout << "mck no_mate escape " << escape;
		if (stm_unknown || unknown > 0)
			cout << " unknown " << (unknown + (stm_unknown ? 1 : 0));
		cout << endl;
	}
	else if (unknown == 0 && legal_moves > 0)
		cout << "mck opp_mate moves " << legal_moves << endl;
	else
		cout << "mck unknown stm " << (stm_unknown ? 1 : 0) << " opp " << unknown << endl;
}
#endif

void check(Position& pos, istringstream& is, StateListPtr& states)
{

	std::ifstream file("ranshogi_sfen.txt"); // 読み込むCSVファイルの名前を指定
    if (!file.is_open()) {
        std::cerr << "ファイルを開くことができませんでした。" << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::vector<std::string> tokens = split(line, ',');

		string sfen = tokens[1];
		//string sfen = "5k/+N1+R3/n1G2l/Ks1r+b1/6/S1G1P1 b Pr 1";
		//string sfen = "5k/+N1+R3/n1G2l/Ks1r+b1/6/S1G1P1 b Pr 1";
		//string sfen = "1+spS1g/P1n1S1/3nk1/+SG+S3/KG4/2P+pg1 b Gg 1";
		//string sfen = "p+S1+ppp/s+nkP+n+L/1pp+bPr/Ll3+B/1P1K1P/P1+L+P1s b R2Ng 1";
		//string sfen = "1+L1s2/+n1k3/nsl1rl/1R+BB2/GpKppg/S+l1GPS b Pg 1";
		states = StateListPtr(new StateList(1));
		pos.set(sfen , &states->back() , Threads.main());
		std::vector<Move> moves_from_game_root;
		Threads.main()->game_root_sfen = sfen;
		Threads.main()->moves_from_game_root = std::move(moves_from_game_root);

		go_cmd(pos, is, states);
    }

    file.close();
}


void mysearch_cmd(Position& pos)
{

	StateInfo si;
	auto& limits = Search::Limits;
	limits.enteringKingRule = to_entering_king_rule(Options["EnteringKingRule"]);

    pos.set("kp4/5K/6/6/6/b5 b - 1", &si, Threads.main());
    //cout << pos.DeclarationWin() << endl;
}

// --------------------
// テスト用にqsearch(),search()を直接呼ぶ
// --------------------

#if defined(EVAL_LEARN)
void qsearch_cmd(Position& pos)
{
	cout << "qsearch : ";
	auto pv = Learner::qsearch(pos);
	cout << "Value = " << pv.first << " , PV = ";
	for (auto m : pv.second)
		cout << m << " ";
	cout << endl;
}




void search_cmd(Position& pos, istringstream& is)
{
	string token;
	int depth = 1;
	int multi_pv = (int)Options["MultiPV"];
	while (is >> token)
	{
		if (token == "depth")
			is >> depth;
		if (token == "multipv")
			is >> multi_pv;
	}

	cout << "search depth = " << depth << " , multi_pv = " << multi_pv << " : ";
	//auto pv = ::search(pos , depth , multi_pv);
	//cout << "Value = " << pv.first << " , PV = ";
	//for (auto m : pv.second)
		//cout << m << " ";
    //cout << endl;
}

#endif

// --------------------
// 　　USI応答部
// --------------------

// USI応答部本体
void usi_cmdexec(Position& pos, StateListPtr& states, string& cmd)
{
	string token;

	{
		istringstream is(cmd);

		token.clear(); // getlineが空を返したときのためのクリア
		is >> skipws >> token;

		if (token == "quit" || token == "stop" || token == "gameover")
		{
			// USIプロトコルにはUCIプロトコルから、
			// gameover win | lose | draw
			// が追加されているが、stopと同じ扱いをして良いと思う。
			// これハンドルしておかないとponderが停止しなくて困る。
			// gameoverに対してbestmoveは返すべきではないのかも知れないが、
			// それを言えばstopにだって…。

#if defined(USE_GAMEOVER_HANDLER)
			// "gameover"コマンドに対するハンドラを呼び出したいのか？
			if (token == "gameover")
				gameover_handler(cmd);
#endif

			// "go infinite" , "go ponder"などで思考を終えて寝てるかも知れないが、
			// そいつらはThreads.stopを待っているので問題ない。
			Threads.stop = true;

		} else if (token == "ponderhit")
		{
			if (Options["Stochastic_Ponder"])
			{
				// Stochastic Ponder hit

				// まず探索スレッドを停止させる。
				// ただしこの時にbestmoveを返してはならないので、これはSearch::Limits.silentで抑制する。
				auto org = Search::Limits.silent;
				Search::Limits.silent = true;
				Threads.stop = true;
				// 終了を待機しないとsilentの解除ができない。
				Threads.main()->wait_for_search_finished();
				Search::Limits.silent = org;

				// 前回と同様のgoコマンドをそのまま送る。ただし"ponder"の文字は無視する。
				// last_go_cmd_stringには先頭に"go"の文字があるが、それはgo_cmdのなかで無視されるので気にしなくて良い。
				istringstream iss(Threads.main()->last_go_cmd_string);
				go_cmd(pos, iss, states, true);
			}
			else {
				// ponderhitに追加パラメーターがあるか？(USI拡張プロトコル)

#if defined(USE_TIME_MANAGEMENT)
				bool token_processed = parse_ponderhit(is);
				// 追加パラメーターを処理したなら今回の思考時間を再計算する。
				if (token_processed)
					Time.reinit();
#endif

				// 通常のponder
				Time.reset_for_ponderhit();     // ponderhitから計測しなおすべきである。
				Threads.main()->ponder = false; // 通常探索に切り替える。
			}
		}

		// 起動時いきなりこれが飛んでくるので速攻応答しないとタイムアウトになる。
		else if (token == "usi")
			sync_cout << engine_info() << Options << "usiok" << sync_endl;

		// オプションを設定する
		else if (token == "setoption") setoption_cmd(is);

		// 与えられた局面について思考するコマンド
		else if (token == "go") {
			Threads.main()->last_go_cmd_string = cmd;       // Stochastic_Ponderで使うので保存しておく。
			go_cmd(pos, is, states);
		}

		// (思考などに使うための)開始局面(root)を設定する
		else if (token == "position") {
			Threads.main()->last_position_cmd_string = cmd; // 保存しておく。
			position_cmd(pos, is, states);
		}

		// "usinewgame"はゲーム中にsetoptionなどを送らないことを宣言するためのものだが、
		// 我々はこれに関知しないので単に無視すれば良い。
		// やねうら王では、時間のかかる初期化はisreadyの応答でやっている。
		// Stockfishでは、Search::clear() (時間のかかる処理)をここで呼び出しているようだが。
		// そもそもで言うと、"usinewgame"に対してはエンジン側は何ら応答を返さないので、
		// GUI側は、エンジン側が処理中なのかどうかが判断できない。
		// なのでここで長い時間のかかる処理はすべきではないと思うのだが。
		else if (token == "usinewgame") return;

		// 思考エンジンの準備が出来たかの確認
		else if (token == "isready") is_ready_cmd(pos, states);

		else if (token == "evalcsv_last") evalcsv_last_cmd(pos, is, states);
		// 以下、デバッグのためのカスタムコマンド(非USIコマンド)
		// 探索中には使わないようにすべし。

#if defined(USER_ENGINE)
		// ユーザーによる実験用コマンド。user.cppのuser()が呼び出される。
		else if (token == "user") user_test(pos, is);
#endif

		// ベンチコマンド(これは常に使える)
		else if (token == "bench") bench_cmd(pos, is);

		// 現在の局面を表示する。(デバッグ用)
		else if (token == "d") cout << pos << endl;

		// USI Commands from File
		else if (token == "f") {
			string filename = "";
			is >> filename;
			if (!filename.empty())
			{
				filename += ".txt";
				sync_cout << "USI Commands from File = " << filename << sync_endl;
				vector<string> lines;
				SystemIO::ReadAllLines(filename, lines);
				for (auto& line : lines)
					std_input.push(line);
			}
		}

		// 現在の局面について評価関数を呼び出して、その値を返す。
		else if (token == "eval") cout << "eval = " << Eval::compute_eval(pos) << endl;
		else if (token == "evalstat") Eval::print_eval_stat(pos);

		// この実行ファイルをコンパイルしたコンパイラの情報を出力する。
		else if (token == "compiler") sync_cout << compiler_info() << sync_endl;

		// -- 以下、やねうら王独自拡張のカスタムコマンド

		// config.hで設定した値などについて出力する。
		else if (token == "config") sync_cout << config_info() << sync_endl;

		// オプションを取得する(USI独自拡張)
		else if (token == "getoption") getoption_cmd(is);

		// 指し手生成祭りの局面をセットする。
		else if (token == "matsuri") pos.set("l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w GR5pnsg 1", &states->back(), Threads.main());

		// "position sfen"の略。
		else if (token == "sfen") position_cmd(pos, is, states);

		// ログファイルの書き出しのon
		// 備考)
		// Stockfishの方は、エンジンオプションでログの出力ファイル名を指定できるのだが、
		// ログ自体はホスト側で記録することが多いので、ファイル名は固定でいいや…。
		else if (token == "log") start_logger("io_log.txt");

		else if (token == "my") mysearch_cmd(pos);
#if defined(EVAL_LEARN)
		// テスト用にqsearch(),search()を直接呼ぶコマンド
		else if (token == "qsearch") qsearch_cmd(pos);
		else if (token == "search") search_cmd(pos,is);
#endif

		// generate ranshogi sfens
		else if (token == "grs") generate_ranshogi_sfen_cmd(pos, is, states);
		// generate a kachikire (winning advantage) position
		else if (token == "gkp") generate_kachikire_sfen_cmd(pos, is, states);
		// generate normal Ranshogi starting positions (balanced, piece-limited, decisive)
		else if (token == "gnp") generate_normal_ranshogi_sfen_cmd(pos, is, states);
		else if (token == "chk") check(pos, is, states);
#if defined(USE_MATE_DFPN)
		// 詰みチェック(df-pn)。position で局面を設定してから呼ぶ
		else if (token == "mck") mate_check_cmd(pos, is);
#endif

		// この局面での指し手をすべて出力
		else if (token == "moves") {
			for (auto m : MoveList<LEGAL_ALL>(pos))
				cout << m.move << ' ';
			cout << endl;
		}

		// この局面の手番側がどちらであるかを返す。BLACK or WHITE
		else if (token == "side") cout << (pos.side_to_move() == BLACK ? "black":"white") << endl;

		// この局面が詰んでいるかの判定
		else if (token == "mated") cout << pos.is_mated() << endl;

		// この局面のhash keyの値を出力
		else if (token == "key") cout << hex << pos.state()->hash_key() << dec << endl;

		// 探索の終了を待機するコマンド("stop"は送らずに。goコマンドの終了を待機できて便利。)
		else if (token == "wait") Threads.main()->wait_for_search_finished();

		// 一定時間待機するコマンド。("quit"の前に一定時間待ちたい時などに用いる。sleep 1000 == 1秒待つ)
		else if (token == "sleep") { u64 ms; is >> ms; Tools::sleep(ms); }

#if defined(MATE_1PLY) && defined(LONG_EFFECT_LIBRARY)
		// この局面での1手詰め判定
		else if (token == "mate1") cout << pos.mate1ply() << endl;
#endif

#if defined (ENABLE_TEST_CMD)
		// テストコマンド
		else if (token == "test") Test::test_cmd(pos, is);
#endif

		// UnitTest
		else if (token == "unittest") Test::UnitTest(pos, is);

#if defined (ENABLE_MAKEBOOK_CMD) && (defined(EVAL_LEARN) || defined(YANEURAOU_ENGINE_DEEP))
		// 定跡を作るコマンド
		else if (token == "makebook") Book::makebook_cmd(pos, is);
#endif

#if defined (EVAL_LEARN)
		else if (token == "gensfen") Learner::gen_sfen(pos, is);
		else if (token == "learn") Learner::learn(pos, is);

#if defined (GENSFEN2019)
		// 開発中の教師局面生成コマンド
		else if (token == "gensfen2019") Learner::gen_sfen2019(pos, is);
#endif

#endif

#if defined(USE_YO_CLUSTER)
#if defined(YANEURAOU_ENGINE_DEEP) || defined(YANEURAOU_ENGINE_NNUE)
		else if (token == "cluster")
			// cluster時のUSIメッセージの処理ループ
			YaneuraouTheCluster::cluster_usi_loop(pos, is);
#endif
#endif

		else
		{
			//    簡略表現として、
			//> threads 1
			//      のように指定したとき、
			//> setoption name Threads value 1
			//      と等価なようにしておく。

			if (!token.empty())
			{
				string value;
				is >> value;

				for (auto& o : Options)
				{
					// 大文字、小文字を無視して比較。
					if (!StringExtension::stricmp(token, o.first))
					{
						Options[o.first] = value;
						sync_cout << "Options[" << o.first << "] = " << value << sync_endl;

						goto OPTION_FOUND;
					}
				}
				sync_cout << "No such option: " << token << sync_endl;
			OPTION_FOUND:;
			}
		}
	}
}

// USI応答部ループ
void USI::loop(int argc, char* argv[])
{
	// 探索開始局面(root)を格納するPositionクラス
	// "position"コマンドで設定された局面が格納されている。
	Position pos;

	string cmd, token;

	// 局面を遡るためのStateInfoのlist。
	StateListPtr states(new StateList(1));

	std_input.parse_args(argc,argv);

	// このファイルがあれば、この内容を実行してやる。
	const string startup = "startup.txt";
	vector<string> lines;
	if (SystemIO::ReadAllLines(startup, lines).is_ok())
	{
		for (auto& line : lines)
			std_input.push(line);
	}

	do
	{
		cmd = std_input.input();
		usi_cmdexec(pos, states, cmd);

		// quit検知
		istringstream is(cmd);
		is >> skipws >> token;
	} while (token != "quit");

	// quitが来た時点ではまだ探索中かも知れないのでmain threadの停止を待つ。
	Threads.main()->wait_for_search_finished();
}

// --------------------
// USI関係の記法変換部
// --------------------

namespace {
	// USIの指し手文字列などに使われている盤上の升を表す文字列をSquare型に変換する
	// 変換できなかった場合はSQ_NBが返る。高速化のために用意した。
	Square usi_to_sq(char f, char r)
	{
		File file = toFile(f);
		Rank rank = toRank(r);

		if (is_ok(file) && is_ok(rank))
			return file | rank;

		return SQ_NB;
	}
}

#if defined(USE_PIECE_VALUE)
// スコアを歩の価値を100として正規化して出力する。
// USE_PIECE_VALUEが定義されていない時は正規化しようがないのでこの関数は呼び出せない。
std::string USI::value(Value v)
{
	ASSERT_LV3(-VALUE_INFINITE < v && v < VALUE_INFINITE);

	std::stringstream s;

	// 置換表上、値が確定していないことがある。
	if (v == VALUE_NONE)
		s << "none";
	else if (abs(v) < VALUE_MATE_IN_MAX_PLY)
		s << "cp " << v * 100 / int(Eval::PawnValue);
	else if (v == -VALUE_MATE)
		// USIプロトコルでは、手数がわからないときには "mate -"と出力するらしい。
		// 手数がわからないというか詰んでいるのだが…。これを出力する方法がUSIプロトコルで定められていない。
		// ここでは"-0"を出力しておく。
		// ※　ShogiGUIだと、これで"+詰"と出力されるようである。
		s << "mate -0";
	else
		s << "mate " << (v > 0 ? VALUE_MATE - v : -VALUE_MATE - v);

	return s.str();
}
#endif

// Square型をUSI文字列に変換する
std::string USI::square(Square s) {
	return std::string{ char('a' + file_of(s)), char('1' + rank_of(s)) };
}

// 指し手をUSI文字列に変換する。
std::string USI::move(Move   m) { return move(Move16(m)); }
std::string USI::move(Move16 m)
{
	std::stringstream ss;
	if (!is_ok(m))
	{
		ss << ((m == MOVE_RESIGN) ? "resign" :
			   (m == MOVE_WIN)    ? "win" :
			   (m == MOVE_NULL)   ? "null" :
			   (m == MOVE_NONE)   ? "none" :
			    "");
	}
	else if (is_drop(m))
	{
		ss << move_dropped_piece(m);
		ss << '*';
		ss << to_sq(m);
	}
	else {
		ss << from_sq(m);
		ss << to_sq(m);
		if (is_promote(m))
			ss << '+';
	}
	return ss.str();
}

// 読み筋をUSI文字列化して返す。
// " 7g7f 8c8d" のように返る。
std::string USI::move(const std::vector<Move>& moves)
{
	std::ostringstream oss;
	for (const auto& move : moves) {
		oss << " " << move;
	}
	return oss.str();
}


// 局面posとUSIプロトコルによる指し手を与えてもし可能なら等価で合法な指し手を返す。
// また合法でない指し手の場合、エラーである旨を出力する。
Move USI::to_move(const Position& pos, const std::string& str)
{
	// 全合法手のなかからusi文字列に変換したときにstrと一致する指し手を探してそれを返す
	//for (const ExtMove& ms : MoveList<LEGAL_ALL>(pos))
	//  if (str == move_to_usi(ms.move))
	//    return ms.move;

	// ↑のコードは大変美しいコードではあるが、棋譜を大量に読み込むときに時間がかかるうるのでもっと高速な実装をする。

	if (str == "resign")
		return MOVE_RESIGN;

	if (str == "win")
		return MOVE_WIN;

	// パス(null move)入力への対応 {UCI: "0000", GPSfish: "pass"}
	if (str == "0000" || str == "null" || str == "pass")
		return MOVE_NULL;

	// usi文字列を高速にmoveに変換するやつがいるがな..
	Move move = pos.to_move(USI::to_move16(str));

	// 現在の局面に至る手順として歩の不成が与えられることはあるので、
	// pseudo_legal_s<true>()で判定する。
	if (pos.pseudo_legal_s<true>(move) && pos.legal(move))
		return move;

	// 入力に非合法手が含まれていた。エラーとして出力すべき。
	sync_cout << "info string Error! : Illegal Input Move : " << str << sync_endl;

	return MOVE_NONE;
}


// USI形式から指し手への変換。本来この関数は要らないのだが、
// 棋譜を大量に読み込む都合、この部分をそこそこ高速化しておきたい。
// やねうら王、独自追加。
Move16 USI::to_move16(const string& str)
{
	Move16 move = MOVE_NONE;

	{
		// さすがに3文字以下の指し手はおかしいだろ。
		if (str.length() <= 3)
			goto END;

		Square to = usi_to_sq(str[2], str[3]);
		if (!is_ok(to))
			goto END;

		bool promote = str.length() == 5 && str[4] == '+';
		bool drop = str[1] == '*';

		if (!drop)
		{
			Square from = usi_to_sq(str[0], str[1]);
			if (is_ok(from))
				move = promote ? make_move_promote16(from, to) : make_move16(from, to);
		}
		else
		{
			for (int i = 1; i <= 7; ++i)
				if (PieceToCharBW[i] == str[0])
				{
					move = make_move_drop16((PieceType)i, to);
					break;
				}
		}
	}

END:
	return move;
}

// namespace USI内のUnitTest。
void USI::UnitTest(Test::UnitTester& tester)
{
	auto section1 = tester.section("USI");

	Position pos;
	StateInfo si;

	// 平手初期化
	auto hirate_init = [&] { pos.set_hirate(&si, Threads.main()); };

	// SFEN文字列でのPosition初期化
	auto sfen_init = [&](const string& sfen) { pos.set(sfen, &si, Threads.main()); };

	// Search::Limitsのalias
	auto& limits = Search::Limits;

	{

		auto section2 = tester.section("to_move()");
		{
			//auto section3 = tester.section("unpromoted pawn move");

			sfen_init("lsgkgs/ppprpp/3p2/1P4/PBPPPP/SGKGSL b - 1");

			//std::string SFEN_HIRATE = "";

			// いま不成を生成するオプションがオフであると仮定する。
			limits.generate_all_legal_moves = false;

			//auto moves = "6a5b 1g1f 4a3b 1f1e 3a2b 2g2f 2c2d 7g7f 3b2c 2f2e 2d2e 2h2e P*2d 2e9e 7a8b 8h6f 8c8d 3i3h 5b6b 7i6h 6b7b 9e9f 7b8c 6h7g 7c7d 7f7e 7d7e 7g8f 8b7c 1e1d 1c1d 8f7e P*7d 7e8f 5a5b P*7b 5b6b 7b7a+ 6b7a P*7e 7a6b 7e7d 7c7d 8f9e P*7e 9f8f 8d8e 8f9f 5c5d 5g5f 6c6d 5f5e 6b6c 5e5d 6c5d 8i7g 6d6e 6f5g 2c3d 6i7h 3d4e 4g4f P*5f 5g6h 4e5e 3h4g 6e6f 4f4e 9c9d 9e9d 8c8d 6g6f P*9e 9d9c 8d9d P*5g 9e9f P*2c 2b2c 5g5f 5e4e P*4f 4e4d 4f4e 4d4e 9c8b 7e7f 7g6e 7d7e P*4f 4e4d 4f4e 4d4e 7h6g R*8i 5i4h 8i9i+ P*4f 4e4d 4f4e 4d4e 6e7c+ 4c4d 7c7d 9d8d 7d8d 7e8d 4g4f 4e4f 6h4f L*4e 6g5g 4e4f 5g4f 5d4c 5f5e P*5c G*2b 2c3d L*5i 4d4e 4f3f 4e4f 3f4f P*4e 4f3f 4e4f 3f4f P*4e 4f3f 4e4f 3f4f P*4e 4f3f 4e4f 3f4f P*4e 4f3f 4e4f 3f4f P*4e 4f3f N*4d 4h3h 4d3f 3g3f 4e4f P*2g S*4g 3h2h 4g3f P*4d 4c4d P*4h 2d2e P*3g 3f2g 2h2g 2e2f 2g2f 3d3e 2f2e G*2d 2e1f 1d1e 1f1g P*2f N*3f 3e3f 3g3f B*2g S*3h 2g3f+ 1g2h N*3e S*1h 2f2g+ 1h2g 3e2g+ 3h2g 3f6c N*3f 4d3d 3f2d P*2f 2g2f S*2g 2h3i P*2h G*3g 2h2i+ 3i2i P*2h 2i3i N*5g 3g4f 5g4i+ 3i4i G*6h P*7i 9i7i G*5h 6h5h 4i5h 7i7h G*6h G*6g 5h4g 7h6h N*3f G*5g 4g3g 6h4h 3g2g 4h4f P*3g 3d4e S*3e 4f4g P*4i 4e5f 4i4h 4g4h 5i5g 5f5g 1i1e 5g5h 2g1f 5h5i P*4f 4h4i 1e1c+ 4i5h 1f1e 5h4g G*3i 4g5h 8b7a+ 6c6d 1e1d 6d5e 8g8f 8e8f P*8g 8f8g";
			auto moves = "3e3d 5b5c 4f3e 5a5b 5e3c 5b6c 5f5e 6c5d 4e4d 4b4c 3c4b 4a4b 5e5d 4c4d 5d6c 6b6c P*5d G*4c 3f4e 3a4a S*2c 4d4e 5d5c 4b3c 2c1d B*2c P*4d 3b3a 1d1c G*6b 4d4c P*5e G*2d 6b5c 3d3c 5c5b 3e4d 5b4b 1e1d 1b1c 4c4b 4a4b G*5f 2c1b G*3b 4b5b 4d5d S*5c 5d5e 5b6b 2d3d 5c4d 3d4d 4e4f+ 2f3e 1b4e 6e6d 6c6d 5e6d 4e2c+ P*4b 4f3f S*5c 6b5a P*6c P*5b 3b2a 2c3b 2a2b P*3d 2e2d 6a6c 6d5e 3b2b G*4c P*6e 1d1c 3a4a 6f6e 3f3e P*6d G*2c 5c6b+ 5a6b 4d4e S*5c 4b4a+ P*4d R*1d 3e2f 3c3b S*1e P*5d 5c4b P*4f P*1b 5f6f 4d4e+";
			// ↑この局面、最後の8f8gが歩の不成だが、これがUSI::to_move()で非合法手扱いされないかをテストする。
			// cf. https://github.com/yaneurao/YaneuraOu/issues/190

			istringstream is(moves);
			string token;
			bool fail = false;

			StateInfo si[512];
			while (is >> token)
			{
				Move m = USI::to_move(pos,token);
				if (m == MOVE_NONE)
					fail = true;

				pos.do_move(m, si[pos.game_ply()]);
			}

			tester.test("pawn's unpromoted move",!fail);

		}

	}
}

#if defined(__EMSCRIPTEN__)
// --------------------
// EMSCRIPTEN support
// --------------------
static StateListPtr states(new StateList(1));

// USI応答部 emscriptenインターフェース
EMSCRIPTEN_KEEPALIVE extern "C" int usi_command(const char *c_cmd) {
	std::string cmd(c_cmd);

	static Position pos;
	string token;

	for (Thread* th : Threads) {
		if (!th->threadStarted)
			return 1;
	}

	usi_cmdexec(pos, states, cmd);

	return 0;
}
#endif
