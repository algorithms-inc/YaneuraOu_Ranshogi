// 乱将棋(6x6)エンジンのC APIラッパ実装。
// 詳細は ranshogi_api.h を参照のこと。

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "ranshogi_api.h"

#include "../misc.h"
#include "../position.h"
#include "../search.h"
#include "../thread.h"
#include "../types.h"
#include "../usi.h"
#include "../evaluate.h"
#include "../dbg_kachikire.h"

// usi.cppにあるグローバル関数。usi.hに宣言のないgo_cmdだけここで宣言しておく。
// (第4引数はusi.cpp側でデフォルト値falseが指定されているので、ここでは省略しない)
extern void go_cmd(const Position& pos, std::istringstream& is, StateListPtr& states, bool ignore_ponder);
// usi.cpp にある。駒の枚数がエンジンの扱える範囲の局面か。
extern bool is_supported_ranshogi_sfen(const std::string& sfen);

namespace {

	bool g_initialized = false;

	// エンジンが保持する現局面。position_cmd()がこれを書き換える。
	Position     g_pos;
	StateListPtr g_states;

	// 直近の探索結果(rs_last_pv()が返すバッファの寿命を持たせるため)
	std::string  g_last_pv;
	int          g_last_score = 0;
	int          g_last_depth = 0;

	// setoptionを1つ適用する。
	void set_option(const std::string& name, const std::string& value)
	{
		if (Options.count(name))
			Options[name] = value;
	}

} // namespace

int rs_init(const char* eval_dir, int threads, int hash_mb)
{
#if defined(KACHIKIRE_DEBUG_KING_CAPTURE)
	{
		std::ostringstream oss;
		oss << "RS_INIT_BEGIN tid=" << dbg_thread_id() << " name=" << dbg_thread_name()
			<< " initialized=" << g_initialized << " threads=" << threads;
		dbg_log_line(oss.str());
	}
#endif
	if (g_initialized)
		return RS_ERR_ALREADY_INIT;

	if (eval_dir == nullptr || eval_dir[0] == '\0')
		return RS_ERR_BAD_ARG;

	if (threads < 1) threads = 1;
	if (hash_mb < 1) hash_mb = 1;

	// CommandLine::init()はbinaryDirectory/workingDirectoryを決めるためだけに使われる。
	// ライブラリとして組み込む場合はargv[0]が無いのでダミーを渡しておく。
	// (評価関数のパスはEvalDirに絶対パスを与えるので、これらには依存しない)
	static char  arg0[] = "ranshogi";
	static char* argv[] = { arg0, nullptr };
	CommandLine::init(1, argv);

	USI::init(Options);
	Bitboards::init();
	Position::init();
	Search::init();

	set_option("Threads"            , std::to_string(threads));
	set_option("USI_Hash"           , std::to_string(hash_mb));
	// GUIとの通信遅延を見込む必要がないので0にしておく。
	set_option("NetworkDelay"       , "0");
	set_option("NetworkDelay2"      , "0");
	// 定跡は使わない(組み込みサイズを増やしたくないため)。
	set_option("BookFile"           , "no_book");

	Threads.set((size_t)threads);
	Eval::init();

	// 評価関数の場所を指定してから読み込ませる。
	set_option("EvalDir", std::string(eval_dir));
	is_ready();

	if (!USI::load_eval_finished)
		return RS_ERR_EVAL_LOAD;

	g_states = StateListPtr(new StateList(1));
	g_pos.set_hirate(&g_states->back(), Threads.main());

#if defined(RS_EVAL_SELFCHECK)
	// NEON版などで評価関数の計算がNO_SSE版と一致するかを確かめるため、決まった局面の評価値を出す。
	// (Macで同じ行を出して見比べる)
	{
		static const char* const sfens[] = {
			"k+n+l+p2/1P4/rrSp+SS/+P2P1N/2K1+L1/P4+L b SNglp 1",
			"+s3G1/k2gpn/2+P3/r2B2/P1r2P/1PLPPK b B2Nnl 1",
			"+S+nlp2/1Bpbkp/+N+sPN1+p/PPn+L1+s/+L5/2+S1K1 b gp 1",
			"k+p1s1g/1sp3/+p+NPBNn/3GL1/pg1R2/1L1bKG b SP 1",
			"1n2gp/1nP1kL/L1L+pNL/G1p1+b1/3ss1/K1S3 b BGP 1",
			"lGG2k/pp3n/Nl1l2/+sNppRp/+R3p+p/+NK2+ss b sl 1",
		};
		std::ostringstream oss;
		oss << "RS_EVAL_SELFCHECK target=" << TARGET_CPU << " eval=";
		for (const char* sfen : sfens)
		{
			StateInfo si;
			Position pos;
			pos.set(sfen, &si, Threads.main());
			oss << (int)Eval::compute_eval(pos) << ' ';
		}
		sync_cout << oss.str() << sync_endl;
	}
#endif

	g_initialized = true;
#if defined(KACHIKIRE_DEBUG_KING_CAPTURE)
	dbg_log_line("RS_INIT_END tid=" + std::to_string(dbg_thread_id()));
#endif
	return RS_OK;
}

int rs_set_option(const char* name, const char* value)
{
	if (!g_initialized)                    return RS_ERR_NOT_INIT;
	if (name == nullptr || value == nullptr) return RS_ERR_BAD_ARG;
	if (!Options.count(std::string(name)))   return RS_ERR_BAD_ARG;

	Options[std::string(name)] = std::string(value);
	return RS_OK;
}

// 探索を1回走らせる。成否は Threads.main()->last_result.valid で判断する。
// search_moves が空でなければ、その指し手だけを候補にする ("go searchmoves ...")
static void rs_run_search(int depth, int movetime_ms, const std::vector<Move>& search_moves)
{
	std::ostringstream go_ss;
	if (depth       > 0) go_ss << "depth "    << depth       << ' ';
	if (movetime_ms > 0) go_ss << "movetime " << movetime_ms << ' ';
	// 標準出力への"info ..."/"bestmove ..."は不要なので抑制する。
	// (結果は Threads.main()->last_result から直接取り出す)
	go_ss << "silent ";

	// searchmoves は他の指定より後ろに置く。この後ろは全部指し手として読まれるため
	if (!search_moves.empty())
	{
		go_ss << "searchmoves ";
		for (auto m : search_moves)
			go_ss << USI::move(m) << ' ';
	}

	// 前回の結果が残っていると成否の判定ができないので消しておく。
	Threads.main()->last_result.valid = false;

	std::istringstream go_is(go_ss.str());
	go_cmd(g_pos, go_is, g_states, false);
	Threads.main()->wait_for_search_finished();
}

int rs_bestmove(const char* position_args, int depth, int movetime_ms,
                char* out_move, int out_move_len)
{
	// これまでの呼び出し元の動きを変えないため、王手を避ける処理は入れない
	return rs_bestmove_ex(position_args, depth, movetime_ms, 0, out_move, out_move_len);
}

int rs_bestmove_ex(const char* position_args, int depth, int movetime_ms,
                   int avoid_checks_when_lost, char* out_move, int out_move_len)
{
	if (!g_initialized)                          return RS_ERR_NOT_INIT;
	if (position_args == nullptr)                return RS_ERR_BAD_ARG;
	if (out_move == nullptr || out_move_len < 8) return RS_ERR_BUFFER;

	out_move[0] = '\0';

#if defined(KACHIKIRE_DEBUG_KING_CAPTURE)
	{
		std::ostringstream oss;
		oss << "RS_BESTMOVE_BEGIN tid=" << dbg_thread_id() << " name=" << dbg_thread_name()
			<< " main_searching=" << Threads.main()->is_searching()
			<< " depth=" << depth << " args=" << position_args;
		dbg_log_line(oss.str());
	}
#endif

	// --- 局面の設定 ("position"以降の文字列をそのままposition_cmd()に渡す)
	{
		const std::string args(position_args);

		// 以前のルールの局面(駒の枚数が標準を超える)は読み込むとエンジンが落ちるので、読み込む前に弾く
		if (args.compare(0, 5, "sfen ") == 0)
		{
			const auto moves_at = args.find(" moves");
			const std::string sfen = args.substr(5, moves_at == std::string::npos ? std::string::npos : moves_at - 5);
			if (!is_supported_ranshogi_sfen(sfen))
				return RS_ERR_UNSUPPORTED_POSITION;
		}

		std::istringstream is(args);
		position_cmd(g_pos, is, g_states);
	}

	// --- 合法手が無いなら探索せずに投了を返す
	//     (rootMovesが空だと探索側が結果を残さないため、ここで先に弾く)
	if (MoveList<LEGAL_ALL>(g_pos).size() == 0)
	{
		g_last_pv.clear();
		g_last_score = 0;
		g_last_depth = 0;
		std::strncpy(out_move, "resign", (size_t)out_move_len - 1);
		out_move[out_move_len - 1] = '\0';
		return RS_OK;
	}

	// --- 探索条件の組み立て
	if (depth <= 0 && movetime_ms <= 0)
		depth = 8; // 既定値。学習時の対局条件と揃えてある。

	rs_run_search(depth, movetime_ms, std::vector<Move>());

	// 負けが確定したあとの王手ラッシュを避ける。
	// 勝敗がつくまでは何もしないので、competitive な局面での強さは変わらない。
	if (avoid_checks_when_lost != 0)
	{
		auto& first = Threads.main()->last_result;
		const bool lost = first.valid && (int)first.score <= -(int)VALUE_SUPERIOR;
		if (lost && g_pos.gives_check(first.bestmove))
		{
			// 王手以外の合法手を集める
			std::vector<Move> quiet_moves;
			for (auto m : MoveList<LEGAL_ALL>(g_pos))
				if (!g_pos.gives_check(m.move))
					quiet_moves.push_back(m.move);

			// 王手しか無いときは、さきほどの結果をそのまま使う
			if (!quiet_moves.empty())
			{
				const Move  fallback_move  = first.bestmove;
				const Value fallback_score = first.score;
				const Depth fallback_depth = first.depth;
				const std::string fallback_pv = first.pv_info;

				rs_run_search(depth, movetime_ms, quiet_moves);

				// 探索し直しに失敗したときは、さきほどの結果に戻す
				auto& second = Threads.main()->last_result;
				if (!second.valid)
				{
					second.valid    = true;
					second.bestmove = fallback_move;
					second.score    = fallback_score;
					second.depth    = fallback_depth;
					second.pv_info  = fallback_pv;
				}
			}
		}
	}

	auto& r = Threads.main()->last_result;

#if defined(KACHIKIRE_DEBUG_KING_CAPTURE)
	{
		std::ostringstream oss;
		oss << "RS_BESTMOVE_END tid=" << dbg_thread_id() << " valid=" << r.valid
			<< " best=" << (r.valid ? USI::move(r.bestmove) : std::string("-"))
			<< " main_searching=" << Threads.main()->is_searching();
		dbg_log_line(oss.str());
	}
#endif

	if (!r.valid)
		return RS_ERR_NO_RESULT;

	g_last_pv    = r.pv_info;
	g_last_score = (int)r.score;
	g_last_depth = (int)r.depth;

	const std::string mv = USI::move(r.bestmove);
	if ((int)mv.size() >= out_move_len)
		return RS_ERR_BUFFER;

	std::strncpy(out_move, mv.c_str(), (size_t)out_move_len - 1);
	out_move[out_move_len - 1] = '\0';
	return RS_OK;
}

int         rs_last_score(void) { return g_last_score; }
int         rs_last_depth(void) { return g_last_depth; }
const char* rs_last_pv   (void) { return g_last_pv.c_str(); }

void rs_stop(void)
{
	if (!g_initialized) return;

	// USIの"stop"と同じ。探索スレッドがこれを見て打ち切る。
	// 探索していないときに立っても、次の探索の開始時に下ろされる。
	Threads.stop = true;
}

void rs_quit(void)
{
	if (!g_initialized)
		return;

	Threads.set(0);
	g_initialized = false;
}

const char* rs_version(void)
{
	return ENGINE_VERSION;
}
