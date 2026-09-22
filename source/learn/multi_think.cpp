#include "../types.h"

#if defined(EVAL_LEARN) && defined(YANEURAOU_ENGINE)

#include "multi_think.h"
#include "../tt.h"
#include "../usi.h"

#include <thread>

// On macOS (and MinGW/pthreads platforms) std::thread defaults to a 512KB stack,
// which is too small for deep qsearch recursion in the learner (~7.5KB per frame).
// Use pthread_attr_setstacksize to match the 8MB stack used by search threads.
#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)
#  include <pthread.h>
#  define MULTI_THINK_USE_PTHREADS
   static const size_t MULTI_THINK_STACK_SIZE = 8 * 1024 * 1024;
#endif

void MultiThink::go_think()
{
	// あとでOptionsの設定を復元するためにコピーで保持しておく。
	auto oldOptions = Options;

	// 定跡を用いる場合、on the flyで行なうとすごく時間がかかる＆ファイルアクセスを行なう部分が
	// thread safeではないので、メモリに丸読みされている状態であることをここで保証する。
	Options["BookOnTheFly"] = std::string("false");

	// 評価関数の読み込み等
	// learnコマンドの場合、評価関数読み込み後に評価関数の値を補正している可能性があるので、
	// メモリの破損チェックは省略する。
	is_ready(true);

	// 派生クラスのinit()を呼び出す。
	init();

	// ループ上限はset_loop_max()で設定されているものとする。
	loop_count = 0;
	done_count = 0;

	// threadをOptions["Threads"]の数だけ生成して思考開始。
	auto thread_num = (size_t)Options["Threads"];

	// worker threadの終了フラグの確保
	thread_finished.resize(thread_num);

	// worker threadの起動
	// On macOS/pthreads platforms use pthread with 8MB stack to prevent stack overflow
	// in deep qsearch recursion (~7.5KB per frame, default 512KB is too small).
#if defined(MULTI_THINK_USE_PTHREADS)
	struct WorkerArg { MultiThink* think; size_t id; };
	std::vector<pthread_t> threads(thread_num);
	std::vector<WorkerArg> worker_args(thread_num);
	for (size_t i = 0; i < thread_num; ++i)
	{
		thread_finished[i] = 0;
		worker_args[i] = {this, i};
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, MULTI_THINK_STACK_SIZE);
		pthread_create(&threads[i], &attr, [](void* p) -> void* {
			auto* a = static_cast<WorkerArg*>(p);
			WinProcGroup::bindThisThread(a->id);
			a->think->thread_worker(a->id);
			a->think->thread_finished[a->id] = 1;
			return nullptr;
		}, &worker_args[i]);
		pthread_attr_destroy(&attr);
	}
#else
	std::vector<std::thread> threads;
	for (size_t i = 0; i < thread_num; ++i)
	{
		thread_finished[i] = 0;
		threads.push_back(std::thread([i, this]
		{
			// プロセッサの全スレッドを使い切る。
			WinProcGroup::bindThisThread(i);

			// オーバーライドされている処理を実行
			this->thread_worker(i);

			// スレッドが終了したので終了フラグを立てる
			this->thread_finished[i] = 1;
		}));
	}
#endif

	// すべてのthreadの終了待ちを
	// for (auto& th : threads)
	//  th.join();
	// のように書くとスレッドがまだ仕事をしている状態でここに突入するので、
	// その間、callback_func()が呼び出せず、セーブできなくなる。
	// そこで終了フラグを自前でチェックする必要がある。

	// すべてのスレッドが終了したかを判定する関数
	auto threads_done = [&]()
	{
		// ひとつでも終了していなければfalseを返す
		for (auto& f : thread_finished)
			if (!f)
				return false;
		return true;
	};

	// コールバック関数が設定されているならコールバックする。
	auto do_a_callback = [&]()
	{
		if (callback_func)
			callback_func();
	};


	for (u64 i = 0 ; ; )
	{
		// 全スレッドが終了していたら、ループを抜ける。
		if (threads_done())
			break;

		Tools::sleep(1000);

		// callback_secondsごとにcallback_func()が呼び出される。
		if (++i == callback_seconds)
		{
			do_a_callback();
			// ↑から戻ってきてからカウンターをリセットしているので、
			// do_a_callback()のなかでsave()などにどれだけ時間がかかろうと
			// 次に呼び出すのは、そこから一定時間の経過を要する。
			i = 0;
		}
	}

	// 最後の保存。
	std::cout << std::endl << "finalize..";

	// do_a_callback();
	// →　呼び出し元で保存するはずで、ここでは要らない気がする。

	// 終了したフラグは立っているがスレッドの終了コードの実行中であるということはありうるので
	// join()でその終了を待つ必要がある。
#if defined(MULTI_THINK_USE_PTHREADS)
	for (auto& th : threads)
		pthread_join(th, nullptr);
#else
	for (auto& th : threads)
		th.join();
#endif

	// 全スレッドが終了しただけでfileの書き出しスレッドなどはまだ動いていて
	// 作業自体は完了していない可能性があるのでスレッドがすべて終了したことだけ出力する。
	std::cout << "all threads are joined." << std::endl;

	// Optionsを書き換えたので復元。
	// 値を代入しないとハンドラが起動しないのでこうやって復元する。
	for (auto& s : oldOptions)
		Options[s.first] = std::string(s.second);

}


#endif // defined(EVAL_LEARN)
