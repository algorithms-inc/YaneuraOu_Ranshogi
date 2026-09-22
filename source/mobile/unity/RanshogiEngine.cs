// 乱将棋(6x6)エンジンのUnity側バインディング。
//
// ネイティブ側の実体は source/mobile/ranshogi_api.h / .cpp。
// iOSでは libranshogi.a を Assets/Plugins/iOS/ に置くと自動でリンクされる。
//
// 使い方:
//     await RanshogiEngine.InitAsync();                       // 起動時に1回
//     string move = await RanshogiEngine.BestMoveAsync(sfen, RanshogiEngine.Level.Normal);
//
// 注意: エンジンの状態はネイティブ側にグローバルで1つ。
//       このクラスは内部でセマフォを持ち、同時に1つの思考しか走らせない。

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

public static class RanshogiEngine
{
    // ------------------------------------------------------------------
    //  ネイティブ関数の宣言
    // ------------------------------------------------------------------

#if UNITY_IOS && !UNITY_EDITOR
    const string DLL = "__Internal";     // 静的ライブラリはアプリ本体にリンクされる
#else
    const string DLL = "ranshogi";       // Android(.so) / エディタ用(.bundle,.dylib)
#endif

    [DllImport(DLL)] static extern int rs_init(string evalDir, int threads, int hashMb);
    [DllImport(DLL)] static extern int rs_set_option(string name, string value);
    [DllImport(DLL)] static extern int rs_bestmove(string positionArgs, int depth, int movetimeMs,
                                                   StringBuilder outMove, int outMoveLen);
    [DllImport(DLL)] static extern int rs_last_score();
    [DllImport(DLL)] static extern int rs_last_depth();
    [DllImport(DLL)] static extern void rs_quit();

    // ------------------------------------------------------------------
    //  難易度
    // ------------------------------------------------------------------
    // 探索の深さで強さを決める。数字は Intel Mac(SIMDなしビルド)実測の1手あたりの時間。
    // 実機はこれより数倍遅いと見ておくこと。
    public enum Level
    {
        Easy   = 4,     //   ~5 ms
        Normal = 8,     //  ~25 ms   (学習時の対局条件と同じ)
        Hard   = 12,    // ~500 ms
        Max    = 16,    // ~2.6 s
    }

    // ------------------------------------------------------------------
    //  状態
    // ------------------------------------------------------------------

    static readonly SemaphoreSlim _lock = new SemaphoreSlim(1, 1);
    static bool _initialized;

    public static bool IsReady => _initialized;

    /// <summary>直近の思考の評価値(手番側から見た値。歩=100程度のスケール)。</summary>
    public static int LastScore { get; private set; }

    /// <summary>直近の思考で到達した深さ。</summary>
    public static int LastDepth { get; private set; }

    // ------------------------------------------------------------------
    //  初期化
    // ------------------------------------------------------------------

    /// <summary>
    /// 評価関数を読み込んでエンジンを起動する。ゲーム開始前に1回だけ呼ぶ。
    /// StreamingAssetsに置いた nn.bin を、必要なら読み取り可能な場所へ展開してから渡す。
    /// </summary>
    public static async Task<bool> InitAsync(int threads = 1, int hashMb = 16)
    {
        if (_initialized) return true;

        // 対局開始時のウォームアップと初手の思考が同時に走っても二重初期化しないようにする。
        await _lock.WaitAsync();
        try
        {
            return await DoInitAsync(threads, hashMb);
        }
        finally
        {
            _lock.Release();
        }
    }

    static async Task<bool> DoInitAsync(int threads, int hashMb)
    {
        if (_initialized) return true;

        string evalDir = await PrepareEvalDirAsync();
        if (evalDir == null)
        {
            Debug.LogError("[Ranshogi] nn.bin が見つかりません。");
            return false;
        }

        int rc;
        try
        {
            rc = await Task.Run(() => rs_init(evalDir, threads, hashMb));
        }
        catch (DllNotFoundException e)
        {
            // エディタで動かす場合は Assets/Plugins/libranshogi.dylib が必要。
            // (source/mobile の `make mac` で作れる)
            Debug.LogError($"[Ranshogi] ネイティブライブラリが見つかりません: {e.Message}");
            return false;
        }
        catch (EntryPointNotFoundException e)
        {
            Debug.LogError($"[Ranshogi] ライブラリが古い可能性があります: {e.Message}");
            return false;
        }

        // RS_ERR_ALREADY_INIT(-1)は、シーンを跨いで2回目に呼ばれただけなので成功扱いにする。
        if (rc != 0 && rc != -1)
        {
            Debug.LogError($"[Ranshogi] rs_init failed: rc={rc} dir={evalDir}");
            return false;
        }

        _initialized = true;
        return true;
    }

    /// <summary>
    /// nn.bin のあるディレクトリを返す。
    /// Androidではapkの中を直接fopenできないため、初回だけpersistentDataPathへ展開する。
    /// </summary>
    static async Task<string> PrepareEvalDirAsync()
    {
        const string fileName = "nn.bin";
        string src = Path.Combine(Application.streamingAssetsPath, "eval", fileName);

#if UNITY_ANDROID && !UNITY_EDITOR
        string dstDir = Path.Combine(Application.persistentDataPath, "eval");
        string dst    = Path.Combine(dstDir, fileName);
        if (!File.Exists(dst))
        {
            Directory.CreateDirectory(dstDir);
            using (var req = UnityEngine.Networking.UnityWebRequest.Get(src))
            {
                var op = req.SendWebRequest();
                while (!op.isDone) await Task.Yield();
                if (req.result != UnityEngine.Networking.UnityWebRequest.Result.Success)
                {
                    Debug.LogError($"[Ranshogi] nn.binの展開に失敗: {req.error}");
                    return null;
                }
                File.WriteAllBytes(dst, req.downloadHandler.data);
            }
        }
        return dstDir;
#else
        await Task.CompletedTask;
        return File.Exists(src) ? Path.GetDirectoryName(src) : null;
#endif
    }

    // ------------------------------------------------------------------
    //  思考
    // ------------------------------------------------------------------

    /// <summary>
    /// 局面を渡して最善手を得る。UIスレッドはブロックしない。
    /// </summary>
    /// <param name="sfen">現局面のsfen(例 "1psl1k/GLn+Sbg/2Nppp/1G+p2P/p1p2+P/1K2Rs b B 1")</param>
    /// <param name="level">難易度</param>
    /// <param name="movetimeMs">思考時間の上限(ms)。0なら深さのみで打ち切る。</param>
    /// <returns>"7g7f" 形式の指し手。合法手が無ければ "resign"。失敗時はnull。</returns>
    public static Task<string> BestMoveAsync(string sfen, Level level = Level.Normal, int movetimeMs = 0)
        => BestMoveRawAsync("sfen " + sfen, (int)level, movetimeMs);

    /// <summary>
    /// 開始局面からの指し手列で局面を渡す版。千日手の判定を正しくさせたい場合はこちら。
    /// </summary>
    public static Task<string> BestMoveAsync(string rootSfen, System.Collections.Generic.IEnumerable<string> moves,
                                             Level level = Level.Normal, int movetimeMs = 0)
    {
        var sb = new StringBuilder("sfen ").Append(rootSfen);
        bool any = false;
        foreach (var m in moves)
        {
            if (!any) { sb.Append(" moves"); any = true; }
            sb.Append(' ').Append(m);
        }
        return BestMoveRawAsync(sb.ToString(), (int)level, movetimeMs);
    }

    /// <summary>USIの"position"以降の文字列をそのまま渡す版。</summary>
    public static async Task<string> BestMoveRawAsync(string positionArgs, int depth, int movetimeMs)
    {
        if (!_initialized)
        {
            Debug.LogError("[Ranshogi] InitAsync() が終わっていません。");
            return null;
        }

        await _lock.WaitAsync();
        try
        {
            var buf = new StringBuilder(32);
            int rc;
            try
            {
                rc = await Task.Run(() => rs_bestmove(positionArgs, depth, movetimeMs, buf, buf.Capacity));
            }
            catch (Exception e)
            {
                Debug.LogError($"[Ranshogi] rs_bestmove threw: {e.Message}");
                return null;
            }
            if (rc != 0)
            {
                Debug.LogError($"[Ranshogi] rs_bestmove failed: rc={rc}");
                return null;
            }
            LastScore = rs_last_score();
            LastDepth = rs_last_depth();
            return buf.ToString();
        }
        finally
        {
            _lock.Release();
        }
    }

    /// <summary>USIオプションを設定する。InitAsync()の後に呼ぶこと。</summary>
    public static void SetOption(string name, string value)
    {
        if (_initialized) rs_set_option(name, value);
    }

    /// <summary>アプリ終了時に呼ぶ(呼ばなくても良い)。</summary>
    public static void Quit()
    {
        if (!_initialized) return;
        rs_quit();
        _initialized = false;
    }
}
