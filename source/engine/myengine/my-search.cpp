#include "../../types.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>	// std::log(),std::pow(),std::round()
#include <cstring>	// memset()

#include "../../position.h"
#include "../../search.h"
#include "../../thread.h"
#include "../../misc.h"
#include "../../tt.h"
#include "../../book/book.h"
#include "../../movepick.h"
#include "../../usi.h"
#include "../../learn/learn.h"
#include "../../mate/mate.h"

using namespace Search;
using namespace Eval;
using namespace std;

void my_search_thread_init(Thread* th, Stack* ss , Move pv[])
{
	// 先頭10個を初期化しておけば十分。そのあとはsearch()の先頭でss+1,ss+2を適宜初期化していく。
	// RootNodeはss->ply == 0がその条件。
	// ゼロクリアするので、ss->ply == 0となるので大丈夫…。
	std::memset(ss - 7, 0, 10 * sizeof(Stack));

	// counterMovesをnullptrに初期化するのではなくNO_PIECEのときの値を番兵として用いる。
	for (int i = 7; i > 0; i--)
		(ss - i)->continuationHistory = &th->continuationHistory[0][0][SQ_ZERO][NO_PIECE]; // Use as a sentinel

	// Stack(探索用の構造体)上のply(手数)は事前に初期化しておけば探索時に代入する必要がない。
	for (int i = 0; i <= MAX_PLY + 2; ++i)
		(ss + i)->ply = i;

	// 最善応手列(Principal Variation)
	ss->pv = pv;

	// ---------------------
	//   移動平均を用いる統計情報の初期化
	// ---------------------


	// 千日手の時の動的なcontempt。これ、やねうら王では使わないことにする。
	//th->trend = VALUE_ZERO;

}

namespace MyLearner
{
	void init_for_search(Position& pos, Stack* ss , Move pv[], bool qsearch)
	{

		{
			auto& limits = Search::Limits;
			limits.infinite = true;
			limits.silent = true;
			limits.nodes = 0;
			limits.depth = 0;
			limits.max_game_ply = 1 << 16;
			limits.enteringKingRule = EnteringKingRule::EKR_27_POINT;
		}

		{
			drawValueTable[REPETITION_DRAW][BLACK] = VALUE_ZERO;
			drawValueTable[REPETITION_DRAW][WHITE] = VALUE_ZERO;
		}

		{
			auto th = pos.this_thread();

			th->nodes = th->bestMoveChanges = /* th->tbHits = */ th->nmpMinPly = 0;
			th->rootDepth = th->completedDepth = 0;
			my_search_thread_init(th,ss,pv);

			if (!qsearch)
			{
				auto& rootMoves = th->rootMoves;

				rootMoves.clear();
				for (auto m : MoveList<LEGAL>(pos))
					rootMoves.push_back(Search::RootMove(m));

				ASSERT_LV3(!rootMoves.empty());
			}

			th->tt.new_search();
		}

	}

	typedef std::pair<Value, std::vector<Move> > ValueAndPV;
	ValueAndPV qsearch(Position& pos)
	{
		Stack stack[MAX_PLY + 10], *ss = stack + 7;
		Move pv[MAX_PLY + 1];
		std::vector<Move> pvs;

		// 詰まされているのか
		if (pos.is_mated())
		{
			pvs.push_back(MOVE_RESIGN);
			return ValueAndPV(mated_in(/*ss->ply*/ 0 + 1), pvs);
		}

		// 探索の初期化
		init_for_search(pos, ss , pv, /* qsearch = */true);


		//auto bestValue = ::qsearch<PV>(pos, ss, -VALUE_INFINITE, VALUE_INFINITE, 0);

        auto bestValue = VALUE_ZERO;

		// 得られたPVを返す。
		//for (Move* p = &ss->pv[0]; is_ok(*p); ++p)
			//pvs.push_back(*p);

		return ValueAndPV(bestValue, pvs);
	}

}

