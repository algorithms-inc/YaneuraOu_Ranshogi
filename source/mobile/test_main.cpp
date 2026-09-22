// ranshogi_api の動作確認用ホストテスト。
// 使い方: ./ranshogi_apitest [eval_dir] [depth] [n_positions]
//
// モバイル実機に持っていく前に、
//   ・NO_SSEビルドで評価関数が読めるか
//   ・sfenを渡してbestmoveが返るか
//   ・1手あたり何msかかるか
// をMac上で確認するためのもの。ライブラリ本体には含めない。

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "ranshogi_api.h"

static double now_ms()
{
	using namespace std::chrono;
	return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
}

int main(int argc, char* argv[])
{
	const char* eval_dir = (argc > 1) ? argv[1] : "evalsave/d12_rl_v1_final";
	const int   depth    = (argc > 2) ? atoi(argv[2]) : 8;
	const int   n_pos    = (argc > 3) ? atoi(argv[3]) : 5;

	printf("=== ranshogi engine API test ===\n");
	printf("engine  : %s\n", rs_version());
	printf("eval_dir: %s\n", eval_dir);
	printf("depth   : %d\n", depth);

	double t0 = now_ms();
	int r = rs_init(eval_dir, 1, 16);
	printf("[rs_init] rc=%d  %.0f ms\n", r, now_ms() - t0);
	if (r != RS_OK)
		return 1;

	// テスト局面を読み込む(1行目のカンマ以前をsfenとみなす)
	std::vector<std::string> sfens;
	{
		std::ifstream ifs("balanced_positions.txt");
		std::string line;
		while ((int)sfens.size() < n_pos && std::getline(ifs, line))
		{
			auto comma = line.find(',');
			if (comma != std::string::npos) line = line.substr(0, comma);
			if (!line.empty()) sfens.push_back(line);
		}
	}
	if (sfens.empty())
		sfens.push_back("startpos");

	// --- 1手ずつ思考させて時間を測る
	char  move[32];
	double total = 0;
	printf("\n--- single move ---\n");
	for (size_t i = 0; i < sfens.size(); ++i)
	{
		const std::string args = (sfens[i] == "startpos") ? sfens[i] : ("sfen " + sfens[i]);
		double t = now_ms();
		r = rs_bestmove(args.c_str(), depth, 0, move, sizeof(move));
		double ms = now_ms() - t;
		total += ms;
		printf("[%zu] %7.0f ms  rc=%d  best=%-6s score=%5d depth=%d\n",
			   i + 1, ms, r, move, rs_last_score(), rs_last_depth());
	}
	printf("average : %.0f ms/move  (depth %d, 1 thread)\n", total / sfens.size(), depth);

	// --- 自己対局20手。合法手を返し続けるか、落ちないかの確認。
	printf("\n--- selfplay 20 plies from position #1 ---\n");
	std::string args = "sfen " + sfens[0];
	std::string moves;
	double t_sp = now_ms();
	int ply = 0;
	for (; ply < 20; ++ply)
	{
		const std::string cmd = args + (moves.empty() ? "" : " moves" + moves);
		r = rs_bestmove(cmd.c_str(), depth, 0, move, sizeof(move));
		if (r != RS_OK) { printf("  stopped: rc=%d at ply %d\n", r, ply); break; }
		if (strcmp(move, "resign") == 0 || strcmp(move, "win") == 0) { printf("  %s at ply %d\n", move, ply); break; }
		moves += " ";
		moves += move;
	}
	printf("  plies=%d  %.0f ms total  (%.0f ms/move)\n", ply, now_ms() - t_sp, (now_ms() - t_sp) / (ply ? ply : 1));
	printf("  moves:%s\n", moves.c_str());

	rs_quit();
	printf("\n=== done ===\n");
	return 0;
}
