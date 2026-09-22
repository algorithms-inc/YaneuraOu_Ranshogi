#ifndef DBG_KACHIKIRE_H_INCLUDED
#define DBG_KACHIKIRE_H_INCLUDED

// 勝ち切れ乱将棋のiOS実機クラッシュ調査用。KACHIKIRE_DEBUG_KING_CAPTUREのときだけ使う。
// 「同じ局面を複数のスレッドが同時に探索している」疑いを確かめるため、
// スレッドIDの取得と、呼び出し履歴(バックトレース)の出力を用意する。

#if defined(KACHIKIRE_DEBUG_KING_CAPTURE)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <execinfo.h>
#include <pthread.h>
#endif

// 現在のスレッドのID(Xcode/lldbの"thread list"に出るtidと同じ値)
inline uint64_t dbg_thread_id()
{
#if defined(__APPLE__)
	uint64_t tid = 0;
	pthread_threadid_np(nullptr, &tid);
	return tid;
#else
	return 0;
#endif
}

// 現在のスレッドの名前(付いていなければ空)
inline std::string dbg_thread_name()
{
#if defined(__APPLE__)
	char name[64] = {};
	pthread_getname_np(pthread_self(), name, sizeof(name));
	return std::string(name);
#else
	return std::string();
#endif
}

// 1行出力する(stdoutとstderrの両方。Xcodeのコンソールにはどちらも出る)
inline void dbg_log_line(const std::string& line)
{
	std::fprintf(stdout, "%s\n", line.c_str());
	std::fflush(stdout);
	std::fprintf(stderr, "%s\n", line.c_str());
	std::fflush(stderr);
}

// 現在のスレッドの呼び出し履歴を出力する
inline void dbg_log_backtrace(const char* tag)
{
#if defined(__APPLE__)
	void* frames[64];
	const int n = backtrace(frames, 64);
	char** symbols = backtrace_symbols(frames, n);
	for (int i = 0; i < n; ++i)
	{
		std::string line = std::string(tag) + " bt " + (symbols ? symbols[i] : "?");
		dbg_log_line(line);
	}
	std::free(symbols);
#else
	(void)tag;
#endif
}

#endif // KACHIKIRE_DEBUG_KING_CAPTURE

#endif // DBG_KACHIKIRE_H_INCLUDED
